/* rh264 -- clean-room H.264 decoder (amalgamated single TU).
 * Public API: include/formats/rh264.h. CAVLC tables extracted from libopenh264
 * encoder rodata (verified prefix-free), inverted into per-instance
 * lookup tables at open.
 *
 * What it implements: I, P and B slices with both CAVLC and CABAC
 * entropy coding; 4:2:0, 4:2:2 and 4:4:4 reconstruction (the chroma
 * planes of 4:4:4 decoded as luma, ChromaArrayType 3) at 8 to 14 bits
 * per sample (High 10 / High 4:2:2 / High 4:4:4 Predictive; above 8
 * bits the planes hold uint16_t samples and the sample kernels are the
 * C instantiation of rh264_bd.inc, the SIMD ones being 8-bit); the
 * full integer transforms (4x4 and 8x8, with scaling matrices) and the
 * lossless transform bypass of High 4:4:4 Predictive (qpprime_y_zero_
 * transform_bypass_flag, 8.5.15); intra and
 * inter prediction with quarter-pel motion compensation, multiple
 * reference pictures (sliding-window and MMCO marking, list
 * modifications), weighted and implicit bi-prediction, spatial and
 * temporal direct; the in-loop deblocking filter; POC types 0/1/2
 * with display-order output; Annex-B and length-prefixed AVCC input.
 * Frame-coded MBAFF pairs decode, as do CAVLC I/P field pictures.
 *
 * What it does not implement: monochrome, separate-colour-plane
 * 4:4:4, and luma and chroma at different depths; SP/SI switching
 * slices; FMO/ASO
 * and redundant pictures; field-coded B and CABAC pictures and
 * field-coded macroblock pairs; encoding.  Out-of-scope streams are
 * refused at the parameter-set or slice level rather than decoded
 * wrongly. */
#include <formats/rh264.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#ifdef HAVE_THREADS
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <rthreads/tpool.h>
#endif

#include <compat/intrinsics.h>

#if defined(_MSC_VER)
#include <intrin.h>   /* _byteswap_uint64 */
#endif
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define RH264_SSE2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define RH264_NEON 1
#endif

/* ==================== rh264_bits.h ==================== */
/* rh264 -- bitstream layer: NAL RBSP + Exp-Golomb. */
struct rh264_vlc_set;
typedef struct { const uint8_t *buf; size_t size; size_t bitpos;
   /* inverse CAVLC tables of the owning decoder instance; NULL (as for
    * parameter-set parsing, which never reads residual) selects the
    * serial matchers */
   const struct rh264_vlc_set *vlc; } rh264_bits;
static void rh264_bits_init(rh264_bits *b, const uint8_t *buf, size_t size){ b->buf=buf; b->size=size; b->bitpos=0; b->vlc=NULL; }
static int rh264_more_data(const rh264_bits *b){ return b->bitpos < b->size*8; }
/* more_rbsp_data (7.2): true when bits remain beyond the current position
 * other than the rbsp_stop_one_bit and its trailing zeros. */
static int rh264_more_rbsp(const rh264_bits *b){
   size_t last=b->size; size_t stop;
   uint8_t v;
   while(last>0&&b->buf[last-1]==0) last--;
   if(!last) return 0;
   v=b->buf[last-1]; stop=last*8;
   while(!(v&1)){ v>>=1; stop--; }
   return b->bitpos < stop-1;
}
#if defined(__GNUC__) || defined(__clang__)
#define RH264_BITS_INLINE static __inline__ __attribute__((always_inline))
#elif defined(_MSC_VER)
#define RH264_BITS_INLINE static __forceinline
#else
#define RH264_BITS_INLINE static
#endif
/* Read the next 1..32 bits at bitpos without consuming them.  Bits past
 * the end of the buffer read as zero, exactly as the serial reader did:
 * every consumer either treats trailing zeros as benign padding or fails
 * the parse downstream, and rh264_more_data / rh264_more_rbsp remain the
 * authoritative end-of-data tests. */
RH264_BITS_INLINE uint32_t rh264_peek(const rh264_bits *b, int n)
{
   size_t byte = b->bitpos >> 3;
   int off     = (int)(b->bitpos & 7);
   uint64_t acc;
   if (byte + 8 <= b->size)
   {
#if defined(_MSC_VER)
      memcpy(&acc, b->buf + byte, 8);
      acc = _byteswap_uint64(acc);
#elif defined(__GNUC__) && defined(__BYTE_ORDER__) && \
      (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
      memcpy(&acc, b->buf + byte, 8);
#elif defined(__GNUC__)
      memcpy(&acc, b->buf + byte, 8);
      acc = __builtin_bswap64(acc);
#else
      acc = ((uint64_t)b->buf[byte]   << 56) | ((uint64_t)b->buf[byte+1] << 48)
          | ((uint64_t)b->buf[byte+2] << 40) | ((uint64_t)b->buf[byte+3] << 32)
          | ((uint64_t)b->buf[byte+4] << 24) | ((uint64_t)b->buf[byte+5] << 16)
          | ((uint64_t)b->buf[byte+6] <<  8) |  (uint64_t)b->buf[byte+7];
#endif
   }
   else
   {
      int i;
      acc = 0;
      for (i = 0; i < 8; i++)
         acc = (acc << 8) | (byte + (size_t)i < b->size ? b->buf[byte + i] : 0);
   }
   return (uint32_t)((acc << off) >> (64 - n));
}
static uint32_t rh264_u1(rh264_bits *b){
   uint32_t v=rh264_peek(b,1); b->bitpos++; return v;
}
static uint32_t rh264_un(rh264_bits *b,int n){
   uint32_t v; if(n<=0) return 0; v=rh264_peek(b,n); b->bitpos+=(size_t)n; return v;
}
/* The serial Exp-Golomb reader, kept for the two cases the peek window
 * cannot settle: a run of 15+ leading zeros, and data exhausted mid-run
 * (both end the parse or a conformant value shortly after). */
static uint32_t rh264_ue_slow(rh264_bits *b){
   int z=0; while(rh264_more_data(b)&&rh264_u1(b)==0) z++;
   if(z==0) return 0;
   if(z>=32) return 0xFFFFFFFFu;
   return ((1u<<z)-1u)+rh264_un(b,z);
}
static uint32_t rh264_ue(rh264_bits *b){
   uint32_t v=rh264_peek(b,32),z;
   if(!v) return rh264_ue_slow(b);
   z=compat_clz_u32(v);
   if(z>15) return rh264_ue_slow(b);
   b->bitpos+=(size_t)(2*z+1);
   return (v>>(31-2*z))-1u;
}
static int32_t rh264_se(rh264_bits *b){ uint32_t k=rh264_ue(b); uint32_t m=(k+1)>>1; return (k&1)?(int32_t)m:-(int32_t)m; }
static uint8_t *rh264_unescape(const uint8_t *nal,size_t len,size_t *out_size){
   uint8_t *r; size_t i,o=0; if(!(r=(uint8_t*)malloc(len?len:1))) return NULL;
   for(i=0;i<len;i++){
      if(i>=2&&nal[i]==0x03&&nal[i-1]==0x00&&nal[i-2]==0x00&&i+1<len&&nal[i+1]<=0x03) continue;
      r[o++]=nal[i];
   }
   *out_size=o; return r;
}
/* Same, into the decoder's scratch block, which only ever grows. */
static uint8_t *rh264_unescape_scratch(struct rh264_video *v,
      const uint8_t *nal, size_t len, size_t *out_size);


/* ==================== rh264_ps.h ==================== */
typedef struct { int valid,profile_idc,level_idc,log2_max_frame_num,pic_order_cnt_type,
   max_num_ref_frames,
   log2_max_poc_lsb,frame_mbs_only_flag,mb_adaptive_frame_field_flag,
   frame_mbs,
   pic_width_in_mbs,pic_height_in_map_units,
   frame_width,frame_height,crop_x,crop_y,
   chroma_format_idc,direct_8x8_inference_flag,
   tb,   /* qpprime_y_zero_transform_bypass_flag */
   poc_type1_always_zero,
   poc1_offset_non_ref, poc1_offset_ttb, poc1_ncycle,
   vui_num_reorder, /* VUI max_num_reorder_frames, -1 when not signalled */
   /* kept for a hardware decoder (rh264_hw_sps_t) */
   sps_id, constraint_flags, gaps_allowed, frame_cropping,
   crop_l, crop_r, crop_t, crop_b;
   int32_t poc1_offset_ref[256]; /* offset_for_ref_frame (POC type 1)  */
   int bit_depth_luma, bit_depth_chroma;   /* 8 outside the High profiles */
   int scaling_present;         /* seq_scaling_matrix_present_flag */
   uint8_t sl_present[12];      /* seq_scaling_list_present_flag   */
   uint8_t sl_usedef[12];       /* first delta selected the default */
   uint8_t sl4[6][16];          /* parsed 4x4 lists (raster)        */
   uint8_t sl8[6][64];          /* parsed 8x8 lists (raster); 4:4:4
                                 * carries six (Y/Cb/Cr x intra/inter) */
   } rh264_sps;
typedef struct { int valid,entropy_coding_mode_flag,pic_order_present_flag,pic_init_qp,
   num_ref_idx_l0_default,num_ref_idx_l1_default,weighted_pred_flag,weighted_bipred_idc,
   deblocking_filter_control_present,constrained_intra_pred_flag,chroma_qp_index_offset,
   transform_8x8_mode,chroma_qp_index_offset2,
   scaling_present; uint8_t sl_present[12],sl_usedef[12],
   sl4[6][16],sl8[6][64];
   int pps_id, sps_id, pic_init_qs; } rh264_pps;
static const uint8_t rh264_zigzag4[16]={0,1,4,8,5,2,3,6,9,12,13,10,7,11,14,15};
/* Field-coded macroblocks scan their coefficients in a different order
 * (Tables 8-12 and 8-13): a line's vertical neighbour within a field is
 * two lines away in the frame, so the field scan favours vertical
 * frequencies.  Selected per picture by rh264_frame.field. */
static const uint8_t rh264_fieldscan4[16]={0,4,1,8,12,5,9,13,2,6,10,14,3,7,11,15};

/* 8x8 zig-zag scan, frame coding: scan position -> raster position. */
static const uint8_t rh264_zigzag8[64]={
    0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
   12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
   35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
   58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static const uint8_t rh264_fieldscan8[64]={
   0, 8,16, 1, 9,24,32,17, 2,25,40,48,56,33,10, 3,
  18,41,49,57,26,11, 4,19,34,42,50,58,27,12, 5,20,
  35,43,51,59,28,13, 6,21,36,44,52,60,29,14,22,37,
  45,53,61,30, 7,15,38,46,54,62,23,31,39,47,55,63
};
#define RH264_SCAN4(f) ((f)->field ? rh264_fieldscan4 : rh264_zigzag4)
#define RH264_SCAN8(f) ((f)->field ? rh264_fieldscan8 : rh264_zigzag8)


/* Default scaling matrices of Tables 7-3 and 7-4 (raster order). */
static const uint8_t rh264_def4_intra[16]={
    6,13,20,28, 13,20,28,32, 20,28,32,37, 28,32,37,42};
static const uint8_t rh264_def4_inter[16]={
   10,14,20,24, 14,20,24,27, 20,24,27,30, 24,27,30,34};
static const uint8_t rh264_def8_intra[64]={
    6,10,13,16,18,23,25,27, 10,11,16,18,23,25,27,29,
   13,16,18,23,25,27,29,31, 16,18,23,25,27,29,31,33,
   18,23,25,27,29,31,33,36, 23,25,27,29,31,33,36,38,
   25,27,29,31,33,36,38,40, 27,29,31,33,36,38,40,42};
static const uint8_t rh264_def8_inter[64]={
    9,13,15,17,19,21,22,24, 13,13,17,19,21,22,24,25,
   15,17,19,21,22,24,25,27, 17,19,21,22,24,25,27,28,
   19,21,22,24,25,27,28,30, 21,22,24,25,27,28,30,32,
   22,24,25,27,28,30,32,33, 24,25,27,28,30,32,33,35};

/* scaling_list() of 7.3.2.1.1.1: delta-coded in zig-zag order, stored in
 * raster order; a zero next_scale at the first position selects the default
 * matrix for that list. */
static void rh264_read_scaling_list(rh264_bits *b, uint8_t *out, int size,
      uint8_t *usedef)
{
   int last=8, next=8, j;
   *usedef=0;
   for(j=0;j<size;j++){
      int scanj=(size==16)?rh264_zigzag4[j]:rh264_zigzag8[j];
      if(next){
         int d=rh264_se(b);
         next=(last+d+256)&255;
         if(scanj==0&&next==0) *usedef=1;
      }
      out[scanj]=(uint8_t)(next?next:last);
      last=out[scanj];
   }
}
static int rh264_parse_sps(const uint8_t *rbsp,size_t size,rh264_sps *s){
   rh264_bits b; int i; memset(s,0,sizeof(*s)); rh264_bits_init(&b,rbsp,size);
   s->profile_idc=rh264_un(&b,8); s->constraint_flags=rh264_un(&b,8);
   s->level_idc=rh264_un(&b,8);
   { uint32_t id=rh264_ue(&b); if(id>31u) return 0; s->sps_id=(int)id; }
   s->chroma_format_idc=1; s->bit_depth_luma=8; s->bit_depth_chroma=8;
   if(s->profile_idc==100||s->profile_idc==110||s->profile_idc==122||s->profile_idc==244||
      s->profile_idc==44||s->profile_idc==83||s->profile_idc==86||s->profile_idc==118||
      s->profile_idc==128||s->profile_idc==138||s->profile_idc==139||s->profile_idc==134){
      s->chroma_format_idc=rh264_ue(&b);
      /* 4:4:4 with the three colour planes coded together (Cb and Cr
       * as luma-like planes, 7.4.2.1.1 ChromaArrayType 3); the
       * separate-plane form decodes three monochrome pictures and is
       * not carried.  Monochrome and high bit depth are refused: the
       * reconstruction pipeline is 8-bit and always has chroma. */
      if(s->chroma_format_idc==3){ if(rh264_u1(&b)) return 0; }
      if(s->chroma_format_idc<1||s->chroma_format_idc>3) return 0;
      s->bit_depth_luma   = (int)rh264_ue(&b) + 8;
      s->bit_depth_chroma = (int)rh264_ue(&b) + 8;
      /* 8..14 bits (7.4.2.1.1), luma and chroma at the same depth: the
       * planes share one sample width and one QpBdOffset domain */
      if(s->bit_depth_luma<8||s->bit_depth_luma>14) return 0;
      if(s->bit_depth_chroma!=s->bit_depth_luma) return 0;
      /* lossless: a macroblock at QP'Y == 0 carries its residual as
       * samples, no transform (8.5.15) */
      s->tb=rh264_u1(&b);
      if(rh264_u1(&b)){
         int nl=(s->chroma_format_idc==3)?12:8;
         s->scaling_present=1;
         for(i=0;i<nl;i++){
            s->sl_present[i]=(uint8_t)rh264_u1(&b);
            if(s->sl_present[i]){
               if(i<6)
                  rh264_read_scaling_list(&b,s->sl4[i],16,&s->sl_usedef[i]);
               else
                  rh264_read_scaling_list(&b,s->sl8[i-6],64,&s->sl_usedef[i]);
            }
         }
      }
   }
   /* Both minus4 fields are 0..12 (7.4.2.1.1), so the log2 values are
    * 4..16.  Unchecked they reach 1 << value and rh264_un's bit count
    * as anything up to 0xFFFFFFFF + 4. */
   { uint32_t lm4=rh264_ue(&b);
     if(lm4>12u) return 0;
     s->log2_max_frame_num=(int)lm4+4; }
   s->pic_order_cnt_type=rh264_ue(&b);
   if(s->pic_order_cnt_type==0)
   { uint32_t pm4=rh264_ue(&b);
     if(pm4>12u) return 0;
     s->log2_max_poc_lsb=(int)pm4+4; }
   else if(s->pic_order_cnt_type==1){ int n; s->poc_type1_always_zero=rh264_u1(&b);
      s->poc1_offset_non_ref=rh264_se(&b); s->poc1_offset_ttb=rh264_se(&b);
      n=rh264_ue(&b); if(n>255) return 0; s->poc1_ncycle=n;
      for(i=0;i<n;i++) s->poc1_offset_ref[i]=rh264_se(&b); }
   s->max_num_ref_frames=rh264_ue(&b); s->gaps_allowed=rh264_u1(&b);
   /* Geometry is attacker-controlled: rh264_ue can hand back anything up
    * to 0xFFFFFFFF, which lands in these ints as a huge or negative
    * count and reaches the frame allocator's size arithmetic.  Bound it
    * to 1024 macroblocks per side, 16384 pixels - above every level the
    * standard defines. */
   { uint32_t wm1=rh264_ue(&b), hm1=rh264_ue(&b);
     if(wm1>=1024||hm1>=1024) return 0;
     s->pic_width_in_mbs=(int)wm1+1; s->pic_height_in_map_units=(int)hm1+1; }
   s->frame_mbs_only_flag=rh264_u1(&b);
   if(!s->frame_mbs_only_flag)
      s->mb_adaptive_frame_field_flag=rh264_u1(&b);
   /* Macroblock-adaptive frame/field coding is accepted only so far as
    * the slice header check below allows: the macroblocks arrive in
    * vertical pairs, which changes their scan order, and a pair may be
    * field coded, which changes every neighbour derivation.  Only the
    * scan order is handled. */
   s->direct_8x8_inference_flag=rh264_u1(&b);
   { uint32_t cl=0,cr=0,ct=0,cb=0;
     int mbh=(2-s->frame_mbs_only_flag)*s->pic_height_in_map_units;
     /* Coded height in macroblock rows.  This is not always the cropped
      * height rounded up: a sequence that permits field coding always
      * codes an even number of rows, so cropping can hide a whole row.
      * Those macroblocks are still decoded and still take part in the
      * deblocking of the visible ones. */
     s->frame_mbs=mbh;
     if(rh264_u1(&b)){ s->frame_cropping=1;
                       cl=rh264_ue(&b); cr=rh264_ue(&b);
                       ct=rh264_ue(&b); cb=rh264_ue(&b); }
     /* Cropping is attacker-controlled too: keep each offset inside the
      * coded picture so the products below cannot overflow, then require
      * the cropped picture to be non-empty and no larger than the coded
      * one.  A frame_width or frame_height that came out negative was
      * cast to size_t by the frame allocator. */
     if(cl>16384u||cr>16384u||ct>16384u||cb>16384u) return 0;
     /* The crop offsets count chroma samples, so the step they scale by
      * is how many luma samples a chroma one spans (7.4.2.1.1).  That
      * is two across for both 4:2:0 and 4:2:2, but two DOWN only for
      * 4:2:0 - 4:2:2 keeps the luma height, so its vertical step is
      * one.  A field-capable sequence doubles the vertical step again. */
     { int sw=2, sh=(s->chroma_format_idc==1?2:1)*(2-s->frame_mbs_only_flag);
       int cw=s->pic_width_in_mbs*16, ch=mbh*16;
       int fw=cw-sw*(int)(cl+cr), fh=ch-sh*(int)(ct+cb);
       if(fw<16||fh<16||fw>cw||fh>ch) return 0;
       s->frame_width=fw; s->frame_height=fh;
       /* left/top offsets of the visible window in luma samples; the
        * offsets stay within the checks above so they cannot escape the
        * coded picture.  crop_x is always even (sw is 2) and crop_y is
        * even whenever chroma subsamples vertically (sh is then even),
        * so the chroma window origin below is exact. */
       s->crop_x=sw*(int)cl; s->crop_y=sh*(int)ct;
       s->crop_l=(int)cl; s->crop_r=(int)cr;
       s->crop_t=(int)ct; s->crop_b=(int)cb; } }
   /* VUI, walked only as far as max_num_reorder_frames (E.1.1), which bounds
    * how many decoded pictures can precede a given picture in output order
    * and so sets the display reorder delay. Absent VUI or an early end of
    * data leaves it unsignalled (-1). */
   s->vui_num_reorder=-1;
   if(rh264_u1(&b)){
      if(rh264_u1(&b)){ if(rh264_un(&b,8)==255){ rh264_un(&b,16); rh264_un(&b,16); } }
      if(rh264_u1(&b)) rh264_u1(&b);
      if(rh264_u1(&b)){ rh264_un(&b,3); rh264_u1(&b);
         if(rh264_u1(&b)){ rh264_un(&b,8); rh264_un(&b,8); rh264_un(&b,8); } }
      if(rh264_u1(&b)){ rh264_ue(&b); rh264_ue(&b); }
      if(rh264_u1(&b)){ rh264_un(&b,32); rh264_un(&b,32); rh264_u1(&b); }
      { int hrd0,hrd1,k;
        /* cpb_cnt_minus1 is 0..31 (E.2.2); an unbounded count spins on a
         * reader that has already run out of data, since a spent
         * rh264_ue consumes nothing. */
        hrd0=rh264_u1(&b);
        if(hrd0){ uint32_t cnt=rh264_ue(&b); if(cnt>31u) return 0;
           rh264_un(&b,4); rh264_un(&b,4);
           for(k=0;k<=(int)cnt;k++){ rh264_ue(&b); rh264_ue(&b); rh264_u1(&b); }
           rh264_un(&b,5); rh264_un(&b,5); rh264_un(&b,5); rh264_un(&b,5); }
        hrd1=rh264_u1(&b);
        if(hrd1){ uint32_t cnt=rh264_ue(&b); if(cnt>31u) return 0;
           rh264_un(&b,4); rh264_un(&b,4);
           for(k=0;k<=(int)cnt;k++){ rh264_ue(&b); rh264_ue(&b); rh264_u1(&b); }
           rh264_un(&b,5); rh264_un(&b,5); rh264_un(&b,5); rh264_un(&b,5); }
        if(hrd0||hrd1) rh264_u1(&b); }
      rh264_u1(&b);
      if(rh264_u1(&b)){
         rh264_u1(&b); rh264_ue(&b); rh264_ue(&b); rh264_ue(&b); rh264_ue(&b);
         if(rh264_more_data(&b)) s->vui_num_reorder=(int)rh264_ue(&b); }
   }
   s->valid=1; return 1;
}
static int rh264_parse_pps(const uint8_t *rbsp,size_t size,rh264_pps *p,
      int chroma444){
   rh264_bits b; memset(p,0,sizeof(*p)); rh264_bits_init(&b,rbsp,size);
   { uint32_t pid=rh264_ue(&b), sid=rh264_ue(&b);
     if(pid>255u||sid>31u) return 0;
     p->pps_id=(int)pid; p->sps_id=(int)sid; }
   p->entropy_coding_mode_flag=rh264_u1(&b);
   p->pic_order_present_flag=rh264_u1(&b);
   if(rh264_ue(&b)!=0) return 0;
   p->num_ref_idx_l0_default=rh264_ue(&b)+1;
   p->num_ref_idx_l1_default=rh264_ue(&b)+1;
   p->weighted_pred_flag=rh264_u1(&b);
   p->weighted_bipred_idc=(int)rh264_un(&b,2);
   /* pic_init_qp_minus26 is -(26 + 6*bit_depth_luma_minus8) .. +25
    * (7.4.2.2): pic_init_qp in -36..51 across the depths this decoder
    * carries (8..14 bits).  The PPS does not know its SPS's depth; the
    * slice header, which does, bounds SliceQPY to -QpBdOffsetY..51
    * before the value indexes anything. */
   { int32_t q = rh264_se(&b) + 26;
     if (q < -36 || q > 51) return 0;
     p->pic_init_qp = (int)q; }
   p->pic_init_qs=rh264_se(&b)+26; p->chroma_qp_index_offset=rh264_se(&b);
   p->deblocking_filter_control_present=rh264_u1(&b); p->constrained_intra_pred_flag=rh264_u1(&b);
   if(rh264_u1(&b)) return 0;   /* redundant_pic_cnt_present unsupported */
   p->chroma_qp_index_offset2=p->chroma_qp_index_offset;
   if(rh264_more_rbsp(&b)){
      p->transform_8x8_mode=rh264_u1(&b);
      if(rh264_u1(&b)){
         int i,n=6+(chroma444?6:2)*p->transform_8x8_mode;
         p->scaling_present=1;
         for(i=0;i<n;i++){
            p->sl_present[i]=(uint8_t)rh264_u1(&b);
            if(p->sl_present[i]){
               if(i<6)
                  rh264_read_scaling_list(&b,p->sl4[i],16,&p->sl_usedef[i]);
               else
                  rh264_read_scaling_list(&b,p->sl8[i-6],64,&p->sl_usedef[i]);
            }
         }
      }
      p->chroma_qp_index_offset2=rh264_se(&b);
   }
   p->valid=1; return 1;
}

/* Resolve the eight effective weight matrices for the active parameter sets
 * (fall-back rules A and B of 7.4.2.1.1 / 7.4.2.2, per JM CalculateQuantParam):
 * lists 0..5 are the 4x4 Intra/Inter Y,Cb,Cr weights, then the 8x8 Intra and
 * Inter luma weights. Flat 16 when neither parameter set carries matrices. */
static void rh264_resolve_scaling(const rh264_sps *s, const rh264_pps *p,
      uint8_t w4[6][16], uint8_t w8[6][64])
{
   /* 8x8 lists: 0 Intra Y, 1 Inter Y, and for 4:4:4 also 2 Intra Cb,
    * 3 Inter Cb, 4 Intra Cr, 5 Inter Cr; the chroma ones fall back to
    * the previous list of the same kind (rule A / B). */
   int i, k, n8 = (s->chroma_format_idc == 3) ? 6 : 2;
   if (!s->scaling_present && !p->scaling_present)
   {
      for (i = 0; i < 6; i++) for (k = 0; k < 16; k++) w4[i][k] = 16;
      for (i = 0; i < 6; i++) for (k = 0; k < 64; k++) w8[i][k] = 16;
      return;
   }
   for (i = 0; i < 6; i++) for (k = 0; k < 64; k++) w8[i][k] = 16;
   if (s->scaling_present)
   {
      for (i = 0; i < 6 + n8; i++)
      {
         if (i < 6)
         {
            const uint8_t *m;
            if (!s->sl_present[i])   /* fall-back rule A */
               m = (i == 0) ? rh264_def4_intra
                 : (i == 3) ? rh264_def4_inter : w4[i-1];
            else if (s->sl_usedef[i])
               m = (i < 3) ? rh264_def4_intra : rh264_def4_inter;
            else
               m = s->sl4[i];
            memcpy(w4[i], m, 16);
         }
         else
         {
            const uint8_t *m;
            if (!s->sl_present[i])   /* fall-back rule A */
               m = (i == 6) ? rh264_def8_intra : (i == 7) ? rh264_def8_inter
                 : w8[i-8];
            else if (s->sl_usedef[i])
               m = (i & 1) ? rh264_def8_inter : rh264_def8_intra;
            else
               m = s->sl8[i-6];
            if (m != w8[i-6]) memcpy(w8[i-6], m, 64);
         }
      }
   }
   if (p->scaling_present)
   {
      for (i = 0; i < 6 + n8; i++)
      {
         if (i < 6)
         {
            const uint8_t *m;
            if (!p->sl_present[i])   /* fall-back rule B */
            {
               if (i == 0 || i == 3)
                  m = s->scaling_present ? w4[i]
                    : (i == 0) ? rh264_def4_intra : rh264_def4_inter;
               else
                  m = w4[i-1];
            }
            else if (p->sl_usedef[i])
               m = (i < 3) ? rh264_def4_intra : rh264_def4_inter;
            else
               m = p->sl4[i];
            if (m != w4[i]) memcpy(w4[i], m, 16);
         }
         else if (p->transform_8x8_mode)
         {
            const uint8_t *m;
            if (!p->sl_present[i])   /* fall-back rule B */
            {
               if (i == 6 || i == 7)
                  m = s->scaling_present ? w8[i-6]
                    : (i == 6) ? rh264_def8_intra : rh264_def8_inter;
               else
                  m = w8[i-8];
            }
            else if (p->sl_usedef[i])
               m = (i & 1) ? rh264_def8_inter : rh264_def8_intra;
            else
               m = p->sl8[i-6];
            if (m != w8[i-6]) memcpy(w8[i-6], m, 64);
         }
         else if (!s->scaling_present)
            memset(w8[i-6], 16, 64);
      }
   }
}


/* ==================== rh264_slice.h ==================== */
enum { RH264_SLICE_P=0,RH264_SLICE_B=1,RH264_SLICE_I=2,RH264_SLICE_SP=3,RH264_SLICE_SI=4 };
#define RH264_MAX_REFS 16
#define RH264_OUT_SLOTS (RH264_MAX_REFS+2)
typedef struct { int first_mb_in_slice,slice_type,pic_parameter_set_id,frame_num,
   idr_pic_id,poc_lsb,slice_qp,disable_deblocking_filter_idc,is_idr,
   field_pic_flag,bottom_field_flag,switching,
   poc1_delta0,poc1_delta1,
   num_ref_idx_l0,num_ref_idx_l1,direct_spatial_mv_pred_flag,
   cabac_init_idc,frame_num_val,
   nmod,nmod1,wp_valid,luma_log2_denom,chroma_log2_denom,
   slice_alpha_c0_offset,slice_beta_offset,n_mmco,idr_ltr,
   poc_bottom_delta;
   uint8_t mmco_op[RH264_MAX_REFS*2];
   int32_t mmco_a[RH264_MAX_REFS*2], mmco_b[RH264_MAX_REFS*2];
   int8_t mod_op[34],mod_op1[34]; int32_t mod_val[34],mod_val1[34];
   int16_t wp_lw[32],wp_lo[32],wp_cw[32][2],wp_co[32][2];
   int16_t wp1_lw[32],wp1_lo[32],wp1_cw[32][2],wp1_co[32][2]; } rh264_slice_hdr;
/* advancing variant: parses from an existing reader b (leaves it at slice data) */
static int rh264_parse_slice_header_adv(rh264_bits *b,int nal_unit_type,int nal_ref_idc,
      const rh264_sps *sps,const rh264_pps *pps,rh264_slice_hdr *sh){
   int st; memset(sh,0,sizeof(*sh)); sh->is_idr=(nal_unit_type==5);
   sh->first_mb_in_slice=rh264_ue(b);
   st=rh264_ue(b); sh->slice_type=st%5;
   /* Only intra 4:2:2 with CAVLC reconstructs: the CABAC residual
    * layer needs its own block categories for the extra chroma blocks,
    * and inter prediction needs chroma motion compensation that does
    * not halve the vertical vector.  Refusing here keeps a wrongly
    * reconstructed picture off the screen. */

   sh->pic_parameter_set_id=rh264_ue(b);
   sh->frame_num=rh264_un(b,sps->log2_max_frame_num);
   sh->frame_num_val=sh->frame_num;
   if(!sps->frame_mbs_only_flag)
   {
      sh->field_pic_flag=rh264_u1(b);
      if(sh->field_pic_flag) sh->bottom_field_flag=rh264_u1(b);
      /* In a macroblock-adaptive frame picture first_mb_in_slice counts
       * macroblock PAIRS, so the address of the slice's first
       * macroblock is twice it (7.3.3).  Everything downstream wants
       * the address. */
      if(!sh->field_pic_flag&&sps->mb_adaptive_frame_field_flag)
         sh->first_mb_in_slice*=2;

   }
   if(sh->is_idr) sh->idr_pic_id=rh264_ue(b);
   if(sps->pic_order_cnt_type==0){
      sh->poc_lsb=rh264_un(b,sps->log2_max_poc_lsb);
      if(pps->pic_order_present_flag&&!sh->field_pic_flag)
         sh->poc_bottom_delta=rh264_se(b);
   } else if(sps->pic_order_cnt_type==1&&!sps->poc_type1_always_zero){
      sh->poc1_delta0=rh264_se(b);
      if(pps->pic_order_present_flag) sh->poc1_delta1=rh264_se(b);
   }
   sh->num_ref_idx_l0=1; sh->num_ref_idx_l1=1;
   if(sh->slice_type==RH264_SLICE_B)
      sh->direct_spatial_mv_pred_flag=rh264_u1(b);
   /* Temporal direct prediction scales motion by picture order count
    * distances that are derived differently for fields; only the
    * spatial mode is handled for field pictures. */

   if(sh->slice_type==RH264_SLICE_P||sh->slice_type==RH264_SLICE_SP
         ||sh->slice_type==RH264_SLICE_B){
      /* num_ref_idx_active_override_flag + ref_pic_list_modification (7.3.3). */
      sh->num_ref_idx_l0=pps->num_ref_idx_l0_default;
      sh->num_ref_idx_l1=pps->num_ref_idx_l1_default;
      if(sh->num_ref_idx_l0<1) sh->num_ref_idx_l0=1;
      if(sh->num_ref_idx_l1<1) sh->num_ref_idx_l1=1;
      if(rh264_u1(b)){
         sh->num_ref_idx_l0=rh264_ue(b)+1;
         if(sh->slice_type==RH264_SLICE_B) sh->num_ref_idx_l1=rh264_ue(b)+1;
      }
      if(rh264_u1(b)){ /* ref_pic_list_modification_flag_l0 (7.3.3.1) */
         int op; do{ op=rh264_ue(b);
            if(op==0||op==1||op==2){
               int v=rh264_ue(b);
               if(sh->nmod<34){ sh->mod_op[sh->nmod]=(int8_t)op;
                                sh->mod_val[sh->nmod]=v; sh->nmod++; }
            }
         } while(op!=3&&rh264_more_data(b));
      }
      if(sh->slice_type==RH264_SLICE_B&&rh264_u1(b)){
         int op; do{ op=rh264_ue(b);
            if(op==0||op==1||op==2){
               int v=rh264_ue(b);
               if(sh->nmod1<34){ sh->mod_op1[sh->nmod1]=(int8_t)op;
                                 sh->mod_val1[sh->nmod1]=v; sh->nmod1++; }
            }
         } while(op!=3&&rh264_more_data(b));
      }
   }
   /* Switching slices are refused.  They carry the same header as a
    * predicted slice, so the branch above accepts SP without noticing,
    * but their residual is reconstructed through a transform and
    * quantisation of their own (8.5.13) - decoded as if predicted they
    * come out wrong rather than failing. */
   if(sh->slice_type==RH264_SLICE_SP||sh->slice_type==RH264_SLICE_SI)
   { sh->switching=1; return 0; }
   /* pred_weight_table (7.3.3.2). Entries left unsignalled keep the default
    * of unit weight and zero offset. */
   if((pps->weighted_pred_flag
         &&(sh->slice_type==RH264_SLICE_P||sh->slice_type==RH264_SLICE_SP))
      ||(pps->weighted_bipred_idc==1&&sh->slice_type==RH264_SLICE_B)){
      int i,j;
      sh->luma_log2_denom=rh264_ue(b);
      sh->chroma_log2_denom=rh264_ue(b);
      /* 7.4.3.2 bounds both denominators to 0..7; a corrupt stream can
       * signal anything, and the unit-weight defaults below shift by
       * the value read.  Compare as unsigned: rh264_ue saturates to
       * 0xFFFFFFFF on an over-long code, which lands in these ints as
       * -1 and slips past a signed upper-bound test, leaving the shifts
       * below - and the ones in the weighting itself - with a negative
       * exponent. */
      if((unsigned)sh->luma_log2_denom>7||(unsigned)sh->chroma_log2_denom>7)
         return 0;
      for(i=0;i<32;i++){
         sh->wp_lw[i]=(int16_t)(1<<sh->luma_log2_denom); sh->wp_lo[i]=0;
         sh->wp1_lw[i]=(int16_t)(1<<sh->luma_log2_denom); sh->wp1_lo[i]=0;
         for(j=0;j<2;j++){ sh->wp_cw[i][j]=(int16_t)(1<<sh->chroma_log2_denom);
                           sh->wp_co[i][j]=0;
                           sh->wp1_cw[i][j]=(int16_t)(1<<sh->chroma_log2_denom);
                           sh->wp1_co[i][j]=0; } }
      for(i=0;i<sh->num_ref_idx_l0&&i<32;i++){
         if(rh264_u1(b)){ sh->wp_lw[i]=(int16_t)rh264_se(b);
                          sh->wp_lo[i]=(int16_t)rh264_se(b); }
         if(rh264_u1(b)) for(j=0;j<2;j++){
            sh->wp_cw[i][j]=(int16_t)rh264_se(b);
            sh->wp_co[i][j]=(int16_t)rh264_se(b); }
      }
      if(sh->slice_type==RH264_SLICE_B)
         for(i=0;i<sh->num_ref_idx_l1&&i<32;i++){
            if(rh264_u1(b)){ sh->wp1_lw[i]=(int16_t)rh264_se(b);
                             sh->wp1_lo[i]=(int16_t)rh264_se(b); }
            if(rh264_u1(b)) for(j=0;j<2;j++){
               sh->wp1_cw[i][j]=(int16_t)rh264_se(b);
               sh->wp1_co[i][j]=(int16_t)rh264_se(b); }
         }
      sh->wp_valid=1;
   }
   /* dec_ref_pic_marking (7.3.3.3): only when nal_ref_idc != 0. */
   if(nal_ref_idc){
      if(sh->is_idr){
         rh264_u1(b);                    /* no_output_of_prior_pics_flag */
         sh->idr_ltr=rh264_u1(b);        /* long_term_reference_flag     */
      }
      else {
         if(rh264_u1(b)){ /* adaptive_ref_pic_marking_mode_flag (7.3.3.3) */
            int op; do{ op=rh264_ue(b);
               if(op>=1&&op<=6){
                  int a=0,b2=0;
                  if(op==1||op==2||op==3||op==4||op==6) a=rh264_ue(b);
                  if(op==3) b2=rh264_ue(b);
                  if(sh->n_mmco<RH264_MAX_REFS*2){
                     sh->mmco_op[sh->n_mmco]=(uint8_t)op;
                     sh->mmco_a[sh->n_mmco]=a;
                     sh->mmco_b[sh->n_mmco]=b2;
                     sh->n_mmco++;
                  }
                  else return 0;
               }
               else if(op!=0) return 0;
            } while(op!=0&&rh264_more_data(b));
         }
      }
   }
   if(pps->entropy_coding_mode_flag&&sh->slice_type!=RH264_SLICE_I
         &&sh->slice_type!=RH264_SLICE_SI)
      sh->cabac_init_idc=rh264_ue(b);
   sh->slice_qp=pps->pic_init_qp+rh264_se(b);
   /* SliceQPY is -QpBdOffsetY..51 (7.4.3): 0..51 at 8 bits, down to
    * -6 * (bit_depth - 8) above.  The value feeds f->qp (QPY); the
    * dequantiser adds QpBdOffsetY back, and the CABAC initialisation
    * clips to 0..51.  Refuse the slice rather than let a corrupt delta
    * drive those out of range. */
   if(sh->slice_qp < -6*(sps->bit_depth_luma-8) || sh->slice_qp>51) return 0;
   if(pps->deblocking_filter_control_present){
      sh->disable_deblocking_filter_idc=rh264_ue(b);
      if(sh->disable_deblocking_filter_idc!=1){
         sh->slice_alpha_c0_offset=rh264_se(b)*2;
         sh->slice_beta_offset=rh264_se(b)*2; }
   }
   return 1;
}


/* ==================== rh264_xform.h ==================== */
#ifdef RH264_SSE2
/* 32x32->32 low multiply; _mm_mullo_epi32 is SSE4.1, so combine the two
 * odd/even 32x32->64 products SSE2 does have. */
static __inline __m128i rh264_sse2_mullo32(__m128i a,__m128i b){
   /* even lanes (0,2) from the direct product, odd lanes (1,3) from the
    * shifted one; each 64-bit result keeps its low half, and the two
    * are interleaved so lane order is preserved */
   __m128i e=_mm_mul_epu32(a,b);
   __m128i o=_mm_mul_epu32(_mm_srli_si128(a,4),_mm_srli_si128(b,4));
   return _mm_unpacklo_epi32(
         _mm_shuffle_epi32(e,_MM_SHUFFLE(0,0,2,0)),
         _mm_shuffle_epi32(o,_MM_SHUFFLE(0,0,2,0)));
}
#define RH264_SSE2_TR4_32(a,b,c,d) do { \
   __m128i t0=_mm_unpacklo_epi32((a),(b)), t1=_mm_unpackhi_epi32((a),(b)); \
   __m128i t2=_mm_unpacklo_epi32((c),(d)), t3=_mm_unpackhi_epi32((c),(d)); \
   (a)=_mm_unpacklo_epi64(t0,t2); (b)=_mm_unpackhi_epi64(t0,t2); \
   (c)=_mm_unpacklo_epi64(t1,t3); (d)=_mm_unpackhi_epi64(t1,t3); \
} while (0)
#elif defined(RH264_NEON)
#define RH264_NEON_TR4_32(a,b,c,d) do { \
   int32x4x2_t p0=vtrnq_s32((a),(b)), p1=vtrnq_s32((c),(d)); \
   (a)=vcombine_s32(vget_low_s32(p0.val[0]),vget_low_s32(p1.val[0])); \
   (b)=vcombine_s32(vget_low_s32(p0.val[1]),vget_low_s32(p1.val[1])); \
   (c)=vcombine_s32(vget_high_s32(p0.val[0]),vget_high_s32(p1.val[0])); \
   (d)=vcombine_s32(vget_high_s32(p0.val[1]),vget_high_s32(p1.val[1])); \
} while (0)
#endif
static const int rh264_dequant4_v[6][3]={{10,16,13},{11,18,14},{13,20,16},{14,23,18},{16,25,20},{18,29,23}};
static const uint8_t rh264_dequant4_idx[16]={0,2,0,2,2,1,2,1,0,2,0,2,2,1,2,1};
/* The per-coefficient scale w[i] * LevelScale(qP%6, i) depends only on
 * the weight matrix and qP, both fixed for a whole block, so the two
 * table lookups and the multiply that built it per coefficient are
 * hoisted into one 16-entry vector.  The per/shift decision is likewise
 * invariant across the block: taking it once turns the body into a
 * straight multiply-and-shift over sixteen lanes.  has_dc_sep leaves
 * blk[0] untouched (the DC arrives already scaled from the Hadamard
 * path); it is restored after the vector pass rather than tested per
 * lane. */
static void rh264_dequant4x4(int32_t *blk,int qP,int has_dc_sep,
      const uint8_t *w){
   int i,per=qP/6,rem=qP%6;
   int32_t sc[16];
   int32_t dc = blk[0];
   for(i=0;i<16;i++)
      sc[i]=(int32_t)w[i]*rh264_dequant4_v[rem][rh264_dequant4_idx[i]];
   if(per>=4){
      int sh=per-4;
#ifdef RH264_SSE2
      for(i=0;i<16;i+=4){
         __m128i b=_mm_loadu_si128((const __m128i*)(blk+i));
         __m128i m=_mm_loadu_si128((const __m128i*)(sc+i));
         _mm_storeu_si128((__m128i*)(blk+i),
               _mm_sll_epi32(rh264_sse2_mullo32(b,m),_mm_cvtsi32_si128(sh)));
      }
#elif defined(RH264_NEON)
      for(i=0;i<16;i+=4)
         vst1q_s32(blk+i,vshlq_s32(vmulq_s32(vld1q_s32(blk+i),
               vld1q_s32(sc+i)),vdupq_n_s32(sh)));
#else
      for(i=0;i<16;i++)
         blk[i]=(int32_t)((uint32_t)(blk[i]*sc[i])<<sh);
#endif
   }else{
      int sh=4-per;
      int32_t rnd=1<<(3-per);
#ifdef RH264_SSE2
      __m128i vr=_mm_set1_epi32(rnd),vs=_mm_cvtsi32_si128(sh);
      for(i=0;i<16;i+=4){
         __m128i b=_mm_loadu_si128((const __m128i*)(blk+i));
         __m128i m=_mm_loadu_si128((const __m128i*)(sc+i));
         _mm_storeu_si128((__m128i*)(blk+i),
               _mm_sra_epi32(_mm_add_epi32(rh264_sse2_mullo32(b,m),vr),vs));
      }
#elif defined(RH264_NEON)
      for(i=0;i<16;i+=4)
         vst1q_s32(blk+i,vshlq_s32(vaddq_s32(vmulq_s32(vld1q_s32(blk+i),
               vld1q_s32(sc+i)),vdupq_n_s32(rnd)),vdupq_n_s32(-sh)));
#else
      for(i=0;i<16;i++)
         blk[i]=(blk[i]*sc[i]+rnd)>>sh;
#endif
   }
   if(has_dc_sep) blk[0]=dc;
}
/* The 4x4 scale of rh264_dequant4x4 as one multiplier a raster position
 * for dequantising coefficient by coefficient, (c * qm[i] + 32) >> 6:
 * the scale times 2^(per + 2).  For qP / 6 >= 4 that is c * scale <<
 * (per - 4) exactly, the low six bits of the product being zero; below,
 * (c * scale + 2^(3 - per)) >> (4 - per) with numerator and divisor
 * scaled alike.  At most 7395 << 16, so it fits. */
static void rh264_qmul4(int32_t *qm, int qP, const uint8_t *w)
{
   int i, per = qP / 6, rem = qP % 6;
   for (i = 0; i < 16; i++)
      qm[i] = (int32_t)((uint32_t)((int32_t)w[i]
            * rh264_dequant4_v[rem][rh264_dequant4_idx[i]]) << (per + 2));
}
/* 8.5.12.2 residual inverse transform.  The row pass runs four rows in
 * parallel over transposed data, then one transpose feeds the identical
 * column pass, so the same butterfly serves both directions. */
static void rh264_itransform4x4(const int32_t *d,int32_t *r){
#ifdef RH264_SSE2
   __m128i s0=_mm_loadu_si128((const __m128i*)(d+0));
   __m128i s1=_mm_loadu_si128((const __m128i*)(d+4));
   __m128i s2=_mm_loadu_si128((const __m128i*)(d+8));
   __m128i s3=_mm_loadu_si128((const __m128i*)(d+12));
   int pass;
   for(pass=0;pass<2;pass++){
      /* transpose first so the butterfly runs down lanes: pass 0 then
       * transforms the rows, pass 1 the columns of the result, and the
       * vectors are left holding rows ready to store */
      __m128i z0,z1,z2,z3;
      RH264_SSE2_TR4_32(s0,s1,s2,s3);
      z0=_mm_add_epi32(s0,s2);
      z1=_mm_sub_epi32(s0,s2);
      z2=_mm_sub_epi32(_mm_srai_epi32(s1,1),s3);
      z3=_mm_add_epi32(s1,_mm_srai_epi32(s3,1));
      s0=_mm_add_epi32(z0,z3); s1=_mm_add_epi32(z1,z2);
      s2=_mm_sub_epi32(z1,z2); s3=_mm_sub_epi32(z0,z3);
   }
   _mm_storeu_si128((__m128i*)(r+0),s0);
   _mm_storeu_si128((__m128i*)(r+4),s1);
   _mm_storeu_si128((__m128i*)(r+8),s2);
   _mm_storeu_si128((__m128i*)(r+12),s3);
#elif defined(RH264_NEON)
   int32x4_t s0=vld1q_s32(d+0),s1=vld1q_s32(d+4);
   int32x4_t s2=vld1q_s32(d+8),s3=vld1q_s32(d+12);
   int pass;
   for(pass=0;pass<2;pass++){
      int32x4_t z0,z1,z2,z3;
      RH264_NEON_TR4_32(s0,s1,s2,s3);
      z0=vaddq_s32(s0,s2);
      z1=vsubq_s32(s0,s2);
      z2=vsubq_s32(vshrq_n_s32(s1,1),s3);
      z3=vaddq_s32(s1,vshrq_n_s32(s3,1));
      s0=vaddq_s32(z0,z3); s1=vaddq_s32(z1,z2);
      s2=vsubq_s32(z1,z2); s3=vsubq_s32(z0,z3);
   }
   vst1q_s32(r+0,s0); vst1q_s32(r+4,s1);
   vst1q_s32(r+8,s2); vst1q_s32(r+12,s3);
#else
   int32_t e[16]; int i;
   for(i=0;i<4;i++){ const int32_t*s=d+i*4;
      int32_t z0=s[0]+s[2],z1=s[0]-s[2],z2=(s[1]>>1)-s[3],z3=s[1]+(s[3]>>1);
      e[i*4+0]=z0+z3;e[i*4+1]=z1+z2;e[i*4+2]=z1-z2;e[i*4+3]=z0-z3; }
   for(i=0;i<4;i++){ int32_t z0=e[0*4+i]+e[2*4+i],z1=e[0*4+i]-e[2*4+i],z2=(e[1*4+i]>>1)-e[3*4+i],z3=e[1*4+i]+(e[3*4+i]>>1);
      r[0*4+i]=z0+z3;r[1*4+i]=z1+z2;r[2*4+i]=z1-z2;r[3*4+i]=z0-z3; }
#endif
}
static void rh264_ihadamard4x4(const int32_t *in,int32_t *out){
   int32_t e[16]; int i;
   for(i=0;i<4;i++){ const int32_t*s=in+i*4;
      int32_t z0=s[0]+s[2],z1=s[0]-s[2],z2=s[1]-s[3],z3=s[1]+s[3];
      e[i*4+0]=z0+z3;e[i*4+1]=z1+z2;e[i*4+2]=z1-z2;e[i*4+3]=z0-z3; }
   for(i=0;i<4;i++){ int32_t z0=e[0*4+i]+e[2*4+i],z1=e[0*4+i]-e[2*4+i],z2=e[1*4+i]-e[3*4+i],z3=e[1*4+i]+e[3*4+i];
      out[0*4+i]=z0+z3;out[1*4+i]=z1+z2;out[2*4+i]=z1-z2;out[3*4+i]=z0-z3; }
}


/* ==================== rh264_cavlc_ct.h ==================== */
/* CAVLC VLC tables extracted from openh264 WelsEnc forward tables,
 * verified prefix-free. All {len,code}. */
static const uint8_t rh264_ct_len[4][17][4]={
 {{1,0,0,0},{6,2,0,0},{8,6,3,0},{9,8,7,5},{10,9,8,6},{11,10,9,7},{13,11,10,8},{13,13,11,9},{13,13,13,10},{14,14,13,11},{14,14,14,13},{15,15,14,14},{15,15,15,14},{16,15,15,15},{16,16,16,15},{16,16,16,16},{16,16,16,16},},
 {{2,0,0,0},{6,2,0,0},{6,5,3,0},{7,6,6,4},{8,6,6,4},{8,7,7,5},{9,8,8,6},{11,9,9,6},{11,11,11,7},{12,11,11,9},{12,12,12,11},{12,12,12,11},{13,13,13,12},{13,13,13,13},{13,14,13,13},{14,14,14,13},{14,14,14,14},},
 {{4,0,0,0},{6,4,0,0},{6,5,4,0},{6,5,5,4},{7,5,5,4},{7,5,5,4},{7,6,6,4},{7,6,6,4},{8,7,7,5},{8,8,7,6},{9,8,8,7},{9,9,8,8},{9,9,9,8},{10,9,9,9},{10,10,10,10},{10,10,10,10},{10,10,10,10},},
 {{6,0,0,0},{6,6,0,0},{6,6,6,0},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},{6,6,6,6},},
};
static const uint8_t rh264_ct_code[4][17][4]={
 {{1,0,0,0},{5,1,0,0},{7,4,1,0},{7,6,5,3},{7,6,5,3},{7,6,5,4},{15,6,5,4},{11,14,5,4},{8,10,13,4},{15,14,9,4},{11,10,13,12},{15,14,9,12},{11,10,13,8},{15,1,9,12},{11,14,13,8},{7,10,9,12},{4,6,5,8},},
 {{3,0,0,0},{11,2,0,0},{7,7,3,0},{7,10,9,5},{7,6,5,4},{4,6,5,6},{7,6,5,8},{15,6,5,4},{11,14,13,4},{15,10,9,4},{11,14,13,12},{8,10,9,8},{15,14,13,12},{11,10,9,12},{7,11,6,8},{9,8,10,1},{7,6,5,4},},
 {{15,0,0,0},{15,14,0,0},{11,15,13,0},{8,12,14,12},{15,10,11,11},{11,8,9,10},{9,14,13,9},{8,10,9,8},{15,14,13,13},{11,14,10,12},{15,10,13,12},{11,14,9,12},{8,10,13,8},{13,7,9,12},{9,12,11,10},{5,8,7,6},{1,4,3,2},},
 {{3,0,0,0},{0,1,0,0},{4,5,6,0},{8,9,10,11},{12,13,14,15},{16,17,18,19},{20,21,22,23},{24,25,26,27},{28,29,30,31},{32,33,34,35},{36,37,38,39},{40,41,42,43},{44,45,46,47},{48,49,50,51},{52,53,54,55},{56,57,58,59},{60,61,62,63},},
};
static const uint8_t rh264_ctc_len[17][4]={
 {2,0,0,0},
 {6,1,0,0},
 {6,6,3,0},
 {6,7,7,6},
 {6,8,8,7},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
};
static const uint8_t rh264_ctc_code[17][4]={
 {1,0,0,0},
 {7,1,0,0},
 {4,6,1,0},
 {3,3,2,5},
 {2,3,2,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
 {0,0,0,0},
};
static const uint8_t rh264_tz_len[16][16]={
 {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9},
 {3,3,3,3,3,4,4,4,4,5,5,6,6,6,6,0},
 {4,3,3,3,4,4,3,3,4,5,5,6,5,6,0,0},
 {5,3,4,4,3,3,3,4,3,4,5,5,5,0,0,0},
 {4,4,4,3,3,3,3,3,4,5,4,5,0,0,0,0},
 {6,5,3,3,3,3,3,3,4,3,6,0,0,0,0,0},
 {6,5,3,3,3,2,3,4,3,6,0,0,0,0,0,0},
 {6,4,5,3,2,2,3,3,6,0,0,0,0,0,0,0},
 {6,6,4,2,2,3,2,5,0,0,0,0,0,0,0,0},
 {5,5,3,2,2,2,4,0,0,0,0,0,0,0,0,0},
 {4,4,3,3,1,3,0,0,0,0,0,0,0,0,0,0},
 {4,4,2,1,3,0,0,0,0,0,0,0,0,0,0,0},
 {3,3,1,2,0,0,0,0,0,0,0,0,0,0,0,0},
 {2,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};
static const uint8_t rh264_tz_code[16][16]={
 {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1},
 {7,6,5,4,3,5,4,3,2,3,2,3,2,1,0,0},
 {5,7,6,5,4,3,4,3,2,3,2,1,1,0,0,0},
 {3,7,5,4,6,5,4,3,3,2,2,1,0,0,0,0},
 {5,4,3,7,6,5,4,3,2,1,1,0,0,0,0,0},
 {1,1,7,6,5,4,3,2,1,1,0,0,0,0,0,0},
 {1,1,5,4,3,3,2,1,1,0,0,0,0,0,0,0},
 {1,1,1,3,3,2,2,1,0,0,0,0,0,0,0,0},
 {1,0,1,3,2,1,1,1,0,0,0,0,0,0,0,0},
 {1,0,1,3,2,1,1,0,0,0,0,0,0,0,0,0},
 {0,1,1,2,1,3,0,0,0,0,0,0,0,0,0,0},
 {0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0},
 {0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
 {0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};
static const uint8_t rh264_rb_len[8][15]={
 {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {1,2,2,0,0,0,0,0,0,0,0,0,0,0,0},
 {2,2,2,2,0,0,0,0,0,0,0,0,0,0,0},
 {2,2,2,3,3,0,0,0,0,0,0,0,0,0,0},
 {2,2,3,3,3,3,0,0,0,0,0,0,0,0,0},
 {2,3,3,3,3,3,3,0,0,0,0,0,0,0,0},
 {3,3,3,3,3,3,3,4,5,6,7,8,9,10,11},
};
static const uint8_t rh264_rb_code[8][15]={
 {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {3,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
 {3,2,1,1,0,0,0,0,0,0,0,0,0,0,0},
 {3,2,3,2,1,0,0,0,0,0,0,0,0,0,0},
 {3,0,1,3,2,5,4,0,0,0,0,0,0,0,0},
 {7,6,5,4,3,2,1,1,1,1,1,1,1,1,1},
};
/* coeff_token for the chroma DC block of a 4:2:2 macroblock (nC == -2,
 * eight coefficients).  Indexed [trailing_ones][total_coeff]. */
static const uint8_t rh264_ctc422_len[4][9]={
   { 1, 7, 7, 9, 9,10,11,12,13},
   { 0, 2, 7, 7, 9,10,11,12,12},
   { 0, 0, 3, 7, 7, 9,10,11,12},
   { 0, 0, 0, 5, 6, 7, 7,10,11}
};
static const uint8_t rh264_ctc422_code[4][9]={
   { 1,15,14, 7, 6, 7, 7, 7, 7},
   { 0, 1,13,12, 5, 6, 6, 6, 5},
   { 0, 0, 1,11,10, 4, 5, 5, 4},
   { 0, 0, 0, 1, 1, 9, 8, 4, 4}
};
/* total_zeros for the same block, indexed [total_coeff][zeros]. */
static const uint8_t rh264_cdctz422_len[8][8]={
   { 0},
   { 1, 3, 3, 4, 4, 4, 5, 5},
   { 3, 2, 3, 3, 3, 3, 3, 0},
   { 3, 3, 2, 2, 3, 3, 0, 0},
   { 3, 2, 2, 2, 3, 0, 0, 0},
   { 2, 2, 2, 2, 0, 0, 0, 0},
   { 2, 2, 1, 0, 0, 0, 0, 0},
   { 1, 1, 0, 0, 0, 0, 0, 0}
};
static const uint8_t rh264_cdctz422_code[8][8]={
   { 0},
   { 1, 2, 3, 2, 3, 1, 1, 0},
   { 0, 1, 1, 4, 5, 6, 7, 0},
   { 0, 1, 1, 2, 6, 7, 0, 0},
   { 6, 0, 1, 2, 7, 0, 0, 0},
   { 0, 1, 2, 3, 0, 0, 0, 0},
   { 0, 1, 1, 0, 0, 0, 0, 0},
   { 0, 1, 0, 0, 0, 0, 0, 0}
};

static const uint8_t rh264_cdctz_len[4][4]={
 {0,0,0,0},
 {1,2,3,3},
 {1,2,2,0},
 {1,1,0,0},
};
static const uint8_t rh264_cdctz_code[4][4]={
 {0,0,0,0},
 {1,1,1,0},
 {1,1,0,0},
 {1,0,0,0},
};


/* ==================== rh264_cavlc.h ==================== */
/* rh264 -- CAVLC residual block decode (H.264 clause 9.2), using tables
 * extracted+verified from openh264. Decodes one block's coefficients into
 * coeffLevel[maxNumCoeff] in zig-zag order (caller applies inverse scan). */


/* Match a prefix-free VLC: read bits MSB-first until (len,code) hits a
 * table row whose value index is returned. tab_len/tab_code are [N]; the
 * decoded symbol is the matching index. Returns -1 on failure. */
/* ---- inverse (lookup) form of the VLC tables ----
 * The len/code arrays above stay the single source of truth: when a
 * decoder is opened they are inverted into direct-indexed tables,
 * tab[peek] -> (symbol << 5) | code_length, 0 marking an invalid
 * prefix.  A code of length L occupies the 1 << (bits - L) slots
 * sharing its prefix, which is exactly the prefix-free property the
 * tables were verified for.  Lookup is then one peek and one load
 * where the serial matcher rescanned the candidate list after every
 * appended bit.  The set lives in the decoder instance - no globals,
 * so concurrent instances never share state - and costs ~190 KiB,
 * small next to the plane buffers.  Should the build fail, every site
 * falls back to the serial matcher, so behaviour is unchanged, only
 * slower. */
typedef struct { uint16_t *tab; int bits; } rh264_vlc;
struct rh264_vlc_set
{
   rh264_vlc ct[4];        /* coeff_token by nC class      */
   rh264_vlc ctc;          /* chroma-DC coeff_token, 4:2:0 */
   rh264_vlc ctc422;       /* chroma-DC coeff_token, 4:2:2 */
   rh264_vlc tz[16];       /* total_zeros by total_coeff   */
   rh264_vlc cdctz[4];     /* chroma-DC total_zeros, 4:2:0 */
   rh264_vlc cdctz422[8];  /* chroma-DC total_zeros, 4:2:2 */
   rh264_vlc rb[8];        /* run_before by zeros bucket   */
};

static int rh264_vlc_put(rh264_vlc *v, uint32_t code, int len, int idx)
{
   uint32_t base = code << (v->bits - len);
   uint32_t span = 1u << (v->bits - len), j;
   uint16_t e = (uint16_t)(((unsigned)idx << 5) | (unsigned)len);
   for (j = 0; j < span; j++)
   {
      if (v->tab[base + j]) return -1;   /* prefix collision: refuse */
      v->tab[base + j] = e;
   }
   return 0;
}

static int rh264_vlc_alloc(rh264_vlc *v, int bits)
{
   v->bits = bits;
   v->tab  = (uint16_t*)calloc((size_t)1 << bits, sizeof(uint16_t));
   return v->tab ? 0 : -1;
}

/* Invert one [n]-entry len/code row pair (the rh264_vlc_match1 shape). */
static int rh264_vlc_build_row(rh264_vlc *v, const uint8_t *len_row,
      const uint8_t *code_row, int n)
{
   int i, maxl = 0;
   for (i = 0; i < n; i++)
      if (len_row[i] > maxl) maxl = len_row[i];
   if (!maxl) { v->tab = NULL; v->bits = 0; return 0; }   /* empty row */
   if (rh264_vlc_alloc(v, maxl) != 0) return -1;
   for (i = 0; i < n; i++)
      if (len_row[i])
         if (rh264_vlc_put(v, code_row[i], len_row[i], i) != 0) return -1;
   return 0;
}

/* Invert one coeff_token grid: symbol = total_coeff * 4 + trailing_ones,
 * walking the same (tc, t1) domain the serial matcher searches. */
static int rh264_vlc_build_ct(rh264_vlc *v,
      const uint8_t len[17][4], const uint8_t code[17][4], int ntc)
{
   int tc, t1, maxl = 0;
   for (tc = 0; tc < ntc; tc++)
      for (t1 = 0; t1 <= (tc < 3 ? tc : 3); t1++)
         if (len[tc][t1] > maxl) maxl = len[tc][t1];
   if (rh264_vlc_alloc(v, maxl) != 0) return -1;
   for (tc = 0; tc < ntc; tc++)
      for (t1 = 0; t1 <= (tc < 3 ? tc : 3); t1++)
         if (len[tc][t1]
               && rh264_vlc_put(v, code[tc][t1], len[tc][t1],
                     tc * 4 + t1) != 0)
            return -1;
   return 0;
}

/* The 4:2:2 chroma-DC token table is stored transposed, [t1][tc]. */
static int rh264_vlc_build_ctc422(rh264_vlc *v)
{
   int tc, t1, maxl = 0;
   for (tc = 0; tc < 9; tc++)
      for (t1 = 0; t1 <= (tc < 3 ? tc : 3); t1++)
         if (rh264_ctc422_len[t1][tc] > maxl) maxl = rh264_ctc422_len[t1][tc];
   if (rh264_vlc_alloc(v, maxl) != 0) return -1;
   for (tc = 0; tc < 9; tc++)
      for (t1 = 0; t1 <= (tc < 3 ? tc : 3); t1++)
         if (rh264_ctc422_len[t1][tc]
               && rh264_vlc_put(v, rh264_ctc422_code[t1][tc],
                     rh264_ctc422_len[t1][tc], tc * 4 + t1) != 0)
            return -1;
   return 0;
}

static void rh264_vlc_free(struct rh264_vlc_set *s)
{
   int i;
   for (i = 0; i < 4; i++)  { free(s->ct[i].tab);       s->ct[i].tab = NULL; }
   for (i = 0; i < 16; i++) { free(s->tz[i].tab);       s->tz[i].tab = NULL; }
   for (i = 0; i < 4; i++)  { free(s->cdctz[i].tab);    s->cdctz[i].tab = NULL; }
   for (i = 0; i < 8; i++)  { free(s->cdctz422[i].tab); s->cdctz422[i].tab = NULL; }
   for (i = 0; i < 8; i++)  { free(s->rb[i].tab);       s->rb[i].tab = NULL; }
   free(s->ctc.tab);    s->ctc.tab = NULL;
   free(s->ctc422.tab); s->ctc422.tab = NULL;
}

/* Build the whole set; 0 on success, -1 with the set freed on failure. */
static int rh264_vlc_init(struct rh264_vlc_set *s)
{
   int i, ok = 1;
   memset(s, 0, sizeof(*s));
   for (i = 0; i < 4 && ok; i++)
      ok = rh264_vlc_build_ct(&s->ct[i],
            rh264_ct_len[i], rh264_ct_code[i], 17) == 0;
   if (ok) ok = rh264_vlc_build_ct(&s->ctc,
            rh264_ctc_len, rh264_ctc_code, 5) == 0;
   if (ok) ok = rh264_vlc_build_ctc422(&s->ctc422) == 0;
   for (i = 1; i < 16 && ok; i++)
      ok = rh264_vlc_build_row(&s->tz[i],
            rh264_tz_len[i], rh264_tz_code[i], 16) == 0;
   for (i = 1; i < 4 && ok; i++)
      ok = rh264_vlc_build_row(&s->cdctz[i],
            rh264_cdctz_len[i], rh264_cdctz_code[i], 4) == 0;
   for (i = 1; i < 8 && ok; i++)
      ok = rh264_vlc_build_row(&s->cdctz422[i],
            rh264_cdctz422_len[i], rh264_cdctz422_code[i], 8) == 0;
   for (i = 1; i < 8 && ok; i++)
      ok = rh264_vlc_build_row(&s->rb[i],
            rh264_rb_len[i], rh264_rb_code[i], 15) == 0;
   if (!ok) { rh264_vlc_free(s); return -1; }
   return 0;
}

/* One-lookup decode: symbol index on success, -1 on an invalid prefix,
 * -2 when no inverse table is attached (caller falls back). */
RH264_BITS_INLINE int rh264_vlc_look(rh264_bits *b, const rh264_vlc *v)
{
   uint16_t e;
   if (!v || !v->tab) return -2;
   e = v->tab[rh264_peek(b, v->bits)];
   if (!e) return -1;
   b->bitpos += (size_t)(e & 31u);
   return (int)(e >> 5);
}

static int rh264_vlc_match1(rh264_bits *b, const uint8_t *len_row,
      const uint8_t *code_row, int n)
{
   uint32_t acc = 0; int L = 0, i;
   while (L < 24)
   {
      acc = (acc << 1) | rh264_u1(b);
      L++;
      for (i = 0; i < n; i++)
         if (len_row[i] == L && code_row[i] == acc)
            return i;
   }
   return -1;
}

/* coeff_token: choose nC table, return total_coeff & trailing_ones. */
static int rh264_coeff_token(rh264_bits *b, int nC,
      int *total_coeff, int *trailing_ones)
{
   int t, tc, t1;
   if (nC >= 8)      t = 3;         /* FLC path baked into table 3       */
   else if (nC >= 4) t = 2;
   else if (nC >= 2) t = 1;
   else              t = 0;
   {
      int r = rh264_vlc_look(b, b->vlc ? &b->vlc->ct[t] : NULL);
      if (r >= 0)
      {
         *total_coeff   = r >> 2;
         *trailing_ones = r & 3;
         return 1;
      }
      if (r == -1) return 0;
   }
   /* Search the [17][4] (tc,t1) grid for a matching prefix. */
   {
      uint32_t acc = 0; int L = 0;
      while (L < 16)
      {
         acc = (acc << 1) | rh264_u1(b);
         L++;
         for (tc = 0; tc < 17; tc++)
            for (t1 = 0; t1 <= (tc < 3 ? tc : 3); t1++)
               if (rh264_ct_len[t][tc][t1] == L
                && rh264_ct_code[t][tc][t1] == acc)
               {
                  *total_coeff   = tc;
                  *trailing_ones = t1;
                  return 1;
               }
      }
   }
   return 0;
}

/* level_prefix: count of leading zeros terminated by a 1. */
static int rh264_level_prefix(rh264_bits *b)
{
   int lz = 0;
   /* level_prefix is a unary code the standard bounds well under 32 (a
    * conformant level_code fits in the coefficient range); a corrupt or
    * truncated stream can present an unbounded run of zeros, which would
    * otherwise drive the 1 << (level_prefix - 3) below past the width of
    * an int.  Stop counting at 32: the caller's level_code arithmetic
    * then saturates harmlessly and the block is rejected downstream. */
   uint32_t v = rh264_peek(b, 32);
   if (v)
   {
      /* the terminating 1 is real data (padding past the end reads as
       * zero), so consume the zeros and the terminator in one step */
      lz = (int)compat_clz_u32(v);
      b->bitpos += (size_t)lz + 1;
      return lz;
   }
   while (lz < 32 && rh264_more_data(b) && rh264_u1(b) == 0)
      lz++;
   return lz;
}

/* Decode one residual block. nC: predicted non-zero count (context).
 * maxNumCoeff: 16 (luma/AC), 15 (Intra16 AC after DC), 4 (chroma DC).
 * out[]: coefficients in scan order, zero-filled. Returns total_coeff. */
static int rh264_residual_block(rh264_bits *b, int nC, int maxNumCoeff,
      int32_t *out)
{
   int total_coeff = 0, trailing_ones = 0;
   int level[16], run[16];
   int i, is_chroma_dc = (nC == -1 || nC == -2);
   for (i = 0; i < maxNumCoeff; i++) out[i] = 0;

   if (is_chroma_dc)
   {
      /* chroma-DC coeff_token, dedicated VLC table: nC == -1 for the
       * four coefficients of a 4:2:0 block, nC == -2 for the eight of
       * a 4:2:2 one, whose table is indexed the other way round and
       * runs to thirteen bits. */
      uint32_t acc = 0; int L = 0, tc, t1;
      int f422 = (nC == -2), maxL = f422 ? 13 : 8, ntc = f422 ? 9 : 5;
      int r = rh264_vlc_look(b, b->vlc ? (f422 ? &b->vlc->ctc422 : &b->vlc->ctc) : NULL);
      if (r == -1) return -1;
      if (r >= 0)
      {
         total_coeff = r >> 2; trailing_ones = r & 3;
         if (total_coeff == 0) return 0;
         goto levels;
      }
      total_coeff = -1;
      while (L < maxL && total_coeff < 0)
      {
         acc = (acc << 1) | rh264_u1(b); L++;
         for (tc = 0; tc < ntc && total_coeff < 0; tc++)
            for (t1 = 0; t1 <= (tc < 3 ? tc : 3); t1++)
               if (f422
                     ? (rh264_ctc422_len[t1][tc] == L
                        && rh264_ctc422_code[t1][tc] == acc)
                     : (rh264_ctc_len[tc][t1] == L
                        && rh264_ctc_code[tc][t1] == acc))
               {
                  total_coeff = tc; trailing_ones = t1; break;
               }
      }
      if (total_coeff < 0) return -1;
      if (total_coeff == 0) return 0;
      /* fall through to shared level/run decode with maxNumCoeff==4 */
   }
   else
   {
      if (!rh264_coeff_token(b, nC, &total_coeff, &trailing_ones))
         return -1;
      if (total_coeff == 0)
         return 0;
      /* more coefficients than the block can hold is a malformed
       * stream, not something to scatter into the caller's array */
      if (total_coeff > maxNumCoeff)
         return -1;
   }

   /* Levels. */
levels:
   {
      int suffix_length = (total_coeff > 10 && trailing_ones < 3) ? 1 : 0;
      for (i = 0; i < total_coeff; i++)
      {
         if (i < trailing_ones)
         {
            level[i] = rh264_u1(b) ? -1 : 1;
         }
         else
         {
            int level_prefix = rh264_level_prefix(b);
            int level_code, level_suffix, suffix_size = suffix_length;
            if (level_prefix == 14 && suffix_length == 0) suffix_size = 4;
            else if (level_prefix >= 15) suffix_size = level_prefix - 3;
            level_suffix = suffix_size ? (int)rh264_un(b, suffix_size) : 0;
            level_code = (level_prefix < 15 ? (level_prefix << suffix_length)
                                            : ((15 << suffix_length)))
                         + level_suffix;
            if (level_prefix >= 15 && suffix_length == 0)
               level_code += 15;
            if (level_prefix >= 16)
               level_code += (1 << (level_prefix - 3)) - 4096;
            if (i == trailing_ones && trailing_ones < 3)
               level_code += 2;
            if (level_code & 1)
               level[i] = (-level_code - 1) >> 1;
            else
               level[i] = (level_code + 2) >> 1;
            /* A conformant coefficient level fits well inside 16 bits;
             * anything larger is a corrupt or truncated stream.  Reject
             * the block rather than let the value flow into the
             * dequantisation multiplies (coeff * scale, scale up to a few
             * hundred), where an out-of-range level would overflow the
             * int product.  The bound is deliberately loose - it only
             * rules out values no valid stream can produce. */
            if (level[i] > (1 << 16) || level[i] < -(1 << 16))
               return -1;
            if (suffix_length == 0)
               suffix_length = 1;
            if ((level[i] > (3 << (suffix_length - 1)) ||
                 level[i] < -(3 << (suffix_length - 1)))
                && suffix_length < 6)
               suffix_length++;
         }
      }
   }

   /* total_zeros. */
   {
      int zeros_left = 0;
      if (total_coeff < maxNumCoeff)
      {
         int idx;
         if (is_chroma_dc && maxNumCoeff == 8)
         {
            idx = rh264_vlc_look(b, b->vlc ? &b->vlc->cdctz422[total_coeff] : NULL);
            if (idx == -2)
               idx = rh264_vlc_match1(b, rh264_cdctz422_len[total_coeff],
                     rh264_cdctz422_code[total_coeff], 8);
         }
         else if (is_chroma_dc)
         {
            idx = rh264_vlc_look(b, b->vlc ? &b->vlc->cdctz[total_coeff] : NULL);
            if (idx == -2)
               idx = rh264_vlc_match1(b, rh264_cdctz_len[total_coeff],
                     rh264_cdctz_code[total_coeff], 4);
         }
         else
         {
            idx = rh264_vlc_look(b, b->vlc ? &b->vlc->tz[total_coeff] : NULL);
            if (idx == -2)
               idx = rh264_vlc_match1(b, rh264_tz_len[total_coeff],
                     rh264_tz_code[total_coeff], 16);
         }
         if (idx < 0) return -1;
         zeros_left = idx;
      }
      /* run_before. */
      for (i = 0; i < total_coeff - 1; i++)
      {
         int rbv = 0;
         if (zeros_left > 0)
         {
            int bucket = zeros_left > 6 ? 7 : zeros_left;
            int idx = rh264_vlc_look(b, b->vlc ? &b->vlc->rb[bucket] : NULL);
            if (idx == -2)
               idx = rh264_vlc_match1(b, rh264_rb_len[bucket],
                     rh264_rb_code[bucket], 15);
            if (idx < 0) return -1;
            rbv = idx;
         }
         /* a run longer than the zeros total_zeros accounted for would
          * drive the scan position backwards past the start of the
          * block */
         if (rbv > zeros_left)
            return -1;
         run[i] = rbv;
         zeros_left -= rbv;
      }
      run[total_coeff - 1] = zeros_left;
   }

   /* Place coefficients into scan positions (high-freq first). */
   {
      int coeff_num = -1;
      for (i = total_coeff - 1; i >= 0; i--)
      {
         coeff_num += run[i] + 1;
         if (coeff_num >= 0 && coeff_num < maxNumCoeff)
            out[coeff_num] = level[i];
      }
   }
   return total_coeff;
}



/* ==================== rh264_intra.h ==================== */
/* rh264 -- intra prediction (H.264 clause 8.3), baseline modes. */

#ifndef RH264_ABS
#define RH264_ABS(x) ((x)<0?-(x):(x))
#ifndef RH264_INLINE
#define RH264_INLINE
#endif
#endif


/* ---- transform bypass (lossless), 8.5.15 ----
 * TransformBypassModeFlag: the stream's qpprime_y_zero_transform_bypass_
 * flag and the macroblock's QP'Y (== QPY at 8 bits) is 0.  Then the
 * residual r is the inverse-scanned coefficient array c itself, for
 * luma and chroma alike (8.5.12, 8.5.11.2) - no scaling, no transform,
 * no DC Hadamard - added to the prediction without the (x+32)>>6 the
 * transform path rounds with. */
#define RH264_TB(f) ((f)->tb && RH264_QPP(f) == 0)
/* For intra prediction straight down or across, the residual is the
 * difference to the previous sample in that direction (8.5.15):
 * accumulate down each column (vertical prediction, dpcm 1) or along
 * each row (horizontal, dpcm 2) over the whole predicted block - the
 * 4x4, 8x8, 16x16 luma block, or the chroma macroblock. */
static void rh264_tb_dpcm(int32_t *r, int nW, int nH, int dpcm)
{
   int i, j;
   if (dpcm == 1)
      for (i = 1; i < nH; i++) for (j = 0; j < nW; j++)
         r[i*nW + j] += r[(i-1)*nW + j];
   else if (dpcm == 2)
      for (i = 0; i < nH; i++) for (j = 1; j < nW; j++)
         r[i*nW + j] += r[i*nW + j - 1];
}
/* Intra4x4/8x8/16x16PredMode 0 is vertical, 1 horizontal; the chroma
 * intra_chroma_pred_mode has them the other way round (1 horizontal,
 * 2 vertical). */
static int rh264_tb_luma_dpcm(int mode)   { return mode == 0 ? 1 : (mode == 1 ? 2 : 0); }
static int rh264_tb_chroma_dpcm(int mode) { return mode == 2 ? 1 : (mode == 1 ? 2 : 0); }
/* Scatter one 4x4 raster block into a macroblock-sized residual. */
static void rh264_tb_put4(int32_t *mb, int mbw, int bx, int by, const int32_t *blk)
{
   int y;
   for (y = 0; y < 4; y++)
      memcpy(mb + (by*4 + y)*mbw + bx*4, blk + y*4, 4*sizeof(int32_t));
}




/* ==================== rh264_mb.h ==================== */
/* rh264 -- macroblock layer + reconstruction (I slices, baseline). */

/* per-MB coded_block_flag state (for neighbour ctx of the NEXT block/MB) */
typedef struct rh264_cbf_s {
   int avail;      /* MB decoded                                  */
   int is_i16;     /* I_16x16 (vs I_4x4)                          */
   int chroma_nz;  /* chroma_pred_mode != 0 (for chroma-mode ctx) */
   int cbpLuma;    /* coded_block_pattern luma (4 bits)           */
   int cbpChroma;  /* coded_block_pattern chroma (0/1/2)          */
   int lumaDC;     /* I16 luma-DC cbf                             */
   int t8;         /* luma_transform_size_8x8_flag                */
   int pcm;        /* I_PCM: every coded_block_flag reads as 1    */
   int cDC[2];     /* chroma DC cbf [cb,cr]                       */
   /* The per-block flags are bytes: a slice reader keeps three or
    * four of these on its stack (current, left, top, dummy), and as
    * ints the 4:4:4 additions below pushed the CABAC B slice reader
    * past the 2 KiB frame budget of the console thread stacks. */
   uint8_t luma[16];   /* per-4x4 luma cbf (raster in-MB)         */
   uint8_t cAC[2][8];  /* chroma AC cbf [cb,cr][blk]; 8 for 4:2:2 */
   /* 4:4:4: Cb and Cr carry luma-shaped coded_block_flags of their
    * own (ctxBlockCat 6..13); an 8x8 block's flag is replicated into
    * its four 4x4 entries, as luma[] does. */
   uint8_t cbDC, crDC;
   uint8_t cb[16], cr[16];
} rh264_cbf;

typedef struct {
   int w,h,mbw,mbh,ystride,cstride;
   uint8_t *Y,*U,*V;
   /* Yb/Ub/Vb are views into planes; the per-block maps below and, for
    * a DPB picture, mvg/mvg2 are views into meta. Both blocks are sized
    * from the SPS geometry. The plane block travels with the plane
    * pointers when a frame swaps its planes into an output slot. */
   uint8_t *planes, *meta;
   /* Slice-reader scratch for the working frame only (carved into meta
    * by rh264_frame_alloc when requested): the coded-block-flag rows,
    * the skip and type rows for the current and, under macroblock
    * pairs, the top row, and the two motion-vector-difference grids.
    * Each reader zeroes what it uses at slice start. */
   struct rh264_cbf_s *scr_row, *scr_toprow;
   uint8_t *scr_skiprow, *scr_topskip, *scr_typerow, *scr_toptype;
   int16_t *scr_am0, *scr_am1;
   /* bi-prediction temporaries (two lists x three planes, 16x16 each):
    * off the stack because the B slice reader inlines the block
    * predictor, and 1.5 KB of locals there breached the 2 KiB frame
    * budget for the 8 KiB console thread stacks */
   uint8_t *scr_bipred;
   /* the 16-bit luma interpolation's patch and taps (rh264_bd.inc),
    * and the bypass deblocking save area: both too big for the console
    * thread stacks */
   uint8_t *scr_mc;
   struct rh264_tb_save_s *scr_tbsave;
   uint8_t *Yb,*Ub,*Vb;
   int ysb,csb,mbh_frame,field;
   int mbaff;             /* macroblock pairs, scanned two rows at a time */
   size_t plane_len;      /* the plane block's length, for a fresh one */
   struct rh264_mv_s *mvg_meta, *mvg2_meta; /* the grids carved out of meta */
   int mvg_shared;        /* mvg/mvg2 are one counted block, not meta's */
   size_t mvg_cells;      /* cells per grid, for making the frame's own */
   /* chroma rows per macroblock: 8 for 4:2:0, 16 for 4:2:2 and 4:4:4,
    * where chroma keeps the luma height */
   int cmbh;
   /* 4:4:4 (ChromaArrayType 3): the chroma planes are luma-sized and
    * Cb / Cr are decoded exactly as luma is - luma intra modes, luma
    * residual syntax (their own coefficient-count grids nzC, luma
    * shaped), luma transforms with the chroma QP and scaling lists,
    * luma motion compensation, luma-style deblocking.  Nothing
    * chroma-specific (chroma DC, intra_chroma_pred_mode, the 4:2:x
    * chroma blocks) exists in such a stream. */
   int c444;
   /* sample depth: bd bits per sample (8..14); bsh is log2 of the
    * bytes per sample (0 for 8 bits, 1 above), the shift every plane
    * offset takes to become a byte offset */
   int bd, bsh;
   /* QpBdOffsetY (== QpBdOffsetC, one depth): 6 * (bd - 8).  f->qp
    * holds QPY, the coded value, which may be negative down to -qpbd
    * at high depth; the dequantiser's qP is QP'Y = QPY + qpbd
    * (RH264_QPP), and the chroma qP goes through
    * rh264_chroma_qp_bd.  The deblocking filter and the CABAC
    * initialisation work from QPY (7.4.3, 8.7.2.2, 9.3.1.1). */
   int qpbd;
#define RH264_OFF(f, n) ((size_t)(n) << (f)->bsh)   /* samples -> bytes */
#define RH264_QPP(f) ((f)->qp + (f)->qpbd)             /* QP'Y for scaling */
/* scale to the depth: alpha, beta, tC0 and the weighted-prediction
 * offsets (which are signed - a multiply, so no negative shift) */
#define RH264_BDS(f, v) ((v) * (1 << ((f)->bd - 8)))
   int qp;
   /* qpprime_y_zero_transform_bypass_flag: with it, a macroblock at
    * QP'Y == 0 is in transform-bypass mode (7.4.5, 8.5.15) - its
    * residual IS the coefficient array, no transform, no scaling,
    * and the deblocking filter leaves its samples alone. */
   int tb;
   /* prediction may not use samples from inter-coded neighbours */
   int constrained_intra;
   int chroma_qp_offset;   /* Cb */
   int chroma_qp_offset2;  /* Cr (second_chroma_qp_index_offset) */
   uint8_t *i4mode;   /* per-4x4-block intra mode, raster mbw*4 x mbh*4 */
   uint8_t *nzL;      /* per-4x4 luma nonzero count, same grid          */
   /* per-4x4 chroma nonzero counts.  One row per chroma BLOCK row, so
    * mbw*2 wide and mbh*(cmbh/4) tall - twice as tall in 4:2:2, which
    * keeps the luma height. */
   uint8_t *nzC[2];
   uint8_t *mbqp;     /* per-MB luma QP (mbw x mbh)                     */
   uint8_t *mbt8;     /* per-MB 8x8-transform flag (mbw x mbh)          */
   uint8_t *mbslice;  /* per-MB slice index (mbw x mbh), for deblocking */
   uint8_t w4[6][16]; /* effective 4x4 weight matrices (raster)         */
   uint8_t w8[6][64]; /* effective 8x8 weight matrices (raster): Y intra/
                       * inter, then Cb and Cr pairs (4:4:4)          */
   struct rh264_mv_s *mvg; /* per-4x4 motion of this picture, reference pictures only */
   /* A stored field pair keeps a grid per parity: mvg holds the top
    * field's motion (or a frame picture's), mvg2 the bottom field's.
    * A field view points mvg at the one matching its parity, so the
    * colocated lookups index it with the field geometry they already
    * use. */
   struct rh264_mv_s *mvg2;
   int poc;                /* picture order count of this picture */
   int cropx, cropy;       /* visible window origin, luma samples */
   /* Row-lagged deblocking. When set, each slice decoder calls this
    * as it completes a macroblock row, and the video layer filters the
    * row above it: intra prediction reads unfiltered neighbours, so a
    * row is filtered only once the row below has read its last line.
    * The result is the whole-picture pass's, macroblock for
    * macroblock, since address order is unchanged; what changes is that
    * a row is filtered while it is still in cache, and that rows become
    * final one at a time - which is what a frame decoded on another
    * thread will wait on. rows_deblocked is what the finish then has
    * left to do. */
   void (*row_done)(void *user, int mby);
   void *row_user;
   int rows_deblocked;
} rh264_frame;

/* The counted block behind a frame's planes (defined with the block
 * allocator below); its row counter is what a reference read waits on. */
struct rh264_block_hdr;
struct rh264_pic_ctx;
struct rh264_video;
static struct rh264_block_hdr *rh264_block_of(const void *data);
static int rh264_block_rows_final(const void *data);
#ifdef HAVE_THREADS
static void rh264_ctx_join(struct rh264_video *v, struct rh264_pic_ctx *c);
/* One eventcount for every row publication, every wait on one and the
 * join of a picture, shared by the decoders in the process: a
 * publication is a release store and a notify that touches no lock
 * unless a thread is parked, and a waiter registers, re-checks the
 * count it needs and only then sleeps. Made by the first decoder
 * given a pool; the thumbnail streams decode on one worker thread,
 * which is what makes that first call safe. */
static retro_eventcount_t rh264_rows_ec;
static int rh264_rows_ec_ok;
#endif

/* store one raw sample (I_PCM) at sample index 'i' of a plane pointer */
static RH264_INLINE void rh264_pcm_put(const rh264_frame *f, uint8_t *p,
      size_t i, unsigned v)
{
   if (f->bsh) ((uint16_t*)p)[i] = (uint16_t)v;
   else        p[i] = (uint8_t)v;
}

/* Clamp a source coordinate to [0, max-1] for edge extension. */
#define RH264_MC_CLAMP(v, max) ((v) < 0 ? 0 : ((v) >= (max) ? (max)-1 : (v)))

/* ---- the sample kernels, per bit depth (rh264_bd.inc) ---- */
#define RH264_BD  8
#define RH264_PEL uint8_t
#define RH264_FN(name) rh264_ ## name ## _8
#include "rh264_bd.inc"
#undef RH264_BD
#undef RH264_PEL
#undef RH264_FN

/* Sixteen-bit samples for every depth above 8; 'bd' carries the depth. */
#define RH264_BD  16
#define RH264_PEL uint16_t
#define RH264_FN(name) rh264_ ## name ## _16
#include "rh264_bd.inc"
#undef RH264_BD
#undef RH264_PEL
#undef RH264_FN

/* The callers hand these byte pointers into the planes and strides in
 * samples; the frame's sample width (bsh: 0 for a byte per sample, 1
 * for two) picks the instantiation.  The 8-bit path is exactly what
 * the kernels were before the split. */
#define RH264_K2(f, name, p1, p2, ...) \
   do { if ((f)->bsh) rh264_##name##_16((uint16_t*)(p1), (uint16_t*)(p2), __VA_ARGS__, (f)->bd); \
        else          rh264_##name##_8 ((p1), (p2), __VA_ARGS__, 8); } while (0)
static void rh264_add_bypass(const rh264_frame *f, uint8_t *dst, int stride,
      const int32_t *r, int rstride, int w, int h)
{  if (f->bsh) rh264_add_bypass_16((uint16_t*)dst, stride, r, rstride, w, h, f->bd);
   else        rh264_add_bypass_8(dst, stride, r, rstride, w, h, 8); }
static void rh264_add_residual(const rh264_frame *f, uint8_t *dst, int stride,
      const int32_t *r, int n)
{  if (f->bsh) rh264_add_residual_16((uint16_t*)dst, stride, r, n, f->bd);
   else        rh264_add_residual_8(dst, stride, r, n, 8); }
static void rh264_intra16x16(const rh264_frame *f, uint8_t *dst, int stride,
      int mode, int have_up, int have_left)
{  if (f->bsh) rh264_intra16x16_16((uint16_t*)dst, stride, mode, have_up, have_left, f->bd);
   else        rh264_intra16x16_8(dst, stride, mode, have_up, have_left, 8); }
static void rh264_intra_chroma_h(const rh264_frame *f, uint8_t *dst, int stride,
      int mode, int have_up, int have_left, int ch)
{  if (f->bsh) rh264_intra_chroma_h_16((uint16_t*)dst, stride, mode, have_up, have_left, ch, f->bd);
   else        rh264_intra_chroma_h_8(dst, stride, mode, have_up, have_left, ch, 8); }
static void rh264_intra4x4(const rh264_frame *f, uint8_t *dst, int stride,
      int mode, int have_up, int have_left, int have_ur, int have_ul)
{  if (f->bsh) rh264_intra4x4_16((uint16_t*)dst, stride, mode, have_up, have_left, have_ur, have_ul, f->bd);
   else        rh264_intra4x4_8(dst, stride, mode, have_up, have_left, have_ur, have_ul, 8); }
static void rh264_intra8x8(const rh264_frame *f, uint8_t *dst, int stride,
      int mode, int have_up, int have_left, int have_ul, int have_ur)
{  if (f->bsh) rh264_intra8x8_16((uint16_t*)dst, stride, mode, have_up, have_left, have_ul, have_ur, f->bd);
   else        rh264_intra8x8_8(dst, stride, mode, have_up, have_left, have_ul, have_ur, 8); }
static void rh264_filter_luma_edge_n(const rh264_frame *f, uint8_t *e, int s,
      int ls, int n, int bS, int a, int be, int tc0v)
{  if (f->bsh) rh264_filter_luma_edge_n_16((uint16_t*)e, s, ls, n, bS, a, be, tc0v, f->bd);
   else        rh264_filter_luma_edge_n_8(e, s, ls, n, bS, a, be, tc0v, 8); }
static void rh264_filter_luma_edge_pair(const rh264_frame *f, uint8_t *e, int s,
      int ls, int bS0, int t0, int bS1, int t1, int a, int be)
{  if (f->bsh) rh264_filter_luma_edge_pair_16((uint16_t*)e, s, ls, bS0, t0, bS1, t1, a, be, f->bd);
   else        rh264_filter_luma_edge_pair_8(e, s, ls, bS0, t0, bS1, t1, a, be, 8); }
static void rh264_filter_chroma_edge_n(const rh264_frame *f, uint8_t *e, int s,
      int ls, int n, int bS, int a, int be, int tc0v)
{  if (f->bsh) rh264_filter_chroma_edge_n_16((uint16_t*)e, s, ls, n, bS, a, be, tc0v, f->bd);
   else        rh264_filter_chroma_edge_n_8(e, s, ls, n, bS, a, be, tc0v, 8); }
static void rh264_filter_chroma_edge_seg(const rh264_frame *f, uint8_t *e, int s,
      int ls, int n, const int *bS, const int *tc, int a, int be)
{  if (f->bsh) rh264_filter_chroma_edge_seg_16((uint16_t*)e, s, ls, n, bS, tc, a, be, f->bd);
   else        rh264_filter_chroma_edge_seg_8(e, s, ls, n, bS, tc, a, be, 8); }
static void rh264_tb_copy(const rh264_frame *f, uint8_t *dst, int dstride,
      const uint8_t *src, int sstride, int w, int h)
{  if (f->bsh) rh264_tb_copy_16((uint16_t*)dst, dstride, (const uint16_t*)src, sstride, w, h);
   else        rh264_tb_copy_8(dst, dstride, src, sstride, w, h); }
static void rh264_mc_luma(const rh264_frame *f, uint8_t *dst, int dstride,
      const uint8_t *ref, int rstride, int rw, int rh,
      int ox, int oy, int bw, int bh, int mvx, int mvy)
{  if (f->bsh) rh264_mc_luma_16((uint16_t*)dst, dstride, (const uint16_t*)ref, rstride, rw, rh, ox, oy, bw, bh, mvx, mvy, f->bd, f->scr_mc);
   else        rh264_mc_luma_8(dst, dstride, ref, rstride, rw, rh, ox, oy, bw, bh, mvx, mvy, 8, NULL); }
static void rh264_mc_chroma(const rh264_frame *f, uint8_t *dst, int dstride,
      const uint8_t *ref, int rstride, int rw, int rh,
      int ox, int oy, int bw, int bh, int mvx, int mvy)
{  if (f->bsh) rh264_mc_chroma_16((uint16_t*)dst, dstride, (const uint16_t*)ref, rstride, rw, rh, ox, oy, bw, bh, mvx, mvy, f->bd);
   else        rh264_mc_chroma_8(dst, dstride, ref, rstride, rw, rh, ox, oy, bw, bh, mvx, mvy, 8); }
static void rh264_wp_row(const rh264_frame *f, uint8_t *d, int n, int w,
      int rnd, int sh, int o)
{  if (f->bsh) rh264_wp_row_16((uint16_t*)d, n, w, rnd, sh, o, f->bd);
   else        rh264_wp_row_8(d, n, w, rnd, sh, o, 8); }
static void rh264_bi_block(const rh264_frame *f, uint8_t *d, int dstride,
      const uint8_t *s0, int s0s, const uint8_t *s1, int s1s,
      int w, int h, int w0, int w1, int rnd, int sh, int o)
{  if (f->bsh) rh264_bi_block_16((uint16_t*)d, dstride, (const uint16_t*)s0, s0s, (const uint16_t*)s1, s1s, w, h, w0, w1, rnd, sh, o, f->bd);
   else        rh264_bi_block_8(d, dstride, s0, s0s, s1, s1s, w, h, w0, w1, rnd, sh, o, 8); }
static void rh264_avg_block(const rh264_frame *f, uint8_t *d, int dstride,
      const uint8_t *s0, int s0s, const uint8_t *s1, int s1s, int w, int h)
{  if (f->bsh) rh264_avg_block_16((uint16_t*)d, dstride, (const uint16_t*)s0, s0s, (const uint16_t*)s1, s1s, w, h, f->bd);
   else        rh264_avg_block_8(d, dstride, s0, s0s, s1, s1s, w, h, 8); }
#undef RH264_K2

/* Macroblock position from its address.  With macroblock-adaptive
 * frame/field coding the picture is scanned in vertical PAIRS:
 * addresses 2p and 2p+1 are the top and bottom macroblock of pair p,
 * and the pairs run in raster order.  A frame-coded pair puts its two
 * macroblocks on consecutive rows. */
/* Address of the macroblock at (mbx,mby): the inverse of rh264_mb_pos.
 * Neighbour availability compares addresses against the slice's first
 * macroblock, and under pair scanning the neighbour above is not
 * simply one row of addresses back. */
static int rh264_mb_addr(int mbx, int mby, int mbw, int mbaff)
{
   if (mbaff)
      return ((mby >> 1) * mbw + mbx) * 2 + (mby & 1);
   return mby * mbw + mbx;
}

static void rh264_mb_pos(int mbaddr, int mbw, int mbaff,
      int *mbx, int *mby)
{
   if (mbaff)
   {
      int pair = mbaddr >> 1;
      *mbx = pair % mbw;
      *mby = (pair / mbw) * 2 + (mbaddr & 1);
   }
   else
   {
      *mbx = mbaddr % mbw;
      *mby = mbaddr / mbw;
   }
}


/* Apply mb_qp_delta (7-37).  The spec bounds the delta to [-26, 25] for
 * 8-bit video (7.4.5), which is exactly what makes the standard's
 * "+ 52" term enough to keep the modulo's left operand non-negative;
 * a corrupt stream can decode a delta far outside that, leaving a
 * negative QP whose /6 and %6 then index the dequantisation tables out
 * of bounds.  An illegal delta means the macroblock data is not
 * conformant, so report it rather than reconstructing from nonsense. */
static int rh264_qp_apply_delta(rh264_frame *f, int d)
{
   int span = 52 + f->qpbd;
   if (d < -(26 + f->qpbd/2) || d > 25 + f->qpbd/2)
      return -1;
   f->qp = ((f->qp + d + 52 + 2*f->qpbd) % span) - f->qpbd;
   return 0;
}


/* 4x4 luma block scan order within an MB (raster of the 4x4 blocks in the
 * standard zig-zag/Z order used by CAVLC block indexing, 8.4.x). */
static const uint8_t rh264_blk_x[16]={0,1,0,1, 2,3,2,3, 0,1,0,1, 2,3,2,3};
static const uint8_t rh264_blk_y[16]={0,0,1,1, 0,0,1,1, 2,2,3,3, 2,2,3,3};

/* CBP mapping for Intra: codeNum -> cbp via Table 9-4 (intra column). */
static const uint8_t rh264_cbp_intra[48]={
   47,31,15,0,23,27,29,30,7,11,13,14,39,43,45,46,16,3,5,10,12,19,21,26,28,35,
   37,42,44,1,2,4,8,17,18,20,24,6,9,22,25,32,33,34,36,40,38,41};
/* The same for ChromaArrayType 0 or 3 (Table 9-4, right-hand columns):
 * sixteen code numbers, luma bits only. */
static const uint8_t rh264_cbp_intra_noc[16]={
   15,0,7,11,13,14,3,5,10,12,1,2,4,8,6,9};
static const uint8_t rh264_cbp_inter_noc[16]={
   0,1,2,4,8,3,5,10,12,15,7,11,13,14,6,9};

/* nC from left & top 4x4 neighbour nonzero counts (9.2.1). gx,gy are the
 * 4x4-block grid coords. */
/* coeff_token nC derivation (9.2.1). A neighbouring 4x4 block only counts
 * when its macroblock lies in the current slice: with raster slices that is
 * exactly an address at or past the slice's first macroblock. */
static int rh264_nC(const uint8_t *nz,int gw,int gh,int gx,int gy,
      int slice_first){
   int mbw=gw>>2;
   int have_l=gx>0 && ((gy>>2)*mbw+((gx-1)>>2) >= slice_first);
   int have_t=gy>0 && (((gy-1)>>2)*mbw+(gx>>2) >= slice_first);
   int nA=0,nB=0,n=0,cnt=0;
   (void)gh;
   if(have_l){nA=nz[gy*gw+(gx-1)];cnt++;}
   if(have_t){nB=nz[(gy-1)*gw+gx];cnt++;}
   if(cnt==2)n=(nA+nB+1)>>1; else if(cnt==1)n=have_l?nA:nB; else n=0;
   return n;
}

/* ==================== 8x8 transform support (High profile) ==================== */
/* Dequantisation factors of 8.5.9 by qp%6 and position parity class,
 * expanded to the full 8x8 (the flat default weighting of 16 is folded in
 * at use). */
static const uint8_t rh264_deq8[6][64]={
   {20,19,25,19,20,19,25,19, 19,18,24,18,19,18,24,18, 25,24,32,24,25,24,32,24,
    19,18,24,18,19,18,24,18, 20,19,25,19,20,19,25,19, 19,18,24,18,19,18,24,18,
    25,24,32,24,25,24,32,24, 19,18,24,18,19,18,24,18},
   {22,21,28,21,22,21,28,21, 21,19,26,19,21,19,26,19, 28,26,35,26,28,26,35,26,
    21,19,26,19,21,19,26,19, 22,21,28,21,22,21,28,21, 21,19,26,19,21,19,26,19,
    28,26,35,26,28,26,35,26, 21,19,26,19,21,19,26,19},
   {26,24,33,24,26,24,33,24, 24,23,31,23,24,23,31,23, 33,31,42,31,33,31,42,31,
    24,23,31,23,24,23,31,23, 26,24,33,24,26,24,33,24, 24,23,31,23,24,23,31,23,
    33,31,42,31,33,31,42,31, 24,23,31,23,24,23,31,23},
   {28,26,35,26,28,26,35,26, 26,25,33,25,26,25,33,25, 35,33,45,33,35,33,45,33,
    26,25,33,25,26,25,33,25, 28,26,35,26,28,26,35,26, 26,25,33,25,26,25,33,25,
    35,33,45,33,35,33,45,33, 26,25,33,25,26,25,33,25},
   {32,30,40,30,32,30,40,30, 30,28,38,28,30,28,38,28, 40,38,51,38,40,38,51,38,
    30,28,38,28,30,28,38,28, 32,30,40,30,32,30,40,30, 30,28,38,28,30,28,38,28,
    40,38,51,38,40,38,51,38, 30,28,38,28,30,28,38,28},
   {36,34,46,34,36,34,46,34, 34,32,43,32,34,32,43,32, 46,43,58,43,46,43,58,43,
    34,32,43,32,34,32,43,32, 36,34,46,34,36,34,46,34, 34,32,43,32,34,32,43,32,
    46,43,58,43,46,43,58,43, 34,32,43,32,34,32,43,32}};

/* Dequantise an 8x8 block in raster order (8.5.13.1, flat scaling list):
 * value = round(coef * scale * 16 << qp/6 >> 6) with symmetric rounding. */
static void rh264_dequant8x8(int32_t *c, int qp, const uint8_t *w)
{
   int per = qp / 6, rem = qp % 6, i;
   for (i = 0; i < 64; i++)
      if (c[i])
      {
         int32_t v = (int32_t)((uint32_t)(c[i]
               * (int32_t)rh264_deq8[rem][i] * w[i]) << per);
         c[i] = (v + 32) >> 6;
      }
}

/* Inverse 8x8 transform (8.5.12.2); output scaled by 64 like the 4x4 path,
 * the caller rounds with (r + 32) >> 6. */
static void rh264_itransform8x8(const int32_t *d, int32_t *r)
{
   int32_t t[64];
   int i;
   for (i = 0; i < 8; i++)
   {
      const int32_t *p = d + i * 8;
      int32_t a0 = p[0] + p[4], a1 = p[0] - p[4];
      int32_t a2 = p[6] - (p[2] >> 1), a3 = p[2] + (p[6] >> 1);
      int32_t b0 = a0 + a3, b2 = a1 - a2, b4 = a1 + a2, b6 = a0 - a3;
      int32_t b1, b3, b5, b7;
      a0 = -p[3] + p[5] - p[7] - (p[7] >> 1);
      a1 =  p[1] + p[7] - p[3] - (p[3] >> 1);
      a2 = -p[1] + p[7] + p[5] + (p[5] >> 1);
      a3 =  p[3] + p[5] + p[1] + (p[1] >> 1);
      b1 = a0 + (a3 >> 2); b3 = a1 + (a2 >> 2);
      b5 = a2 - (a1 >> 2); b7 = a3 - (a0 >> 2);
      t[i*8+0] = b0 + b7; t[i*8+1] = b2 - b5;
      t[i*8+2] = b4 + b3; t[i*8+3] = b6 + b1;
      t[i*8+4] = b6 - b1; t[i*8+5] = b4 - b3;
      t[i*8+6] = b2 + b5; t[i*8+7] = b0 - b7;
   }
   for (i = 0; i < 8; i++)
   {
      int32_t p0=t[i], p1=t[8+i], p2=t[16+i], p3=t[24+i];
      int32_t p4=t[32+i], p5=t[40+i], p6=t[48+i], p7=t[56+i];
      int32_t a0 = p0 + p4, a1 = p0 - p4;
      int32_t a2 = p6 - (p2 >> 1), a3 = p2 + (p6 >> 1);
      int32_t b0 = a0 + a3, b2 = a1 - a2, b4 = a1 + a2, b6 = a0 - a3;
      int32_t b1, b3, b5, b7;
      a0 = -p3 + p5 - p7 - (p7 >> 1);
      a1 =  p1 + p7 - p3 - (p3 >> 1);
      a2 = -p1 + p7 + p5 + (p5 >> 1);
      a3 =  p3 + p5 + p1 + (p1 >> 1);
      b1 = a0 + (a3 >> 2); b7 = a3 - (a0 >> 2);
      b3 = a1 + (a2 >> 2); b5 = a2 - (a1 >> 2);
      r[i]      = b0 + b7; r[8+i]  = b2 - b5;
      r[16+i]   = b4 + b3; r[24+i] = b6 + b1;
      r[32+i]   = b6 - b1; r[40+i] = b4 - b3;
      r[48+i]   = b2 + b5; r[56+i] = b0 - b7;
   }
}


/* CAVLC residual of one 8x8-transform luma block (7.3.5.3.2): four 4x4
 * residual_block reads whose coefficients interleave into the single 8x8
 * scan (coefficient k of sub-block i lands at scan position 4k+i). Each
 * sub-block keeps its own total_coeff for later nC derivations. Applies
 * dequantisation, the 8x8 inverse transform, and adds onto the prediction
 * already in the frame. */
/* One 8x8 block of a luma-coded plane: the plane's sample pointer for
 * the block, stride, coefficient-count grid (luma shaped), qP and 8x8
 * weight matrix.  Luma passes its own; in 4:4:4 Cb and Cr pass theirs
 * (8.5.1: the chroma planes decode as luma there). */
static int rh264_cavlc_plane8x8(rh264_bits *b, rh264_frame *f,
      uint8_t *d, int stride, uint8_t *nz, const uint8_t *w8,
      int qp, int mbx, int mby, int b8, int slice_first, int dpcm)
{
   int gw = f->mbw * 4;
   int bx8 = (b8 & 1), by8 = (b8 >> 1);
   int32_t scan[64], coef[64], r[64];
   int i, k;
   for (k = 0; k < 64; k++) scan[k] = 0;
   for (i = 0; i < 4; i++)
   {
      int gx = mbx*4 + bx8*2 + (i & 1), gy = mby*4 + by8*2 + (i >> 1);
      int nC = rh264_nC(nz, gw, f->mbh*4, gx, gy, slice_first);
      int32_t sub[16];
      int tc = rh264_residual_block(b, nC, 16, sub);
      if (tc < 0) return -1;
      for (k = 0; k < 16; k++) scan[4*k + i] = sub[k];
      nz[gy*gw + gx] = (uint8_t)tc;
   }
   for (k = 0; k < 64; k++) coef[k] = 0;
   { const uint8_t *sc = RH264_SCAN8(f);
     for (k = 0; k < 64; k++) coef[sc[k]] = scan[k]; }
   if (RH264_TB(f))
   {
      rh264_tb_dpcm(coef, 8, 8, dpcm);
      rh264_add_bypass(f, d, stride, coef, 8, 8, 8);
      return 0;
   }
   rh264_dequant8x8(coef, qp, w8);
   rh264_itransform8x8(coef, r);
   rh264_add_residual(f, d, stride, r, 8);
   return 0;
}

static int rh264_cavlc_luma8x8(rh264_bits *b, rh264_frame *f,
      int mbx, int mby, int b8, int slice_first, int intra, int dpcm)
{
   int bx8 = (b8 & 1), by8 = (b8 >> 1);
   uint8_t *d = f->Y + RH264_OFF(f, (mby*16 + by8*8) * f->ystride + mbx*16 + bx8*8);
   return rh264_cavlc_plane8x8(b, f, d, f->ystride, f->nzL,
         f->w8[intra ? 0 : 1], RH264_QPP(f), mbx, mby, b8, slice_first, dpcm);
}

static int rh264_chroma_qp(int qpy, int offset, int qpbd);
static int rh264_chroma_qp_bd(const rh264_frame *f, int offset);

/* 4:4:4: one chroma plane of an intra macroblock, decoded as luma
 * (8.3.4.5, 7.3.5.3 residual_luma for Cb / Cr): the luma prediction
 * modes, the luma residual syntax against the plane's own coefficient
 * counts, the luma transforms at the chroma qP with the chroma weight
 * matrices.  Follows the luma part of the macroblock in the bitstream
 * - Cb then Cr - so the luma modes are known.  kind: 0 I_4x4 (modes[16]
 * in coding order), 1 I_8x8 (modes[4]), 2 I_16x16 (pred16). */
static int rh264_cavlc_intra_plane444(rh264_bits *b, rh264_frame *f,
      int comp, int mbx, int mby, int kind, const int *modes, int pred16,
      int cbp_luma, int have_up, int have_left, int have_ur, int have_ul,
      int slice_first)
{
   int gw = f->mbw * 4, gx0 = mbx*4, gy0 = mby*4;
   int stride = f->cstride;
   uint8_t *P  = (comp ? f->V : f->U) + RH264_OFF(f, (size_t)mby*16*stride + mbx*16);
   uint8_t *nz = f->nzC[comp];
   int qpc = rh264_chroma_qp_bd(f, comp ? f->chroma_qp_offset2 : f->chroma_qp_offset);
   const uint8_t *w4 = f->w4[1 + comp];
   const uint8_t *w8 = f->w8[2 + 2*comp];
   int tb = RH264_TB(f);
   int i, k;

   if (kind == 1)
   {
      int b8;
      for (b8 = 0; b8 < 4; b8++)
      {
         int bx8 = (b8 & 1), by8 = (b8 >> 1);
         uint8_t *d = P + RH264_OFF(f, by8*8*stride + bx8*8);
         int hu = by8 || have_up, hl = bx8 || have_left;
         int hul = (bx8 && by8) ? 1 : (bx8 ? have_up : (by8 ? have_left : have_ul));
         int hur = (b8 == 0) ? have_up : (b8 == 1) ? have_ur : (b8 == 2) ? 1 : 0;
         rh264_intra8x8(f, d, stride, modes[b8], hu, hl, hul, hur);
         if (cbp_luma & (1 << b8))
         {
            if (rh264_cavlc_plane8x8(b, f, d, stride, nz, w8, qpc,
                     mbx, mby, b8, slice_first,
                     rh264_tb_luma_dpcm(modes[b8])) < 0)
               return -1;
         }
         else
         {
            int cy, cx;
            for (cy = 0; cy < 2; cy++) for (cx = 0; cx < 2; cx++)
               nz[(gy0 + by8*2 + cy)*gw + gx0 + bx8*2 + cx] = 0;
         }
      }
      return 0;
   }
   if (kind == 0)
   {
      for (i = 0; i < 16; i++)
      {
         int bx = rh264_blk_x[i], by = rh264_blk_y[i];
         int gx = gx0 + bx, gy = gy0 + by;
         uint8_t *d = P + RH264_OFF(f, by*4*stride + bx*4);
         int hu = (by > 0) || have_up, hl = (bx > 0) || have_left;
         int hur, hulb, nzc = 0;
         switch (i)
         {
            case 2: case 6: case 8: case 9: case 10: case 12: case 14:
               hur = 1; break;
            case 3: case 11: case 13: case 15:
               hur = 0; break;
            case 0: case 1: case 4:
               hur = have_up; break;
            case 5:
               hur = have_ur; break;
            default:
               hur = 0; break;
         }
         hulb = (bx && by) ? 1 : (bx ? hu : (by ? hl : have_ul));
         rh264_intra4x4(f, d, stride, modes[i], hu, hl, hur, hulb);
         if (cbp_luma & (1 << (i >> 2)))
         {
            int nC = rh264_nC(nz, gw, f->mbh*4, gx, gy, slice_first);
            int32_t scan[16], coef[16], r[16];
            int tc = rh264_residual_block(b, nC, 16, scan);
            if (tc < 0) return -1;
            nzc = tc;
            for (k = 0; k < 16; k++) coef[k] = 0;
            { const uint8_t *sc = RH264_SCAN4(f);
              for (k = 0; k < 16; k++) coef[sc[k]] = scan[k]; }
            if (tb)
            {
               rh264_tb_dpcm(coef, 4, 4, rh264_tb_luma_dpcm(modes[i]));
               rh264_add_bypass(f, d, stride, coef, 4, 4, 4);
            }
            else
            {
               rh264_dequant4x4(coef, qpc, 0, w4);
               rh264_itransform4x4(coef, r);
               rh264_add_residual(f, d, stride, r, 4);
            }
         }
         nz[gy*gw + gx] = (uint8_t)nzc;
      }
      return 0;
   }
   /* I_16x16 */
   {
      int32_t dc[16], tmp[16], tbres[256];
      rh264_intra16x16(f, P, stride, pred16, have_up, have_left);
      {
         int nC = rh264_nC(nz, gw, f->mbh*4, gx0, gy0, slice_first);
         int32_t scan[16];
         int tc = rh264_residual_block(b, nC, 16, scan);
         if (tc < 0) return -1;
         { const uint8_t *sc = RH264_SCAN4(f);
           for (i = 0; i < 16; i++) dc[sc[i]] = scan[i]; }
         if (!tb)
         {
            int per = qpc/6, rem = qpc%6;
            int LS = w4[0]*rh264_dequant4_v[rem][0];
            rh264_ihadamard4x4(dc, tmp);
            for (i = 0; i < 16; i++)
            {
               if (qpc >= 36)
                  dc[i] = (int32_t)(((uint32_t)(tmp[i]*LS)) << (per-6));
               else
                  dc[i] = (tmp[i]*LS + (1 << (5-per))) >> (6-per);
            }
         }
      }
      for (i = 0; i < 16; i++)
      {
         int bxx = rh264_blk_x[i], byy = rh264_blk_y[i];
         int gx = gx0 + bxx, gy = gy0 + byy;
         int32_t ac[16], r[16];
         int nzc = 0;
         for (k = 0; k < 16; k++) ac[k] = 0;
         if (cbp_luma)
         {
            int nC = rh264_nC(nz, gw, f->mbh*4, gx, gy, slice_first);
            int32_t scan[16];
            int tc = rh264_residual_block(b, nC, 15, scan);
            if (tc < 0) return -1;
            nzc = tc;
            { const uint8_t *sc = RH264_SCAN4(f);
              for (k = 0; k < 15; k++) ac[sc[k+1]] = scan[k]; }
         }
         ac[0] = dc[byy*4 + bxx];
         if (tb)
            rh264_tb_put4(tbres, 16, bxx, byy, ac);
         else
         {
            rh264_dequant4x4(ac, qpc, 1, w4);
            rh264_itransform4x4(ac, r);
            rh264_add_residual(f, P + RH264_OFF(f, byy*4*stride + bxx*4), stride, r, 4);
         }
         nz[gy*gw + gx] = (uint8_t)nzc;
      }
      if (tb)
      {
         rh264_tb_dpcm(tbres, 16, 16, rh264_tb_luma_dpcm(pred16));
         rh264_add_bypass(f, P, stride, tbres, 16, 16, 16);
      }
   }
   return 0;
}



/* Chroma QP derivation (8.5.8): map qPI -> QPc. */
/* QPC from QPY and the chroma_qp_index_offset (8.5.8, Table 8-15):
 * qPI = Clip3(-QpBdOffsetC, 51, QPY + offset), mapped above 29.  This
 * is the value the deblocking filter averages (8.7.2.2); the scaling
 * qP is QP'C = QPC + QpBdOffsetC, rh264_chroma_qp_bd. */
static int rh264_chroma_qp(int qpy, int offset, int qpbd)
{
   static const int m[22]={29,30,31,32,32,33,34,34,35,35,36,36,37,37,37,38,38,38,39,39,39,39};
   int qpi = qpy + offset;
   if (qpi < -qpbd) qpi = -qpbd; else if (qpi > 51) qpi = 51;
   if (qpi < 30) return qpi;
   return m[qpi-30];
}
static int rh264_chroma_qp_bd(const rh264_frame *f, int offset)
{
   return rh264_chroma_qp(f->qp, offset, f->qpbd) + f->qpbd;
}
/* QPY of a decoded macroblock (the grid stores QPY + QpBdOffsetY so a
 * byte holds the high-depth negative range) */
static RH264_INLINE int rh264_mbqp(const rh264_frame *f, int mbi)
{
   return f->mbqp ? (int)f->mbqp[mbi] - f->qpbd : f->qp;
}

/* Chroma DC inverse transform for 4:2:2 (8.5.11.1): the eight
 * coefficients form a 2-wide, 4-tall array, transformed by a 4-point
 * Hadamard down each column and a 2-point one across. */
static void rh264_chroma_dc_idct422(int32_t *c)
{
   int col, r;
   int32_t t[8];
   for (col = 0; col < 2; col++)
   {
      int32_t a = c[col], b = c[2+col], e = c[4+col], d = c[6+col];
      int32_t s0 = a + e, s1 = a - e, s2 = b - d, s3 = b + d;
      t[col]     = s0 + s3;
      t[2+col]   = s1 + s2;
      t[4+col]   = s1 - s2;
      t[6+col]   = s0 - s3;
   }
   for (r = 0; r < 4; r++)
   {
      int32_t a = t[r*2], b = t[r*2+1];
      c[r*2]   = a + b;
      c[r*2+1] = a - b;
   }
}

/* chroma DC 2x2 inverse Hadamard (8.5.11.1). */
static void rh264_chroma_dc_idct(int32_t *c)
{
   int32_t a=c[0],b=c[1],cc=c[2],d=c[3];
   c[0]=a+b+cc+d; c[1]=a-b+cc-d; c[2]=a+b-cc-d; c[3]=a-b-cc+d;
}

/* Decode chroma residual for one MB (4:2:0). cbp_chroma: 1=DC only,
 * 2=DC+AC. Returns 0 on success. */
static int rh264_decode_chroma_residual(rh264_bits *b, rh264_frame *f,
      int mbx, int mby, uint8_t *u, uint8_t *v, int cbp_chroma, int slice_first, int inter,
      int dpcm)
{
   uint8_t *planes[2]; int comp;
   int32_t cdc[2][8];
   int tb = RH264_TB(f);
   int32_t tbres[8*16];   /* bypass: the whole chroma MB, 8 wide */
   /* 4:2:0 has four chroma blocks per component in a 2x2 arrangement,
    * 4:2:2 has eight in a 2x4 - chroma there keeps the luma height. */
   int nblk = (f->cmbh == 16) ? 8 : 4;
   planes[0]=u; planes[1]=v;
   /* chroma DC blocks (both components), nC == -1 or -2 */
   for (comp=0; comp<2; comp++)
   {
      int32_t scan[8]; int k;
      for (k=0;k<nblk;k++) cdc[comp][k]=0;
      {
         /* the eight 4:2:2 DC coefficients arrive in their own scan
          * order (Table 8-13); the four of 4:2:0 are already raster */
         static const uint8_t s422[8]={0,2,1,4,6,3,5,7};
         int tc=rh264_residual_block(b,nblk==8?-2:-1,nblk,scan);
         if (tc<0) return -1;
         if (nblk==8) for (k=0;k<8;k++) cdc[comp][s422[k]]=scan[k];
         else         for (k=0;k<4;k++) cdc[comp][k]=scan[k];
      }
      if (tb) continue;   /* bypass: dcC = c (8.5.11.1) */
      if (nblk==8) rh264_chroma_dc_idct422(cdc[comp]);
      else         rh264_chroma_dc_idct(cdc[comp]);
      /* scale (8.5.11.2): dcC = ((f * LevelScale[qpc%6][0]) << (qpc/6)) >> 5 */
      {
         int qpc=rh264_chroma_qp_bd(f,
               comp?f->chroma_qp_offset2:f->chroma_qp_offset);
         int per, rem, LS;
         /* the 4:2:2 DC uses qP + 3, and scales it two different ways
          * either side of qP 36 - a shift up above it, a ROUNDED shift
          * down below, which is where most streams sit (8.5.11.2) */
         if (nblk==8) qpc += 3;
         per=qpc/6; rem=qpc%6;
         LS=f->w4[(inter?4:1)+comp][0]*rh264_dequant4_v[rem][0];
         for (k=0;k<nblk;k++)
         {
            /* the coefficient is attacker-controlled and LS is up to
             * a few hundred; their product can exceed int.  Multiply in
             * uint32_t so the wrap is defined - the reconstruction of a
             * malformed block is discarded, only its arithmetic must not
             * be undefined. */
            if (nblk==8)
               cdc[comp][k] = (per >= 6)
                  ? (int32_t)((uint32_t)cdc[comp][k]*(uint32_t)LS << (per-6))
                  : (int32_t)(((int32_t)((uint32_t)cdc[comp][k]*(uint32_t)LS)
                        + (1 << (5-per))) >> (6-per));
            else
               cdc[comp][k]=(int32_t)((uint32_t)cdc[comp][k]*(uint32_t)LS<<per)>>5;
         }
      }
   }
   /* chroma AC blocks (only if cbp_chroma==2) + reconstruct */
   for (comp=0; comp<2; comp++)
   {
      uint8_t *p=planes[comp]; int blk;
      int cgw=f->mbw*2, cbh=f->cmbh/4;
      for (blk=0; blk<nblk; blk++)
      {
         int bx=blk&1, by=blk>>1, k;
         int cgx=mbx*2+bx, cgy=mby*cbh+by;
         int32_t ac[16],r[16];
         int nzc=0;
         for (k=0;k<16;k++) ac[k]=0;
         if (cbp_chroma==2)
         {
            /* chroma AC nC from left/top chroma-block nonzero counts */
            int nA=0,nB=0,cnt=0,nC;
            /* the slice test needs the neighbour's macroblock ADDRESS,
             * so the chroma block row has to be divided by the block
             * rows a macroblock holds - two for 4:2:0, four for 4:2:2,
             * which keeps the luma height */
            int cmbw=cgw>>1, csh=(cbh==4)?2:1;
            int hA=cgx>0 && ((cgy>>csh)*cmbw+((cgx-1)>>1) >= slice_first);
            int hB=cgy>0 && (((cgy-1)>>csh)*cmbw+(cgx>>1) >= slice_first);
            if (hA){ nA=f->nzC[comp][cgy*cgw+(cgx-1)]; cnt++; }
            if (hB){ nB=f->nzC[comp][(cgy-1)*cgw+cgx]; cnt++; }
            nC=(cnt==2)?((nA+nB+1)>>1):(cnt==1?(hA?nA:nB):0);
            {
               int32_t scan[16];
               int tc=rh264_residual_block(b,nC,15,scan);
               if (tc<0) return -1;
               nzc=tc;
               { const uint8_t *sc = RH264_SCAN4(f);
                 for (k=0;k<15;k++) ac[sc[k+1]]=scan[k]; }
            }
         }
         f->nzC[comp][cgy*cgw+cgx]=(uint8_t)nzc;
         ac[0]=cdc[comp][blk];
         if (tb)
         {
            rh264_tb_put4(tbres, 8, bx, by, ac);
            continue;
         }
         rh264_dequant4x4(ac,rh264_chroma_qp_bd(f,
               comp?f->chroma_qp_offset2:f->chroma_qp_offset),1,
               f->w4[(inter?4:1)+comp]);
         rh264_itransform4x4(ac,r);
         {
            uint8_t *d=p + RH264_OFF(f, by*4*f->cstride+bx*4);
            rh264_add_residual(f, d, f->cstride, r, 4);
         }
      }
      if (tb)
      {
         rh264_tb_dpcm(tbres, 8, f->cmbh, dpcm);
         rh264_add_bypass(f, p, f->cstride, tbres, 8, 8, f->cmbh);
      }
   }
   (void)mbx; (void)mby;
   return 0;
}

static const int rh264_alpha[52]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,5,6,7,8,9,10,12,13,15,17,20,22,25,28,32,36,40,45,50,56,63,71,80,90,101,113,127,144,162,182,203,226,255,255};
static const int rh264_beta[52]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,3,3,3,3,4,4,4,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14,15,15,16,16,17,17,18,18};
static const int rh264_tc0[3][52]={
 {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,2,2,2,2,3,3,3,4,4,4,5,6,6,7,8,9,10,11,13},
 {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,2,2,2,2,3,3,3,4,4,5,5,6,7,8,8,10,11,12,13,15,17},
 {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,2,2,2,2,3,3,3,4,4,4,5,6,6,7,8,9,10,11,13,14,16,18,20,23,25},
};


/* ---- deblocking and transform bypass (8.7.2.1) ----
 * The samples of a macroblock in transform-bypass mode (the stream's
 * qpprime_y_zero_transform_bypass_flag and QP'Y == 0 - stored per
 * macroblock in mbqp) are never modified by the filter: the filtered
 * p'i are replaced by pi when the macroblock containing p0 is such a
 * one, and likewise q'i.  The other side of the edge is still
 * filtered, from the unfiltered values.  Rather than thread a
 * per-side flag through every edge kernel (SIMD included), a bypass
 * macroblock's samples are copied aside before its edges are
 * processed and copied back after: each edge reads its inputs before
 * writing, and no later edge reads a sample this pass wrote before
 * the copy-back, so the result is the same as never writing.  The
 * left and top neighbours are covered too - the current
 * macroblock's edge writes up to three of their samples. */
typedef struct rh264_tb_save_s
{
   /* sized for two bytes per sample; the copies go through
    * rh264_tb_copy, which knows the frame's sample width */
   uint8_t y[16*16*2], u[16*16*2], v[16*16*2];   /* the current macroblock */
   uint8_t ly[16*3*2], lu[16*3*2], lv[16*3*2];   /* left neighbour, 3 columns */
   uint8_t ty[3*16*2], tu[3*16*2], tv[3*16*2];   /* top neighbour, 3 rows */
   int cur, left, top;
} rh264_tb_save;

static int rh264_tb_mb(const rh264_frame *f, int mbx, int mby)
{
   return f->tb && f->mbqp && f->mbqp[mby*f->mbw + mbx] == 0;
}


static void rh264_tb_deblock_save(const rh264_frame *f, int mbx, int mby,
      rh264_tb_save *s)
{
   int ch = f->cmbh, cw = f->c444 ? 16 : 8;
   s->cur = s->left = s->top = 0;
   if (!f->tb)
      return;
   if (rh264_tb_mb(f, mbx, mby))
   {
      s->cur = 1;
      rh264_tb_copy(f, s->y, 16, f->Y + RH264_OFF(f, (size_t)mby*16*f->ystride + mbx*16), f->ystride, 16, 16);
      rh264_tb_copy(f, s->u, cw, f->U + RH264_OFF(f, (size_t)mby*ch*f->cstride + mbx*cw), f->cstride, cw, ch);
      rh264_tb_copy(f, s->v, cw, f->V + RH264_OFF(f, (size_t)mby*ch*f->cstride + mbx*cw), f->cstride, cw, ch);
   }
   if (mbx > 0 && rh264_tb_mb(f, mbx-1, mby))
   {
      s->left = 1;
      rh264_tb_copy(f, s->ly, 3, f->Y + RH264_OFF(f, (size_t)mby*16*f->ystride + mbx*16 - 3), f->ystride, 3, 16);
      rh264_tb_copy(f, s->lu, 3, f->U + RH264_OFF(f, (size_t)mby*ch*f->cstride + mbx*cw - 3), f->cstride, 3, ch);
      rh264_tb_copy(f, s->lv, 3, f->V + RH264_OFF(f, (size_t)mby*ch*f->cstride + mbx*cw - 3), f->cstride, 3, ch);
   }
   if (mby > 0 && rh264_tb_mb(f, mbx, mby-1))
   {
      s->top = 1;
      rh264_tb_copy(f, s->ty, 16, f->Y + RH264_OFF(f, ((size_t)mby*16 - 3)*f->ystride + mbx*16), f->ystride, 16, 3);
      rh264_tb_copy(f, s->tu, cw, f->U + RH264_OFF(f, ((size_t)mby*ch - 3)*f->cstride + mbx*cw), f->cstride, cw, 3);
      rh264_tb_copy(f, s->tv, cw, f->V + RH264_OFF(f, ((size_t)mby*ch - 3)*f->cstride + mbx*cw), f->cstride, cw, 3);
   }
}

static void rh264_tb_deblock_restore(rh264_frame *f, int mbx, int mby,
      const rh264_tb_save *s)
{
   int ch = f->cmbh, cw = f->c444 ? 16 : 8;
   if (s->cur)
   {
      rh264_tb_copy(f, f->Y + RH264_OFF(f, (size_t)mby*16*f->ystride + mbx*16), f->ystride, s->y, 16, 16, 16);
      rh264_tb_copy(f, f->U + RH264_OFF(f, (size_t)mby*ch*f->cstride + mbx*cw), f->cstride, s->u, cw, cw, ch);
      rh264_tb_copy(f, f->V + RH264_OFF(f, (size_t)mby*ch*f->cstride + mbx*cw), f->cstride, s->v, cw, cw, ch);
   }
   if (s->left)
   {
      rh264_tb_copy(f, f->Y + RH264_OFF(f, (size_t)mby*16*f->ystride + mbx*16 - 3), f->ystride, s->ly, 3, 3, 16);
      rh264_tb_copy(f, f->U + RH264_OFF(f, (size_t)mby*ch*f->cstride + mbx*cw - 3), f->cstride, s->lu, 3, 3, ch);
      rh264_tb_copy(f, f->V + RH264_OFF(f, (size_t)mby*ch*f->cstride + mbx*cw - 3), f->cstride, s->lv, 3, 3, ch);
   }
   if (s->top)
   {
      rh264_tb_copy(f, f->Y + RH264_OFF(f, ((size_t)mby*16 - 3)*f->ystride + mbx*16), f->ystride, s->ty, 16, 16, 3);
      rh264_tb_copy(f, f->U + RH264_OFF(f, ((size_t)mby*ch - 3)*f->cstride + mbx*cw), f->cstride, s->tu, cw, cw, 3);
      rh264_tb_copy(f, f->V + RH264_OFF(f, ((size_t)mby*ch - 3)*f->cstride + mbx*cw), f->cstride, s->tv, cw, cw, 3);
   }
}

/* In-loop deblocking (8.7) for an all-intra frame. */
/* Deblock an all-intra picture. Parameters come per macroblock from the
 * slice that macroblock belongs to (idc/oA/oB indexed by f->mbslice, 8.7):
 * idc 1 disables the filter for that slice's macroblocks, and idc 2 keeps
 * the filter but not across a slice boundary. */
static void rh264_deblock(rh264_frame *f, const signed char *sidc,
      const signed char *soA, const signed char *soB, int mby0, int mby1)
{
   int mbx,mby,edge,mba;
   /* The filter runs macroblock by macroblock in ADDRESS order (8.7),
    * and each macroblock filters against samples its neighbours have
    * already had filtered - so the order matters.  Under pair scanning
    * address order is not raster order, and the row range is then
    * always the whole picture. */
   for(mba=mby0*f->mbw;mba<mby1*f->mbw;mba++)
   {
      int mbi, sl, oA, oB, qp, mbt8;
      rh264_tb_save *tbs = f->scr_tbsave;
      rh264_mb_pos(mba, f->mbw, f->mbaff, &mbx, &mby);
      mbi=mby*f->mbw+mbx;
      sl=f->mbslice?f->mbslice[mbi]:0;
      oA=soA[sl]; oB=soB[sl];
      qp=rh264_mbqp(f, mbi);
      mbt8 = f->mbt8 ? f->mbt8[mbi] : 0;
      if(sidc[sl]==1) continue;   /* filter disabled for this slice */
      rh264_tb_deblock_save(f, mbx, mby, tbs);
      /* ---- vertical edges (filter columns), left to right ---- */
      for(edge=0;edge<4;edge++)
      {
         int x=mbx*16+edge*4;
         int bS,qpavg,a,be,idxA,idxB,t;
         if(mbt8 && (edge&1)) continue;   /* 8x8 transform: no 4x4 edges */
         if(edge==0){ if(mbx==0) continue;
            if(sidc[sl]==2 && f->mbslice && f->mbslice[mbi-1]!=sl)
               continue;   /* no filtering across the slice boundary */
            bS=4;
            qpavg=(qp+((f->mbqp ? (int)f->mbqp[mby*f->mbw+mbx-1] - f->qpbd : qp))+1)>>1; }
         else { bS=3; qpavg=qp; }
         idxA=qpavg+oA; if(idxA<0)idxA=0; else if(idxA>51)idxA=51;
         idxB=qpavg+oB; if(idxB<0)idxB=0; else if(idxB>51)idxB=51;
         a=RH264_BDS(f, rh264_alpha[idxA]); be=RH264_BDS(f, rh264_beta[idxB]); t=RH264_BDS(f, rh264_tc0[bS==4?2:bS-1][idxA]);
         rh264_filter_luma_edge_n(f, f->Y + RH264_OFF(f, (mby*16)*f->ystride+x), 1,
               f->ystride, 16, bS, a, be, t);
         if(f->c444){
            /* luma-sized chroma, filtered as luma (chromaStyleFiltering
             * is off for ChromaArrayType 3, 8.7.2) at the chroma qP */
            int cc;
            for(cc=0;cc<2;cc++){
               int coff=cc?f->chroma_qp_offset2:f->chroma_qp_offset;
               int qc=rh264_chroma_qp(qp, coff, f->qpbd);
               int cqpavg=(edge==0)? ((qc+rh264_chroma_qp((f->mbqp ? (int)f->mbqp[mby*f->mbw+mbx-1] - f->qpbd : qp), coff, f->qpbd)+1)>>1) : qc;
               int cA=cqpavg+oA,cB=cqpavg+oB,ca,cbe,ct;
               uint8_t *pl=cc?f->V:f->U;
               if(cA<0)cA=0;else if(cA>51)cA=51; if(cB<0)cB=0;else if(cB>51)cB=51;
               ca=RH264_BDS(f, rh264_alpha[cA]);cbe=RH264_BDS(f, rh264_beta[cB]);ct=RH264_BDS(f, rh264_tc0[bS==4?2:bS-1][cA]);
               rh264_filter_luma_edge_n(f, pl + RH264_OFF(f, (size_t)(mby*16)*f->cstride+x), 1,
                     f->cstride, 16, bS, ca, cbe, ct);
            }
         }
         /* chroma: only on even edges (0 and 8 luma -> chroma 0,4) */
         else if((edge&1)==0){
            int cx=mbx*8+(edge>>1)*4, cc;
            for(cc=0;cc<2;cc++){
               int coff=cc?f->chroma_qp_offset2:f->chroma_qp_offset;
               int qc=rh264_chroma_qp(qp, coff, f->qpbd);
               int cqpavg=(edge==0)? ((qc+rh264_chroma_qp((f->mbqp ? (int)f->mbqp[mby*f->mbw+mbx-1] - f->qpbd : qp), coff, f->qpbd)+1)>>1) : qc;
               int cA=cqpavg+oA,cB=cqpavg+oB,ca,cbe,ct;
               uint8_t *pl=cc?f->V:f->U;
               if(cA<0)cA=0;else if(cA>51)cA=51; if(cB<0)cB=0;else if(cB>51)cB=51;
               ca=RH264_BDS(f, rh264_alpha[cA]);cbe=RH264_BDS(f, rh264_beta[cB]);ct=RH264_BDS(f, rh264_tc0[bS==4?2:bS-1][cA]);
               rh264_filter_chroma_edge_n(f, pl + RH264_OFF(f, (mby*f->cmbh)*f->cstride+cx), 1,
                     f->cstride, f->cmbh, bS, ca, cbe, ct);
            }
         }
      }
      /* ---- horizontal edges (filter rows), top to bottom ---- */
      for(edge=0;edge<4;edge++)
      {
         int y=mby*16+edge*4;
         int bS,qpavg,a,be,t,idxA,idxB;
         /* The 8x8 transform removes the LUMA edges inside each 8x8
          * block, not the chroma ones.  In 4:2:0 chroma has no edge
          * there either, so skipping the whole edge is the same thing;
          * in 4:2:2, which keeps every chroma row, it is not. */
         int do_luma = !(mbt8 && (edge&1));
         if(!do_luma && (f->cmbh!=16 || f->c444)) continue;
         if(edge==0){ if(mby==0) continue;
            if(sidc[sl]==2 && f->mbslice && f->mbslice[mbi-f->mbw]!=sl)
               continue;   /* no filtering across the slice boundary */
            /* A horizontal macroblock edge between field-coded
             * macroblocks takes bS 3 rather than 4 (8.7.2.1); only the
             * frame case gets the strongest filter. */
            bS=f->field?3:4;
            qpavg=(qp+((f->mbqp ? (int)f->mbqp[(mby-1)*f->mbw+mbx] - f->qpbd : qp))+1)>>1; }
         else { bS=3; qpavg=qp; }
         idxA=qpavg+oA; if(idxA<0)idxA=0; else if(idxA>51)idxA=51;
         idxB=qpavg+oB; if(idxB<0)idxB=0; else if(idxB>51)idxB=51;
         a=RH264_BDS(f, rh264_alpha[idxA]); be=RH264_BDS(f, rh264_beta[idxB]); t=RH264_BDS(f, rh264_tc0[bS==4?2:bS-1][idxA]);
         if(do_luma)
            rh264_filter_luma_edge_n(f, f->Y + RH264_OFF(f, y*f->ystride+mbx*16), f->ystride,
                  1, 16, bS, a, be, t);
         if(f->c444){
            int cc;
            for(cc=0;cc<2;cc++){
               int coff=cc?f->chroma_qp_offset2:f->chroma_qp_offset;
               int qc=rh264_chroma_qp(qp, coff, f->qpbd);
               int cqpavg=(edge==0)? ((qc+rh264_chroma_qp((f->mbqp ? (int)f->mbqp[(mby-1)*f->mbw+mbx] - f->qpbd : qp), coff, f->qpbd)+1)>>1) : qc;
               int cA=cqpavg+oA,cB=cqpavg+oB,ca,cbe,ct;
               uint8_t *pl=cc?f->V:f->U;
               if(cA<0)cA=0;else if(cA>51)cA=51; if(cB<0)cB=0;else if(cB>51)cB=51;
               ca=RH264_BDS(f, rh264_alpha[cA]);cbe=RH264_BDS(f, rh264_beta[cB]);ct=RH264_BDS(f, rh264_tc0[bS==4?2:bS-1][cA]);
               rh264_filter_luma_edge_n(f, pl + RH264_OFF(f, (size_t)y*f->cstride+mbx*16), f->cstride,
                     1, 16, bS, ca, cbe, ct);
            }
         }
         /* chroma has half the luma width but, in 4:2:2, its full
          * height - so every horizontal luma edge has a chroma edge
          * to match, where 4:2:0 has one for every second. */
         else if((edge&1)==0 || f->cmbh==16){
            int cy=mby*f->cmbh+((f->cmbh==16)?edge*4:(edge>>1)*4), cc;
            for(cc=0;cc<2;cc++){
               int coff=cc?f->chroma_qp_offset2:f->chroma_qp_offset;
               int qc=rh264_chroma_qp(qp, coff, f->qpbd);
               int cqpavg=(edge==0)? ((qc+rh264_chroma_qp((f->mbqp ? (int)f->mbqp[(mby-1)*f->mbw+mbx] - f->qpbd : qp), coff, f->qpbd)+1)>>1) : qc;
               int cA=cqpavg+oA,cB=cqpavg+oB,ca,cbe,ct;
               uint8_t *pl=cc?f->V:f->U;
               if(cA<0)cA=0;else if(cA>51)cA=51; if(cB<0)cB=0;else if(cB>51)cB=51;
               ca=RH264_BDS(f, rh264_alpha[cA]);cbe=RH264_BDS(f, rh264_beta[cB]);ct=RH264_BDS(f, rh264_tc0[bS==4?2:bS-1][cA]);
               rh264_filter_chroma_edge_n(f, pl + RH264_OFF(f, cy*f->cstride+mbx*8),
                     f->cstride, 1, 8, bS, ca, cbe, ct);
            }
         }
      }
      rh264_tb_deblock_restore(f, mbx, mby, tbs);
   }
}

static int rh264_decode_intra_mb_cavlc(rh264_bits *b, rh264_frame *f,
      int mbx, int mby, int mb_type, int t8ena, int slice_first)
{
   /* neighbouring macroblocks only exist within the current slice (6.4.8) */
   int mbaddr=rh264_mb_addr(mbx,mby,f->mbw,f->mbaff);
   /* A neighbour is available when it belongs to this slice and has
    * already been decoded (6.4.8).  In raster order the second follows
    * from the first, but pair scanning breaks that: the up-right
    * neighbour of a pair's BOTTOM macroblock is the TOP macroblock of
    * the next pair, which comes later. */
   int nb_up = rh264_mb_addr(mbx,mby-1,f->mbw,f->mbaff);
   int nb_lf = rh264_mb_addr(mbx-1,mby,f->mbw,f->mbaff);
   int nb_ur = rh264_mb_addr(mbx+1,mby-1,f->mbw,f->mbaff);
   int nb_ul = rh264_mb_addr(mbx-1,mby-1,f->mbw,f->mbaff);
   int have_up=(mby>0) && nb_up >= slice_first && nb_up < mbaddr;
   int have_left=(mbx>0) && nb_lf >= slice_first && nb_lf < mbaddr;

   int have_ur=(mby>0) && (mbx+1<f->mbw)
      && nb_ur >= slice_first && nb_ur < mbaddr;
   int have_ul=(mby>0) && (mbx>0)
      && nb_ul >= slice_first && nb_ul < mbaddr;
   int gw=f->mbw*4, cgw=f->mbw*2;
   uint8_t *y=f->Y + RH264_OFF(f, (mby*16)*f->ystride+mbx*16);
   uint8_t *u=f->U + RH264_OFF(f, (mby*f->cmbh)*f->cstride+mbx*(f->c444?16:8));
   uint8_t *v=f->V + RH264_OFF(f, (mby*f->cmbh)*f->cstride+mbx*(f->c444?16:8));

   /* where the picture forbids predicting from inter samples, an
    * inter-coded neighbour is not available to predict from at all
    * (8.3.1.2).  0xff in the mode grid marks an inter macroblock. */
   if (f->constrained_intra)
   {
      if (have_up   && f->i4mode[(mby*4-1)*gw+mbx*4] == 0xff)
         have_up = 0;
      if (have_left && f->i4mode[(mby*4)*gw+mbx*4-1] == 0xff)
         have_left = 0;
      /* the up-right and up-left neighbours feed Intra_4x4 / 8x8
       * prediction (block 5's top-right, every block's top-left) and
       * are inter-coded just as often */
      if (have_ur   && f->i4mode[(mby*4-1)*gw+mbx*4+4] == 0xff)
         have_ur = 0;
      if (have_ul   && f->i4mode[(mby*4-1)*gw+mbx*4-1] == 0xff)
         have_ul = 0;
   }
   (void)cgw;
      if(mb_type==0 && t8ena && rh264_u1(b)){
         /* I_NxN with the 8x8 transform: four 8x8 predictions, CAVLC
          * residual as interleaved 4x4 blocks (rh264_cavlc_luma8x8). */
         int modes[4], b8, chroma_mode, cbp, cbp_luma, cbp_chroma;
         f->mbt8[mby*f->mbw + mbx] = 1;
         for(b8=0;b8<4;b8++){
            int bx8=(b8&1), by8=(b8>>1);
            int cgx=mbx*4+bx8*2, cgy=mby*4+by8*2;
            int prev=rh264_u1(b);
            int la=(bx8||have_left)? f->i4mode[cgy*gw+cgx-1] : -1;
            int ta=(by8||have_up)?   f->i4mode[(cgy-1)*gw+cgx] : -1;
            int mpm, predm;
            if(la==0xff) la=2;
            if(ta==0xff) ta=2;
            if(la<0||ta<0) mpm=2; else mpm=(la<ta?la:ta);
            if(prev) predm=mpm;
            else { int rem=rh264_un(b,3); predm=(rem<mpm)?rem:rem+1; }
            modes[b8]=predm;
            { int cy,cx; for (cy = 0; cy < 2; cy++)for(cx=0;cx<2;cx++)
                 f->i4mode[(cgy+cy)*gw+cgx+cx]=(uint8_t)predm; }
         }
         /* no intra_chroma_pred_mode in 4:4:4: the chroma planes take
          * the luma modes; and the coded_block_pattern has luma bits
          * only (Table 9-4, ChromaArrayType 3 column) */
         chroma_mode=f->c444?0:rh264_ue(b);
         cbp=rh264_ue(b);
         if(f->c444){ if((unsigned)cbp>=16)return -3; cbp=rh264_cbp_intra_noc[cbp]; }
         else { if((unsigned)cbp>=48)return -3; cbp=rh264_cbp_intra[cbp]; }
         cbp_luma=cbp&15; cbp_chroma=cbp>>4;
         if(cbp_luma||cbp_chroma){ int d=rh264_se(b);
            if(rh264_qp_apply_delta(f,d)) return -1; }
         for(b8=0;b8<4;b8++){
            int bx8=(b8&1), by8=(b8>>1);
            uint8_t *d=y + RH264_OFF(f, by8*8*f->ystride+bx8*8);
            int hu=by8||have_up, hl=bx8||have_left;
            int hul=(bx8&&by8)?1:(bx8?have_up:(by8?have_left:have_ul));
            int hur=(b8==0)?have_up
                  :(b8==1)?have_ur
                  :(b8==2)?1:0;
            rh264_intra8x8(f, d,f->ystride,modes[b8],hu,hl,hul,hur);
            if(cbp_luma&(1<<b8)){
               if(rh264_cavlc_luma8x8(b,f,mbx,mby,b8,slice_first,1,
                     rh264_tb_luma_dpcm(modes[b8]))<0)
                  return -1;
            } else { int cy,cx; for (cy = 0; cy < 2; cy++)for(cx=0;cx<2;cx++)
               f->nzL[(mby*4+by8*2+cy)*gw+mbx*4+bx8*2+cx]=0; }
         }
         if(f->c444){
            if(rh264_cavlc_intra_plane444(b,f,0,mbx,mby,1,modes,0,cbp_luma,
                  have_up,have_left,have_ur,have_ul,slice_first)<0) return -1;
            if(rh264_cavlc_intra_plane444(b,f,1,mbx,mby,1,modes,0,cbp_luma,
                  have_up,have_left,have_ur,have_ul,slice_first)<0) return -1;
         } else {
         rh264_intra_chroma_h(f, u,f->cstride,chroma_mode,have_up,have_left,f->cmbh);
         rh264_intra_chroma_h(f, v,f->cstride,chroma_mode,have_up,have_left,f->cmbh);
         if(cbp_chroma) {
            if(rh264_decode_chroma_residual(b,f,mbx,mby,u,v,cbp_chroma,
                  slice_first,0,rh264_tb_chroma_dpcm(chroma_mode))<0)
               return -1;
         }
         if(!cbp_chroma){ int cx,cy; for (cy = 0; cy < f->cmbh/4; cy++)for(cx=0;cx<2;cx++){
            f->nzC[0][(mby*(f->cmbh/4)+cy)*cgw+mbx*2+cx]=0;
            f->nzC[1][(mby*(f->cmbh/4)+cy)*cgw+mbx*2+cx]=0; } }
         }
      }
      else if(mb_type==0){
         /* I_4x4 */
         int modes[16], i, chroma_mode, cbp, cbp_luma, cbp_chroma;
         for(i=0;i<16;i++){
            int bx=rh264_blk_x[i], by=rh264_blk_y[i];
            int gx=mbx*4+bx, gy=mby*4+by;
            int predm;
            int prev=rh264_u1(b);
            /* most-probable mode = min of left & top block modes (8.3.1.1) */
            int la= (bx>0||have_left)? f->i4mode[gy*gw+(gx-1)] : -1;
            int ta= (by>0||have_up)?   f->i4mode[(gy-1)*gw+gx] : -1;
            int mpm;
            /* 0xff marks an inter-coded neighbour.  It contributes
             * Intra_4x4 mode 2 (DC) to the most-probable-mode
             * derivation, unless the picture forbids predicting from
             * inter samples, when it counts as unavailable instead
             * (8.3.1.1).  A neighbour off the frame or slice edge is
             * unavailable either way (-1 -> forces DC). */
            if(la==0xff) la = f->constrained_intra ? -1 : 2;
            if(ta==0xff) ta = f->constrained_intra ? -1 : 2;
            if(la<0||ta<0) mpm=2; else mpm=(la<ta?la:ta);
            if(prev) predm=mpm;
            else { int rem=rh264_un(b,3); predm=(rem<mpm)?rem:rem+1; }
            modes[i]=predm; f->i4mode[gy*gw+gx]=(uint8_t)predm;
         }
         chroma_mode=f->c444?0:rh264_ue(b);
         cbp=rh264_ue(b);
         if(f->c444){ if((unsigned)cbp>=16)return -3; cbp=rh264_cbp_intra_noc[cbp]; }
         else { if((unsigned)cbp>=48)return -3; cbp=rh264_cbp_intra[cbp]; }
         cbp_luma=cbp&15; cbp_chroma=cbp>>4;
         if(cbp_luma||cbp_chroma){ int d=rh264_se(b);
            if(rh264_qp_apply_delta(f,d)) return -1; }

         /* per-4x4 luma: predict then (if cbp bit) residual+reconstruct */
         for(i=0;i<16;i++){
            int bx=rh264_blk_x[i], by=rh264_blk_y[i];
            int gx=mbx*4+bx, gy=mby*4+by;
            uint8_t *d=y + RH264_OFF(f, by*4*f->ystride+bx*4);
            int hu=(by>0)||have_up, hl=(bx>0)||have_left;
            int hur;
            /* Top-right (up-right) 4x4 availability per H.264 block scan.
             * Within-MB blocks whose TR block is decoded later are
             * unavailable; MB-edge blocks depend on the top / top-right MB. */
            switch(i){
               /* within-MB, TR decoded earlier -> available */
               case 2: case 6: case 8: case 9: case 10: case 12: case 14:
                  hur=1; break;
               /* within-MB, TR decoded later -> unavailable */
               case 3: case 11: case 13: case 15:
                  hur=0; break;
               /* top edge of MB (by==0): TR from top / top-right MB */
               case 0: case 1: case 4:
                  hur=have_up; break;
               case 5:
                  /* blk5 top-right leaves the MB to the top-right MB */
                  hur=have_ur; break;
               case 7:
                  /* blk7 TR is to the right MB's top -> unavailable (right
                   * MB not yet decoded in this row) */
                  hur=0; break;
               default: hur=0; break;
            }
            {
               int hulb=(bx&&by)?1:(bx?hu:(by?hl:have_ul));
               rh264_intra4x4(f, d,f->ystride,modes[i],hu,hl,hur,hulb);
            }
            {
               int32_t coef[16],r[16]; int k,nzc=0; for(k=0;k<16;k++)coef[k]=0;
               if(cbp_luma&(1<<(i>>2))){
                  int nC=rh264_nC(f->nzL,gw,f->mbh*4,gx,gy,slice_first);
                  int32_t scan[16];
                  int tc=rh264_residual_block(b,nC,16,scan);
                  if(tc<0)return -1;
                  nzc=tc;
                  { const uint8_t *sc = RH264_SCAN4(f);
                    for(k=0;k<16;k++)coef[sc[k]]=scan[k]; }
                  if(RH264_TB(f)){
                     rh264_tb_dpcm(coef,4,4,rh264_tb_luma_dpcm(modes[i]));
                     rh264_add_bypass(f, d,f->ystride,coef,4,4,4);
                  } else {
                     rh264_dequant4x4(coef, RH264_QPP(f),0,f->w4[0]);
                     rh264_itransform4x4(coef,r);
                     rh264_add_residual(f, d, f->ystride, r, 4);
                  }
               }
               f->nzL[gy*gw+gx]=(uint8_t)nzc;
            }
         }
         if(f->c444){
            if(rh264_cavlc_intra_plane444(b,f,0,mbx,mby,0,modes,0,cbp_luma,
                  have_up,have_left,have_ur,have_ul,slice_first)<0) return -1;
            if(rh264_cavlc_intra_plane444(b,f,1,mbx,mby,0,modes,0,cbp_luma,
                  have_up,have_left,have_ur,have_ul,slice_first)<0) return -1;
         } else {
         /* chroma predict */
         rh264_intra_chroma_h(f, u,f->cstride,chroma_mode,have_up,have_left,f->cmbh);
         rh264_intra_chroma_h(f, v,f->cstride,chroma_mode,have_up,have_left,f->cmbh);
         if(cbp_chroma) {
            if(rh264_decode_chroma_residual(b,f,mbx,mby,u,v,cbp_chroma,
                  slice_first,0,rh264_tb_chroma_dpcm(chroma_mode))<0)
               return -1;
         }
         /* mark chroma nz zero when no chroma residual */
         if(!cbp_chroma){ int cx,cy; for (cy = 0; cy < f->cmbh/4; cy++)for(cx=0;cx<2;cx++){
            f->nzC[0][(mby*(f->cmbh/4)+cy)*cgw+mbx*2+cx]=0;
            f->nzC[1][(mby*(f->cmbh/4)+cy)*cgw+mbx*2+cx]=0; } }
         }
      }
      else if(mb_type>=1&&mb_type<=24){
         int m=mb_type-1, pred=m%4, cbp_chroma=(m/4)%3, cbp_luma=(m>=12)?15:0;
         int32_t dc[16],tmp[16]; int i,bx,by,k;
         int gx0=mbx*4, gy0=mby*4;
         int chroma_mode;
         int tb; int32_t tbres[256];
         rh264_intra16x16(f, y,f->ystride,pred,have_up,have_left);
         chroma_mode=f->c444?0:rh264_ue(b);
         { int d=rh264_se(b);
           if(rh264_qp_apply_delta(f,d)) return -1; }
         /* luma DC (Hadamard) */
         {  int nC=rh264_nC(f->nzL,gw,f->mbh*4,gx0,gy0,slice_first);
            int32_t scan[16]; int tc=rh264_residual_block(b,nC,16,scan);
            if(tc<0)return -1;
            { const uint8_t *sc = RH264_SCAN4(f);
              for(i=0;i<16;i++)dc[sc[i]]=scan[i]; }
            tb=RH264_TB(f);
            if(!tb){
            rh264_ihadamard4x4(dc,tmp);
            /* I_16x16 luma DC scaling (8.5.10): LevelScale = 16*normAdjust
             * (flat weightScale matrix); shift per qP. */
            { int qpp=RH264_QPP(f), per=qpp/6,rem=qpp%6;
              int LS=f->w4[0][0]*rh264_dequant4_v[rem][0];
              for(i=0;i<16;i++){ if(qpp>=36)
                    dc[i]=(int32_t)(((uint32_t)(tmp[i]*LS))<<(per-6));
                 else dc[i]=(tmp[i]*LS+(1<<(5-per)))>>(6-per); } }
            }   /* bypass: dcY = c (8.5.10) */
         }
         /* luma AC per 4x4 in block scan order */
         for(i=0;i<16;i++){
            int bxx=rh264_blk_x[i], byy=rh264_blk_y[i];
            int gx=gx0+bxx, gy=gy0+byy;
            int32_t ac[16],r[16]; int nzc=0;
            for(k=0;k<16;k++)ac[k]=0;
            if(cbp_luma){
               int nC=rh264_nC(f->nzL,gw,f->mbh*4,gx,gy,slice_first);
               int32_t scan[16]; int tc=rh264_residual_block(b,nC,15,scan);
               if(tc<0)return -1;
                  nzc=tc;
               { const uint8_t *sc = RH264_SCAN4(f);
                 for(k=0;k<15;k++)ac[sc[k+1]]=scan[k]; }
            }
            ac[0]=dc[(byy*4+bxx)]; /* raster DC index within 4x4 grid */
            if(tb) rh264_tb_put4(tbres,16,bxx,byy,ac);
            else {
            rh264_dequant4x4(ac, RH264_QPP(f),1,f->w4[0]);
            rh264_itransform4x4(ac,r);
            { uint8_t *bd=y + RH264_OFF(f, byy*4*f->ystride+bxx*4);
              rh264_add_residual(f, bd, f->ystride, r, 4); }
            }
            f->nzL[gy*gw+gx]=(uint8_t)nzc;
            f->i4mode[gy*gw+gx]=2;  /* I_16x16 -> DC for neighbour MPM (8.3.1.1) */
         }
         if(tb){
            rh264_tb_dpcm(tbres,16,16,rh264_tb_luma_dpcm(pred));
            rh264_add_bypass(f, y,f->ystride,tbres,16,16,16);
         }
         (void)bx;(void)by;
         if(f->c444){
            if(rh264_cavlc_intra_plane444(b,f,0,mbx,mby,2,NULL,pred,cbp_luma,
                  have_up,have_left,have_ur,have_ul,slice_first)<0) return -1;
            if(rh264_cavlc_intra_plane444(b,f,1,mbx,mby,2,NULL,pred,cbp_luma,
                  have_up,have_left,have_ur,have_ul,slice_first)<0) return -1;
         } else {
         rh264_intra_chroma_h(f, u,f->cstride,chroma_mode,have_up,have_left,f->cmbh);
         rh264_intra_chroma_h(f, v,f->cstride,chroma_mode,have_up,have_left,f->cmbh);
         if(cbp_chroma){ if(rh264_decode_chroma_residual(b,f,mbx,mby,u,v,
               cbp_chroma,slice_first,0,rh264_tb_chroma_dpcm(chroma_mode))<0)
            return -1; }
         }
         /* an uncoded chroma block still has a coefficient count - zero -
          * and the neighbouring blocks' nC derivation (9.2.1) reads it.
          * The other intra branches record it; without this the counts
          * of whatever macroblock previously occupied this address are
          * read instead, which after an IDR is the freshly cleared grid
          * (zero, coincidentally right) but on any later picture is the
          * previous picture's counts. */
         if(!cbp_chroma&&!f->c444){ int cx,cy; for (cy = 0; cy < f->cmbh/4; cy++)for(cx=0;cx<2;cx++){
            f->nzC[0][(mby*(f->cmbh/4)+cy)*cgw+mbx*2+cx]=0;
            f->nzC[1][(mby*(f->cmbh/4)+cy)*cgw+mbx*2+cx]=0; } }
      }
      else if(mb_type==25){
         /* I_PCM: byte-align (pcm_alignment_zero_bit), then the raw
          * samples. Neighbour bookkeeping matches the CABAC path: DC
          * prediction modes (8.3.1.1) and coefficient counts of 16
          * (9.2.1); QP is unchanged. */
         /* a macroblock carries 256 luma samples and, per chroma
          * component, eight columns by as many rows as the format
          * gives it - eight for 4:2:0, sixteen for 4:2:2 */
         int r,c2,ch=f->cmbh,cw=f->c444?16:8;
         b->bitpos=(b->bitpos+7)&~(size_t)7;
         if((b->size*8-b->bitpos) < (size_t)(256+ch*cw*2)*(size_t)f->bd) return -1;
         /* pcm_sample_luma / chroma are bit_depth bits each (7.3.5) */
         for(r=0;r<16;r++)for(c2=0;c2<16;c2++)
            rh264_pcm_put(f, y, r*f->ystride+c2, rh264_un(b,f->bd));
         for(r=0;r<ch;r++)for(c2=0;c2<cw;c2++)
            rh264_pcm_put(f, u, r*f->cstride+c2, rh264_un(b,f->bd));
         for(r=0;r<ch;r++)for(c2=0;c2<cw;c2++)
            rh264_pcm_put(f, v, r*f->cstride+c2, rh264_un(b,f->bd));
         for(r=0;r<4;r++)for(c2=0;c2<4;c2++){
            f->nzL[(mby*4+r)*gw+mbx*4+c2]=16;
            f->i4mode[(mby*4+r)*gw+mbx*4+c2]=0xff;
            if(f->c444){ f->nzC[0][(mby*4+r)*gw+mbx*4+c2]=16;
                         f->nzC[1][(mby*4+r)*gw+mbx*4+c2]=16; }
         }
         if(!f->c444)
         for (r = 0; r < f->cmbh/4; r++)for(c2=0;c2<2;c2++){
            f->nzC[0][(mby*(f->cmbh/4)+r)*cgw+mbx*2+c2]=16;
            f->nzC[1][(mby*(f->cmbh/4)+r)*cgw+mbx*2+c2]=16;
         }
      }
      else return -2;
   return 0;
}


/* ==================== rh264_inter.h ==================== */
/* Inter prediction: luma sub-pel interpolation (all 15 fractional
 * positions, 8.4.2.2.1) and chroma 1/8-pel bilinear (8.4.2.2.2). */
/* rh264 inter prediction (motion compensation) for P/B slices, 4:2:0.
 * Luma uses the 6-tap half-pel filter (8.4.2.2.1) plus bilinear quarter-pel.
 * Chroma uses 1/8-pel bilinear (8.4.2.2.2). Reference samples are clamped to
 * the picture edge (unrestricted MV / edge replication). */



/* ==================== rh264_pslice.h ==================== */
/* rh264 P-slice decoding (single reference, CAVLC), 4:2:0 Main/Baseline.
 * Depends on the merged decoder's residual/transform/reconstruct helpers and
 * on rh264_inter.h for motion compensation. Motion vectors are stored on a
 * per-4x4 grid so neighbour prediction (median) works across MB boundaries. */

/* Per-4x4 motion state, carried on the frame for neighbour MV prediction,
 * inter deblock and, on reference pictures, the co-located reads of B direct
 * prediction. mvx/mvy are quarter-pel. Each cell holds both prediction
 * lists: ref/pic/mvx/mvy are list 0, ref1/pic1/mvx1/mvy1 list 1. A list a
 * block does not use keeps ref -1 with a zero vector, which is what both
 * the MV predictor (8.4.1.3.2) and the deblock reference comparison expect.
 * ref == -2 marks a cell whose macroblock is not yet decoded; intra marks
 * intra-coded blocks. refpoc0/refpoc1 carry the POC of the picture each
 * list predicts from, which identifies it for temporal direct scaling. */
typedef struct rh264_mv_s {
   int16_t mvx, mvy;
   int16_t mvx1, mvy1;
   int32_t refpoc0, refpoc1;
   int8_t  ref;   /* list 0 index: -1 = none/intra, -2 = not decoded yet   */
   int8_t  pic;   /* which reference picture that index names, -1 if none   */
   int8_t  ref1;  /* list 1 index: -1 = none                                */
   int8_t  pic1;
   int8_t  intra; /* the block is intra coded                               */
   int8_t  dir;   /* motion came from direct derivation (CABAC contexts)    */
} rh264_mv;

/* Every cell of a slice's working grid to "not decoded yet" (ref -2, the
 * other list and the picture ids -1, no motion).  A run of such cells is
 * built once and block-copied over the grid, rather than storing every
 * field of every cell. */
#define RH264_MVG_RUN 16
static void rh264_mvg_reset(rh264_mv *mvg, size_t n)
{
   rh264_mv run[RH264_MVG_RUN];
   size_t i;
   memset(&run[0], 0, sizeof(run[0]));
   run[0].ref  = -2;
   run[0].ref1 = -1;
   run[0].pic  = -1;
   run[0].pic1 = -1;
   for (i = 1; i < RH264_MVG_RUN; i++)
      run[i] = run[0];
   for (i = 0; i + RH264_MVG_RUN <= n; i += RH264_MVG_RUN)
      memcpy(mvg + i, run, sizeof(run));
   if (i < n)
      memcpy(mvg + i, run, (n - i) * sizeof(run[0]));
}

/* median of three */
static RH264_INLINE int rh264_median3(int a, int b, int c)
{
   int mx = a > b ? a : b; int mn = a < b ? a : b;
   int t = c > mx ? mx : (c < mn ? mn : c);
   return t;
}

/* Fetch neighbour MV cell at grid (gx,gy); returns availability. */
static RH264_INLINE const rh264_mv *rh264_mv_at(const rh264_mv *grid,
      int gw, int gx, int gy, int gwmax, int ghmax)
{
   if (gx < 0 || gy < 0 || gx >= gwmax || gy >= ghmax) return NULL;
   return &grid[gy * gw + gx];
}

/* Directional-predictor hint (8.4.1.3.2):
 *   0 = median (default)
 *   1 = prefer B  (16x8 top partition)
 *   2 = prefer A  (16x8 bottom, 8x16 left partition)
 *   3 = prefer C  (8x16 right partition) */
#define RH264_MVP_MEDIAN 0
#define RH264_MVP_B      1
#define RH264_MVP_A      2
#define RH264_MVP_C      3

/* Predict the MV for a partition at 4x4-grid position (gx,gy) of size
 * (bw4 x bh4) 4x4 blocks, given the current partition's ref_idx and a
 * directional hint for 16x8 / 8x16 partitions (8.4.1.3). */
/* Per-list view of a cell: the vector and reference index of one prediction
 * list. The availability marker (ref == -2) stays on the list 0 field. */
static RH264_INLINE int rh264_cell_ref(const rh264_mv *m, int list)
{ return list ? m->ref1 : m->ref; }
static RH264_INLINE int rh264_cell_mvx(const rh264_mv *m, int list)
{ return list ? m->mvx1 : m->mvx; }
static RH264_INLINE int rh264_cell_mvy(const rh264_mv *m, int list)
{ return list ? m->mvy1 : m->mvy; }

static void rh264_pred_mv_ldir(const rh264_mv *grid, int gw, int gwmax, int ghmax,
      int gx, int gy, int bw4, int ref, int hint, int list, int *pmvx, int *pmvy)
{
   const rh264_mv *A = rh264_mv_at(grid, gw, gx - 1,     gy,     gwmax, ghmax);
   const rh264_mv *B = rh264_mv_at(grid, gw, gx,         gy - 1, gwmax, ghmax);
   const rh264_mv *C = rh264_mv_at(grid, gw, gx + bw4,   gy - 1, gwmax, ghmax);
   const rh264_mv *D = rh264_mv_at(grid, gw, gx - 1,     gy - 1, gwmax, ghmax);
   int axx, axy, bxx, bxy, cxx, cxy;
   int aok, bok, cok;
   /* C falls back to D only for GEOMETRIC unavailability -- the top-right
    * neighbour is out of bounds, belongs to a macroblock not yet decoded
    * (ref == -2), or lands in a part of the current macroblock that decodes
    * after this partition. The last case is a fixed function of position
    * (6.4.11.7): for a partition not on the macroblock's top row, C is
    * unavailable when it would cross into the right half at the same height
    * or leave the macroblock to the right. It must stay positional, because
    * direct sub-blocks derive their motion before everything else and would
    * otherwise look decoded early. An intra neighbour (ref == -1) is
    * geometrically present, so it is NOT substituted; it simply contributes
    * an unavailable predictor (mv 0, ref -1). (8.4.1.3.) */
   {
      int mbpx = (gx & 3) * 4, mbpy = (gy & 3) * 4, bw = bw4 * 4;
      if (mbpy > 0)
      {
         if (mbpx < 8)
         {
            if (mbpy == 8)
            {  if (bw == 16) C = NULL; }
            else if (mbpx + bw == 8)
               C = NULL;
         }
         else if (mbpx + bw == 16)
            C = NULL;
      }
   }
   if (!C || C->ref == -2) C = D;

   /* 8.4.1.3: when B and C are both unavailable but A is available, B and C
    * take A's motion vector AND its reference index. With one reference
    * picture this is invisible, because A always matches the current index and
    * the "exactly one neighbour matches" rule below selects A regardless. With
    * several it is not: along the top row, where B and C are always absent, a
    * left neighbour using a different reference would otherwise drop the
    * predictor to the median of A and two zeros. */
   if ((!B || B->ref == -2) && (!C || C->ref == -2) && A && A->ref != -2)
   { B = A; C = A; }

   aok = A && rh264_cell_ref(A, list) >= 0;
   bok = B && rh264_cell_ref(B, list) >= 0;
   cok = C && rh264_cell_ref(C, list) >= 0;
   axx = aok ? rh264_cell_mvx(A, list) : 0; axy = aok ? rh264_cell_mvy(A, list) : 0;
   bxx = bok ? rh264_cell_mvx(B, list) : 0; bxy = bok ? rh264_cell_mvy(B, list) : 0;
   cxx = cok ? rh264_cell_mvx(C, list) : 0; cxy = cok ? rh264_cell_mvy(C, list) : 0;

   /* 8.4.1.3.2 directional prediction for 16x8 / 8x16 takes priority. */
   if (hint == RH264_MVP_B && bok && rh264_cell_ref(B, list) == ref) { *pmvx = bxx; *pmvy = bxy; return; }
   if (hint == RH264_MVP_A && aok && rh264_cell_ref(A, list) == ref) { *pmvx = axx; *pmvy = axy; return; }
   if (hint == RH264_MVP_C && cok && rh264_cell_ref(C, list) == ref) { *pmvx = cxx; *pmvy = cxy; return; }

   /* 8.4.1.3.1: if exactly one neighbour has the same ref, use it. */
   {
      int nsame = (aok && rh264_cell_ref(A, list) == ref)
                + (bok && rh264_cell_ref(B, list) == ref)
                + (cok && rh264_cell_ref(C, list) == ref);
      if (nsame == 1)
      {
         if (aok && rh264_cell_ref(A, list) == ref) { *pmvx = axx; *pmvy = axy; return; }
         if (bok && rh264_cell_ref(B, list) == ref) { *pmvx = bxx; *pmvy = bxy; return; }
         { *pmvx = cxx; *pmvy = cxy; return; }
      }
   }
   *pmvx = rh264_median3(axx, bxx, cxx);
   *pmvy = rh264_median3(axy, bxy, cxy);
}

static void rh264_pred_mv_dir(const rh264_mv *grid, int gw, int gwmax, int ghmax,
      int gx, int gy, int bw4, int ref, int hint, int *pmvx, int *pmvy)
{
   rh264_pred_mv_ldir(grid, gw, gwmax, ghmax, gx, gy, bw4, ref, hint, 0,
         pmvx, pmvy);
}

/* Median-only convenience wrapper. */
static void rh264_pred_mv(const rh264_mv *grid, int gw, int gwmax, int ghmax,
      int gx, int gy, int bw4, int ref, int *pmvx, int *pmvy)
{
   rh264_pred_mv_dir(grid, gw, gwmax, ghmax, gx, gy, bw4, ref,
         RH264_MVP_MEDIAN, pmvx, pmvy);
}

/* Store an MV into all cells of a partition and mark ref. */
/* Convenience form used where the reference picture identity is carried in
 * from the caller's picture-id table. */
static void rh264_mv_fill_pic(rh264_mv *grid, int gw, int gx, int gy,
      int bw4, int bh4, int mvx, int mvy, int ref, int pic, int refpoc)
{
   int x, y;
   for (y = 0; y < bh4; y++)
      for (x = 0; x < bw4; x++)
      {
         rh264_mv *m = &grid[(gy + y) * gw + (gx + x)];
         m->mvx = (int16_t)mvx; m->mvy = (int16_t)mvy;
         m->ref = (int8_t)ref;  m->pic = (int8_t)pic;
         m->refpoc0 = refpoc;
         m->mvx1 = 0; m->mvy1 = 0; m->ref1 = -1; m->pic1 = -1;
         m->refpoc1 = 0; m->intra = 0;
      }
}

/* Mark an inter MB's 4x4 luma-mode cells as unavailable (0xff) so a later
 * intra4x4 MB in the same P-slice computes its most-probable-mode with the
 * inter neighbour treated as unavailable (8.3.1.1). */
static void rh264_inter_clear_i4mode(rh264_frame *f, int mbx, int mby)
{
   int gw = f->mbw * 4, cy;
   uint8_t *m = f->i4mode + (size_t)(mby * 4) * gw + mbx * 4;
   for (cy = 0; cy < 4; cy++, m += gw)
      memset(m, 0xff, 4);
}

/* Motion-compensate a luma partition (bw x bh px at MB-relative bx,by) from
 * the reference frame into the current frame, using quarter-pel MV. Also does
 * the matching chroma (half size). */
/* Before a reference is read: the rows the read reaches must be final.
 * A block of @bh lines at @oy displaced by @mvy (quarter-pel) with the
 * six-tap filter reaches three lines past its last; that row of the
 * reference, and every one above it, is what the read needs. On a
 * single thread the whole picture is final before it is a reference
 * and this returns at once; a picture on another thread waits here,
 * and nowhere else. */
static retro_atomic_int_t rh264_ref_wait_misses;
/* process-wide: reference reads that waited, and the rows they were short */
static retro_atomic_int_t rh264_row_waits, rh264_row_short_sum;
#ifdef HAVE_THREADS
/* process-wide: jobs on the pool at this moment, and the most at once */
static retro_atomic_int_t rh264_jobs_running, rh264_jobs_running_max;
#endif

int rh264_video_ref_wait_misses(void)
{
   return retro_atomic_load_acquire_int(&rh264_ref_wait_misses);
}

static void rh264_ref_wait_rows(const rh264_frame *f, const rh264_frame *ref,
      int oy, int bh, int mvy)
{
   int y_last, rows;
   if (!ref || !ref->planes)
      return;
   /* The second field of a pair predicts from the first, in the same
    * block, which is counted as a whole when the pair completes: the
    * pair decodes on one thread, in order, and needs no wait on
    * itself. */
   if (f && f->planes == ref->planes)
      return;
   y_last = oy + bh + (mvy >> 2) + 3;
   if (y_last < 0)
      return;
   /* A field view interleaves the frame's lines: its line n is the
    * frame's line 2n or 2n+1, and the row counter counts the frame's
    * macroblock rows. */
   if (ref->field)
      y_last = y_last * 2 + 1;
   rows = y_last / 16 + 1;
   if (rows > ref->mbh)
      rows = ref->mbh;
   if (rh264_block_rows_final(ref->planes) < rows)
   {
#ifdef HAVE_THREADS
      /* Another thread is still decoding the reference: sleep until
       * it has published the row. Counted as well, for the sample
       * that runs one thread and asserts this never happens there -
       * a short read on one thread would be a wrong counter, and
       * a wait here for a row already handed over. */
      if (rh264_rows_ec_ok)
      {
         /* a wait, not a miss: on the pool the rows arrive */
         retro_atomic_fetch_add_int(&rh264_row_waits, 1);
         retro_atomic_fetch_add_int(&rh264_row_short_sum,
               rows - rh264_block_rows_final(ref->planes));
         while (rh264_block_rows_final(ref->planes) < rows)
         {
            int key = retro_eventcount_prepare_wait(&rh264_rows_ec);
            if (rh264_block_rows_final(ref->planes) >= rows)
            {
               retro_eventcount_cancel_wait(&rh264_rows_ec);
               break;
            }
            retro_eventcount_commit_wait(&rh264_rows_ec, key);
         }
         return;
      }
#endif
      retro_atomic_fetch_add_int(&rh264_ref_wait_misses, 1);
   }
}

static void rh264_inter_pred_block(rh264_frame *f, const rh264_frame *ref,
      int mbx, int mby, int bx, int by, int bw, int bh, int mvx, int mvy)
{
   int ox = mbx * 16 + bx, oy = mby * 16 + by;
   /* Edge extension happens at the boundaries of the decoded picture, which
    * is the full macroblock-aligned size (8.4.2.2.1: positions are clipped
    * to 0..PicWidthInSamplesL-1). Frame cropping is display-only; samples in
    * the cropped band are decoded normally and referenced by later pictures,
    * so clamping at the cropped size substitutes the last visible column or
    * row for real reference data. */
   int rw = ref->mbw * 16, rh = ref->mbh * 16;
   uint8_t *dY = f->Y + RH264_OFF(f, oy * f->ystride + ox);
   rh264_ref_wait_rows(f, ref, oy, bh, mvy);
   rh264_mc_luma(f, dY, f->ystride, ref->Y, ref->ystride, rw, rh,
         ox, oy, bw, bh, mvx, mvy);
   if (f->c444)
   {
      /* 4:4:4: the chroma planes are luma-sized and are predicted
       * exactly as luma is (8.4.2.2, ChromaArrayType 3: the luma
       * sample interpolation, the luma vector) */
      rh264_mc_luma(f, f->U + RH264_OFF(f, oy * f->cstride + ox), f->cstride, ref->U,
            ref->cstride, rw, rh, ox, oy, bw, bh, mvx, mvy);
      rh264_mc_luma(f, f->V + RH264_OFF(f, oy * f->cstride + ox), f->cstride, ref->V,
            ref->cstride, rw, rh, ox, oy, bw, bh, mvx, mvy);
   }
   else
   {
      /* 4:2:2 halves the width but keeps the height, so its chroma
       * blocks are as tall as the luma ones and a luma vector spans
       * twice as many eighths of a chroma sample vertically. */
      int c422 = (f->cmbh == 16);
      int cox = (mbx * 16 + bx) >> 1;
      int coy = c422 ? (mby * 16 + by) : ((mby * 16 + by) >> 1);
      int cbw = bw >> 1, cbh = c422 ? bh : (bh >> 1);
      int ch  = c422 ? rh : (rh >> 1);
      uint8_t *dU = f->U + RH264_OFF(f, coy * f->cstride + cox);
      uint8_t *dV = f->V + RH264_OFF(f, coy * f->cstride + cox);
      /* A field predicting from a field of the other parity samples
       * chroma half a chroma line away, because the two fields'
       * chroma sampling grids are offset (8.4.1.4).  The vector is in
       * eighths of a chroma sample, so the correction is 2. */
      /* The offset exists because 4:2:0 samples chroma at half the
       * vertical rate, so the two fields' chroma grids sit half a
       * chroma line apart.  4:2:2 keeps every row, its fields' grids
       * line up, and no correction applies. */
      int cmvy = c422 ? mvy * 2 : mvy;
      if (!c422 && f->field && ref->field && f->field != ref->field)
         cmvy += (f->field == 1) ? -2 : 2;
      rh264_mc_chroma(f, dU, f->cstride, ref->U, ref->cstride,
            rw >> 1, ch, cox, coy, cbw, cbh, mvx, cmvy);
      rh264_mc_chroma(f, dV, f->cstride, ref->V, ref->cstride,
            rw >> 1, ch, cox, coy, cbw, cbh, mvx, cmvy);
   }
}


/* ==================== rh264_pdrive.h ==================== */
/* A macroblock without chroma residual still records coefficient
 * counts of zero (9.2.1 reads them from the neighbours): two chroma
 * block columns by cmbh/4 rows in 4:2:x, the luma-shaped grids in
 * 4:4:4. */
static void rh264_nzc_clear(rh264_frame *f, int mbx, int mby)
{
   /* a row of the macroblock's flags at a time, through locals: the
    * byte stores would otherwise have the frame's pointers reread */
   uint8_t *u = f->nzC[0], *v = f->nzC[1];
   int cy;
   if (f->c444)
   {
      int gw = f->mbw * 4;
      size_t o = (size_t)(mby*4)*gw + mbx*4;
      for (cy = 0; cy < 4; cy++, o += gw)
      {
         memset(u + o, 0, 4);
         memset(v + o, 0, 4);
      }
   }
   else
   {
      int rows = f->cmbh/4, cw = f->mbw*2;
      size_t o = (size_t)(mby*rows)*cw + mbx*2;
      for (cy = 0; cy < rows; cy++, o += cw)
      {
         u[o] = 0; u[o + 1] = 0;
         v[o] = 0; v[o + 1] = 0;
      }
   }
}

/* CBP mapping for Inter macroblocks: codeNum -> cbp (Table 9-4, inter col). */
static const uint8_t rh264_cbp_inter[48]={
   0,16,1,2,4,8,32,3,5,10,12,15,47,7,11,13,14,6,9,31,35,37,42,44,33,34,36,40,
   39,43,45,46,17,18,20,24,19,21,26,28,23,27,29,30,22,25,38,41};

/* Reconstruct the 16 luma 4x4 residual blocks of an inter MB on top of the
 * already motion-compensated prediction in the current frame. */
/* The sixteen 4x4 (or four 8x8) residual blocks of one luma-coded plane
 * of an inter macroblock, added to the motion-compensated prediction
 * already in the frame.  Luma passes its own plane, counts and
 * matrices; in 4:4:4 Cb and Cr go through here too (7.3.5.3
 * residual_luma, after luma), with the chroma qP and the inter chroma
 * weight matrices. */
static int rh264_inter_plane_residual(rh264_bits *b, rh264_frame *f,
      uint8_t *plane, int stride, uint8_t *nz, const uint8_t *w4,
      const uint8_t *w8, int qp, int mbx, int mby, int cbp_luma, int t8,
      int slice_first)
{
   int gw = f->mbw * 4;
   uint8_t *y = plane + RH264_OFF(f, (size_t)(mby * 16) * stride + mbx * 16);
   int i;
   if (t8)
   {
      int b8;
      for (b8 = 0; b8 < 4; b8++)
      {
         if (cbp_luma & (1 << b8))
         {
            uint8_t *d = y + RH264_OFF(f, (b8 >> 1) * 8 * stride + (b8 & 1) * 8);
            if (rh264_cavlc_plane8x8(b, f, d, stride, nz, w8, qp,
                     mbx, mby, b8, slice_first, 0) < 0)
               return -1;
         }
         else
         {
            int cy, cx;
            for (cy = 0; cy < 2; cy++) for (cx = 0; cx < 2; cx++)
               nz[(mby*4 + (b8>>1)*2 + cy) * gw
                      + mbx*4 + (b8&1)*2 + cx] = 0;
         }
      }
      return 0;
   }
   for (i = 0; i < 16; i++)
   {
      int bx = rh264_blk_x[i], by = rh264_blk_y[i];
      int gx = mbx * 4 + bx, gy = mby * 4 + by;
      uint8_t *d = y + RH264_OFF(f, by * 4 * stride + bx * 4);
      int nzc = 0;
      if (cbp_luma & (1 << (i >> 2)))
      {
         int nC = rh264_nC(nz, gw, f->mbh * 4, gx, gy, slice_first);
         int32_t scan[16], coef[16], r[16]; int k, tc;
         for (k = 0; k < 16; k++) coef[k] = 0;
         tc = rh264_residual_block(b, nC, 16, scan);
         if (tc < 0) return -1;
         nzc = tc;

         { const uint8_t *sc = RH264_SCAN4(f);
           for (k = 0; k < 16; k++) coef[sc[k]] = scan[k]; }
         if (RH264_TB(f))
            rh264_add_bypass(f, d, stride, coef, 4, 4, 4);
         else
         {
            rh264_dequant4x4(coef, qp, 0, w4);
            rh264_itransform4x4(coef, r);
            rh264_add_residual(f, d, stride, r, 4);
         }
      }
      nz[gy * gw + gx] = (uint8_t)nzc;
   }
   return 0;
}

static int rh264_inter_luma_residual(rh264_bits *b, rh264_frame *f,
      int mbx, int mby, int cbp_luma, int t8, int slice_first)
{
   return rh264_inter_plane_residual(b, f, f->Y, f->ystride, f->nzL,
         f->w4[3], f->w8[1], RH264_QPP(f), mbx, mby, cbp_luma, t8, slice_first);
}

/* 4:4:4: Cb then Cr of an inter macroblock, as luma. */
static int rh264_inter_chroma444_residual(rh264_bits *b, rh264_frame *f,
      int mbx, int mby, int cbp_luma, int t8, int slice_first)
{
   int comp;
   for (comp = 0; comp < 2; comp++)
   {
      int qpc = rh264_chroma_qp_bd(f, comp ? f->chroma_qp_offset2 : f->chroma_qp_offset);
      if (rh264_inter_plane_residual(b, f, comp ? f->V : f->U, f->cstride,
               f->nzC[comp], f->w4[4 + comp], f->w8[3 + 2*comp], qpc,
               mbx, mby, cbp_luma, t8, slice_first) < 0)
         return -1;
   }
   return 0;
}

/* Decode one P macroblock partition's motion: predict MV, read mvd, store,
 * and motion-compensate. gx,gy = 4x4 grid pos of the partition top-left;
 * bw,bh = pixel size; bx,by = MB-relative pixel offset. */
/* Explicit weighted prediction for one predicted block (8.4.2.3.2, single
 * list), applied in place over the samples motion compensation just wrote. */

static void rh264_weight_pred(rh264_frame *f, const rh264_slice_hdr *sh,
      int refidx, int mbx, int mby, int bx, int by, int bw, int bh)
{
   int ox = mbx*16 + bx, oy = mby*16 + by, y;
   int lw, lo, ld;
   if (!sh->wp_valid) return;
   if (refidx < 0) refidx = 0;
   if (refidx > 31) refidx = 31;
   lw = sh->wp_lw[refidx]; lo = RH264_BDS(f, sh->wp_lo[refidx]); ld = sh->luma_log2_denom;
   /* a weight of 2^denom and no offset gives every sample back as it
    * was (8-270, the rounding half below the divisor): the default for
    * a reference without weights of its own, so skipped */
   if (lw != (1 << ld) || lo)
      for (y = 0; y < bh; y++)
         rh264_wp_row(f, f->Y + RH264_OFF(f, (oy+y)*f->ystride + ox), bw, lw,
               ld >= 1 ? 1 << (ld-1) : 0, ld, lo);
   {
      /* 4:2:2 keeps the luma height; 4:4:4 the luma width too */
      int c422 = (f->cmbh == 16) && !f->c444;
      int cox = f->c444 ? ox : (ox >> 1);
      int coy = (f->c444 || c422) ? oy : (oy >> 1);
      int cbw = f->c444 ? bw : (bw >> 1);
      int cbh = (f->c444 || c422) ? bh : (bh >> 1), c;
      int cd = sh->chroma_log2_denom;
      uint8_t *planes[2];
      planes[0] = f->U; planes[1] = f->V;
      for (c = 0; c < 2; c++)
      {
         int cw = sh->wp_cw[refidx][c], co = RH264_BDS(f, sh->wp_co[refidx][c]);
         if (cw == (1 << cd) && !co)
            continue;
         for (y = 0; y < cbh; y++)
            rh264_wp_row(f, planes[c] + RH264_OFF(f, (coy+y)*f->cstride + cox), cbw, cw,
                  cd >= 1 ? 1 << (cd-1) : 0, cd, co);
      }
   }
}

static void rh264_p_part_hint(rh264_bits *b, rh264_frame *f, const rh264_frame *ref,
      const rh264_slice_hdr *sh, rh264_mv *mvg, int gwmax, int ghmax,
      int mbx, int mby, int bx, int by, int bw, int bh, int refidx, int hint,
      const signed char *picid, const int *l0poc)
{
   int gx = mbx * 4 + (bx >> 2), gy = mby * 4 + (by >> 2);
   int bw4 = bw >> 2, bh4 = bh >> 2;
   int pmvx, pmvy, mvx, mvy, mvdx, mvdy;
   rh264_pred_mv_dir(mvg, gwmax, gwmax, ghmax, gx, gy, bw4, refidx, hint, &pmvx, &pmvy);
   mvdx = rh264_se(b); mvdy = rh264_se(b);

   mvx = pmvx + mvdx; mvy = pmvy + mvdy;
   rh264_mv_fill_pic(mvg, gwmax, gx, gy, bw4, bh4, mvx, mvy, refidx,
         picid ? picid[refidx] : refidx, l0poc ? l0poc[refidx] : 0);
   rh264_inter_pred_block(f, ref, mbx, mby, bx, by, bw, bh, mvx, mvy);
   rh264_weight_pred(f, sh, refidx, mbx, mby, bx, by, bw, bh);
}

/* ref_idx_lX, te(v): a single inverted bit when two pictures are available,
 * otherwise ue(v) (9.1.1). */
static int rh264_ref_idx_te(rh264_bits *b, int nrefs)
{
   int v;
   if (nrefs <= 1) return 0;
   if (nrefs == 2) return rh264_u1(b) ? 0 : 1;
   v = rh264_ue(b);
   /* A malformed stream can code an index past the end of the list; clamp so
    * it cannot be used to read outside it. */
   if (v < 0) v = 0;
   if (v >= nrefs) v = nrefs - 1;
   return v;
}

/* Median-predictor partition (8x8 and sub-8x8 use plain median). */
static void rh264_p_part(rh264_bits *b, rh264_frame *f, const rh264_frame *ref,
      const rh264_slice_hdr *sh, rh264_mv *mvg, int gwmax, int ghmax,
      int mbx, int mby, int bx, int by, int bw, int bh, int refidx,
      const signed char *picid, const int *l0poc)
{
   rh264_p_part_hint(b, f, ref, sh, mvg, gwmax, ghmax, mbx, mby,
         bx, by, bw, bh, refidx, RH264_MVP_MEDIAN, picid, l0poc);
}

/* Full P-slice decode (single reference, CAVLC). ref is the previously
 * decoded frame. mvg is a per-4x4 MV grid (gwmax x ghmax) that must persist
 * for the whole slice. Returns 0 on success. */
static int rh264_decode_pslice(rh264_bits *b, const rh264_sps *sps,
      const rh264_pps *pps, rh264_slice_hdr *sh, rh264_frame *f,
      const rh264_frame *const *l0, int nrefs, const signed char *picid,
      const int *l0poc, rh264_mv *mvg, int *end_mb)
{
   int mbaddr = sh->first_mb_in_slice, total = f->mbw * f->mbh;
   int gw = f->mbw * 4, cgw = f->mbw * 2;
   int gwmax = f->mbw * 4, ghmax = f->mbh * 4;
   int skip_run = -1;
   int prev_skipped = 0;
   f->qp = sh->slice_qp;
   f->chroma_qp_offset = pps->chroma_qp_index_offset;
   f->constrained_intra = pps->constrained_intra_pred_flag;
   f->chroma_qp_offset2 = pps->chroma_qp_index_offset2;
   f->tb = sps->tb;
   if (nrefs < 1) return -1;


   /* Reset the per-frame coefficient/mode context. These arrays feed the nC
    * neighbour derivation for CAVLC coeff_token (9.2.1) and the intra-4x4
    * most-probable-mode derivation; they describe the frame currently being
    * decoded, not the reference, so they must start clean for every slice.
    * (Without this they retain values from whichever frame last occupied this
    * buffer, corrupting nC at macroblocks whose neighbours were coded there.) */
   /* the coefficient/mode context describes the picture being decoded; a
    * continuation slice must keep what earlier slices of it produced */
   if (sh->first_mb_in_slice == 0)
   {
      memset(f->nzL, 0, (size_t)gw * f->mbh * 4);
      memset(f->mbt8, 0, (size_t)f->mbw * f->mbh);
      memset(f->nzC[0], 0, (size_t)cgw * f->mbh * (f->cmbh/4));
      memset(f->nzC[1], 0, (size_t)cgw * f->mbh * (f->cmbh/4));
      memset(f->i4mode, 0xff, (size_t)gw * f->mbh * 4);
   }

   while (mbaddr < total)
   {
      int mbx, mby;
      int mb_type;
      rh264_mb_pos(mbaddr, f->mbw, f->mbaff, &mbx, &mby);

      /* mb_skip_run; no further run or macroblock once the slice's RBSP is
       * exhausted (a trailing run can cover through the slice's last MB) */
      if (skip_run <= 0 && !rh264_more_rbsp(b)) break;
      if (skip_run < 0) skip_run = rh264_ue(b);
      if (skip_run > 0)
      {
         /* P_Skip: predicted MV with the skip special case (8.4.1.1). */
         int gx = mbx * 4, gy = mby * 4;
         const rh264_mv *A = rh264_mv_at(mvg, gwmax, gx - 1, gy, gwmax, ghmax);
         const rh264_mv *B = rh264_mv_at(mvg, gwmax, gx, gy - 1, gwmax, ghmax);
         int mvx, mvy;
         /* 8.4.1.1: P_Skip MV is zero when the left or top neighbour is not
          * available (off-frame or not-yet-decoded -> ref == -2; an intra
          * neighbour is "available" per 6.4.9 and does not trigger this), or
          * when either has refIdxL0 == 0 with a zero MV. */
         if (!A || A->ref == -2 || !B || B->ref == -2
               || (A->ref == 0 && A->mvx == 0 && A->mvy == 0)
               || (B->ref == 0 && B->mvx == 0 && B->mvy == 0))
         { mvx = 0; mvy = 0; }
         else
            rh264_pred_mv(mvg, gwmax, gwmax, ghmax, gx, gy, 4, 0, &mvx, &mvy);
         rh264_mv_fill_pic(mvg, gwmax, gx, gy, 4, 4, mvx, mvy, 0,
               picid ? picid[0] : 0, l0poc ? l0poc[0] : 0);
         rh264_inter_pred_block(f, l0[0], mbx, mby, 0, 0, 16, 16, mvx, mvy);
         rh264_weight_pred(f, sh, 0, mbx, mby, 0, 0, 16, 16);
         rh264_inter_clear_i4mode(f, mbx, mby);
         /* skip MBs carry no residual */
         { int cx, cy; for (cy = 0; cy < 4; cy++) for (cx = 0; cx < 4; cx++)
              f->nzL[(mby * 4 + cy) * gw + mbx * 4 + cx] = 0; }
         rh264_nzc_clear(f, mbx, mby);
         f->mbqp[mby * f->mbw + mbx] = (uint8_t)RH264_QPP(f);
         skip_run--; mbaddr++;
         prev_skipped = 1;
         continue;
      }
      skip_run = -1;
      /* mb_field_decoding_flag precedes a pair's top macroblock, and
       * also its bottom one when the top was skipped (7.3.4).  A
       * field-coded pair is refused. */
      if (f->mbaff && (!(mbaddr & 1) || prev_skipped) && rh264_u1(b))
         return -1;
      prev_skipped = 0;
      mb_type = rh264_ue(b);

      if (mb_type >= 5)
      {
         /* Intra MB inside a P-slice: mb_type is offset by 5 (Table 7-11). */
         int imb = mb_type - 5;
         int cx, cy;
         if (rh264_decode_intra_mb_cavlc(b, f, mbx, mby, imb,
               pps->transform_8x8_mode, sh->first_mb_in_slice) < 0) return -1;
         /* mark these 4x4 cells intra (unavailable as MV predictors) */
         for (cy = 0; cy < 4; cy++) for (cx = 0; cx < 4; cx++)
         {
            rh264_mv *m = &mvg[(mby * 4 + cy) * gwmax + mbx * 4 + cx];
            m->mvx = 0; m->mvy = 0; m->ref = -1; m->pic = -1; m->refpoc0 = 0;
            m->mvx1 = 0; m->mvy1 = 0; m->ref1 = -1; m->pic1 = -1;
            m->refpoc1 = 0; m->intra = 1;
         }
         f->mbqp[mby * f->mbw + mbx] = (uint8_t)RH264_QPP(f);
      }
      else
      {
         /* Inter P MB. */
         int cbp, cbp_luma, cbp_chroma;
         int t8 = 0, t8ok = 1;
         uint8_t *u = f->U + RH264_OFF(f, (mby * f->cmbh) * f->cstride + mbx * 8);
         uint8_t *v = f->V + RH264_OFF(f, (mby * f->cmbh) * f->cstride + mbx * 8);
         rh264_inter_clear_i4mode(f, mbx, mby);

      if (mb_type == 0)
         {
            /* P_L0_16x16 */
            int r0 = rh264_ref_idx_te(b, nrefs);
            rh264_p_part(b, f, l0[r0], sh, mvg, gwmax, ghmax, mbx, mby,
                  0, 0, 16, 16, r0, picid, l0poc);
         }
         else if (mb_type == 1)
         {
            /* P_L0_L0_16x8: every ref_idx precedes every mvd (7.3.5.1). */
            int ra = rh264_ref_idx_te(b, nrefs);
            int rb = rh264_ref_idx_te(b, nrefs);
            rh264_p_part_hint(b, f, l0[ra], sh, mvg, gwmax, ghmax, mbx, mby,
                  0, 0, 16, 8, ra, RH264_MVP_B, picid, l0poc);
            rh264_p_part_hint(b, f, l0[rb], sh, mvg, gwmax, ghmax, mbx, mby,
                  0, 8, 16, 8, rb, RH264_MVP_A, picid, l0poc);
         }
         else if (mb_type == 2)
         {
            int ra = rh264_ref_idx_te(b, nrefs);
            int rb = rh264_ref_idx_te(b, nrefs);
            rh264_p_part_hint(b, f, l0[ra], sh, mvg, gwmax, ghmax, mbx, mby,
                  0, 0, 8, 16, ra, RH264_MVP_A, picid, l0poc);
            rh264_p_part_hint(b, f, l0[rb], sh, mvg, gwmax, ghmax, mbx, mby,
                  8, 0, 8, 16, rb, RH264_MVP_C, picid, l0poc);
         }
         else
         {
            /* P_8x8 / P_8x8ref0: all sub_mb_type, then all ref_idx, then the
             * motion vector differences (7.3.5.2). P_8x8ref0 has no ref_idx. */
            int sub[4], rf[4], p;
            for (p = 0; p < 4; p++) sub[p] = rh264_ue(b);
            t8ok = (sub[0] == 0 && sub[1] == 0 && sub[2] == 0 && sub[3] == 0);
            for (p = 0; p < 4; p++)
               rf[p] = (mb_type == 4) ? 0 : rh264_ref_idx_te(b, nrefs);
            for (p = 0; p < 4; p++)
            {
               int px = (p & 1) * 8, py = (p >> 1) * 8;
               int st = sub[p], rr = rf[p];
               const rh264_frame *rp = l0[rr];
               if (st == 0)
                  rh264_p_part(b, f, rp, sh, mvg, gwmax, ghmax, mbx, mby, px, py, 8, 8, rr, picid, l0poc);
               else if (st == 1)
               {
                  rh264_p_part(b, f, rp, sh, mvg, gwmax, ghmax, mbx, mby, px, py,   8, 4, rr, picid, l0poc);
                  rh264_p_part(b, f, rp, sh, mvg, gwmax, ghmax, mbx, mby, px, py+4, 8, 4, rr, picid, l0poc);
               }
               else if (st == 2)
               {
                  rh264_p_part(b, f, rp, sh, mvg, gwmax, ghmax, mbx, mby, px,   py, 4, 8, rr, picid, l0poc);
                  rh264_p_part(b, f, rp, sh, mvg, gwmax, ghmax, mbx, mby, px+4, py, 4, 8, rr, picid, l0poc);
               }
               else
               {
                  rh264_p_part(b, f, rp, sh, mvg, gwmax, ghmax, mbx, mby, px,   py,   4, 4, rr, picid, l0poc);
                  rh264_p_part(b, f, rp, sh, mvg, gwmax, ghmax, mbx, mby, px+4, py,   4, 4, rr, picid, l0poc);
                  rh264_p_part(b, f, rp, sh, mvg, gwmax, ghmax, mbx, mby, px,   py+4, 4, 4, rr, picid, l0poc);
                  rh264_p_part(b, f, rp, sh, mvg, gwmax, ghmax, mbx, mby, px+4, py+4, 4, 4, rr, picid, l0poc);
               }
            }
         }

         /* coded_block_pattern */
         cbp = rh264_ue(b);
         if (f->c444) { if ((unsigned)cbp >= 16) return -3; cbp = rh264_cbp_inter_noc[cbp]; }
         else { if ((unsigned)cbp >= 48) { return -3; } cbp = rh264_cbp_inter[cbp]; }
         cbp_luma = cbp & 15; cbp_chroma = cbp >> 4;
         /* transform_size_8x8_flag sits between the cbp and mb_qp_delta
          * (7.3.5) and is only coded when the partitioning allows it */
         if (cbp_luma && pps->transform_8x8_mode && t8ok)
         {
            t8 = rh264_u1(b);
            if (t8) f->mbt8[mby*f->mbw + mbx] = 1;
         }
         if (cbp_luma || cbp_chroma)
         {
            int d = rh264_se(b);
            if (rh264_qp_apply_delta(f, d)) return -1;
         }
         if (rh264_inter_luma_residual(b, f, mbx, mby, cbp_luma, t8,
               sh->first_mb_in_slice) < 0) return -1;
         if (f->c444)
         {
            if (rh264_inter_chroma444_residual(b, f, mbx, mby, cbp_luma, t8,
                  sh->first_mb_in_slice) < 0) return -1;
         }
         else if (cbp_chroma)
         {
            if (rh264_decode_chroma_residual(b, f, mbx, mby, u, v, cbp_chroma,
                  sh->first_mb_in_slice, 1, 0) < 0)
               return -1;
         }
         else
            rh264_nzc_clear(f, mbx, mby);
         f->mbqp[mby * f->mbw + mbx] = (uint8_t)RH264_QPP(f);
      }
      mbaddr++;
   }

   if (end_mb) *end_mb = mbaddr;
   return 0;
}

/* Boundary strength for a 4x4 edge between block P (side with lower coord) and
 * block Q, per 8.7.2.1. mbedge != 0 when the edge is a macroblock boundary.
 * intra_p/intra_q: the block belongs to an intra MB. cbf_p/cbf_q: the block has
 * non-zero transform coefficients. The two MVs (single reference list, one ref)
 * decide bS 1 vs 0 by the quarter-pel >= 4 rule. */

/* ==================== rh264_bslice.h ==================== */
/* Everything B slices add on top of P: two reference lists ordered by POC,
 * direct prediction (spatial and temporal), bi-prediction and the three
 * weighting modes of weighted_bipred_idc. */

/* Store one list's motion into a partition's cells. clear_other resets the
 * opposite list to "unused", for blocks predicted from one list only. */
static void rh264_mv_fill_list(rh264_mv *grid, int gw, int gx, int gy,
      int bw4, int bh4, int list, int mvx, int mvy, int ref, int pic,
      int refpoc, int clear_other)
{
   int x, y;
   for (y = 0; y < bh4; y++)
      for (x = 0; x < bw4; x++)
      {
         rh264_mv *m = &grid[(gy + y) * gw + (gx + x)];
         m->intra = 0;
         m->dir = 0;
         if (list == 0)
         {
            m->mvx = (int16_t)mvx; m->mvy = (int16_t)mvy;
            m->ref = (int8_t)ref;  m->pic = (int8_t)pic; m->refpoc0 = refpoc;
            if (clear_other)
            { m->mvx1 = 0; m->mvy1 = 0; m->ref1 = -1; m->pic1 = -1;
              m->refpoc1 = 0; }
         }
         else
         {
            m->mvx1 = (int16_t)mvx; m->mvy1 = (int16_t)mvy;
            m->ref1 = (int8_t)ref;  m->pic1 = (int8_t)pic; m->refpoc1 = refpoc;
            if (clear_other)
            { m->mvx = 0; m->mvy = 0; m->ref = -1; m->pic = -1;
              m->refpoc0 = 0; }
         }
      }
}

/* Per-slice B state. */
typedef struct {
   const rh264_frame *l0[34], *l1[34];
   int l0poc[34], l1poc[34];
   signed char pid0[34], pid1[34];
   int n0, n1;
   int currpoc;
   int direct_spatial;
   int d8x8;                  /* SPS direct_8x8_inference_flag */
   int wbidc;                 /* PPS weighted_bipred_idc */
   const rh264_mv *colg;      /* motion grid of RefPicList1[0] */
   signed char w1imp[32][32]; /* implicit list 1 weight, list 0 = 64 - w1 */
   int mvscale[32];           /* temporal DistScaleFactor per l0 index, 9999 = copy */
} rh264_bctx;

/* Implicit bi-prediction weights (8.4.2.3.1) and temporal direct scale
 * factors (8.4.1.2.3), both pure functions of the POC distances between the
 * current picture and each list entry. */
static void rh264_b_setup_scales(rh264_bctx *bc)
{
   int i, j;
   for (i = 0; i < bc->n0 && i < 32; i++)
   {
      int td, tb, tx, dsf;
      for (j = 0; j < bc->n1 && j < 32; j++)
      {
         td = bc->l1poc[j] - bc->l0poc[i];
         if (td < -128) td = -128; else if (td > 127) td = 127;
         if (td == 0) { bc->w1imp[i][j] = 32; continue; }
         tb = bc->currpoc - bc->l0poc[i];
         if (tb < -128) tb = -128; else if (tb > 127) tb = 127;
         tx = (16384 + (td < 0 ? (-td) / 2 : td / 2)) / td;
         dsf = (tb * tx + 32) >> 6;
         if (dsf < -1024) dsf = -1024; else if (dsf > 1023) dsf = 1023;
         dsf >>= 2;
         if (dsf < -64 || dsf > 128) dsf = 32;
         bc->w1imp[i][j] = (signed char)dsf;
      }
      td = bc->l1poc[0] - bc->l0poc[i];
      if (td < -128) td = -128; else if (td > 127) td = 127;
      if (td == 0) bc->mvscale[i] = 9999;
      else
      {
         tb = bc->currpoc - bc->l0poc[i];
         if (tb < -128) tb = -128; else if (tb > 127) tb = 127;
         tx = (16384 + (td < 0 ? (-td) / 2 : td / 2)) / td;
         dsf = (tb * tx + 32) >> 6;
         if (dsf < -1024) dsf = -1024; else if (dsf > 1023) dsf = 1023;
         bc->mvscale[i] = dsf;
      }
   }
}

/* Motion compensate one list of a block into 16x16/8x8 scratch planes. */
/* curfield is the parity of the picture being predicted, 0 for a frame.
 * Predicting from a field of the other parity offsets the vertical
 * chroma vector, because the two fields' chroma grids sit half a chroma
 * line apart (8.4.1.4). */
static void rh264_b_mc_tmp(const rh264_frame *f, uint8_t *ty, uint8_t *tu,
      uint8_t *tv, const rh264_frame *ref, int ox, int oy, int bw, int bh,
      int mvx, int mvy, int curfield, int c422, int c444)
{
   int rw = ref->mbw * 16, rh = ref->mbh * 16;
   int cmvy = mvy;
   /* The B predictions read their references here, not through
    * rh264_inter_pred_block: the same wait, on the same rows. */
   rh264_ref_wait_rows(f, ref, oy, bh, mvy);
   if (!c422 && curfield && ref->field && curfield != ref->field)
      cmvy += (curfield == 1) ? -2 : 2;
   rh264_mc_luma(f, ty, 16, ref->Y, ref->ystride, rw, rh, ox, oy, bw, bh,
         mvx, mvy);
   if (c444)
   {
      /* luma-sized chroma, luma interpolation, 16-wide scratch */
      rh264_mc_luma(f, tu, 16, ref->U, ref->cstride, rw, rh, ox, oy, bw, bh,
            mvx, mvy);
      rh264_mc_luma(f, tv, 16, ref->V, ref->cstride, rw, rh, ox, oy, bw, bh,
            mvx, mvy);
      return;
   }
   /* 4:2:2 keeps the luma height: the chroma block is as tall as the
    * luma one and the vector spans twice the eighths vertically. */
   if (c422) cmvy = mvy * 2;   /* and no parity offset: see above */
   rh264_mc_chroma(f, tu, 8, ref->U, ref->cstride, rw >> 1,
         c422 ? rh : (rh >> 1), ox >> 1, c422 ? oy : (oy >> 1),
         bw >> 1, c422 ? bh : (bh >> 1), mvx, cmvy);
   rh264_mc_chroma(f, tv, 8, ref->V, ref->cstride, rw >> 1,
         c422 ? rh : (rh >> 1), ox >> 1, c422 ? oy : (oy >> 1),
         bw >> 1, c422 ? bh : (bh >> 1), mvx, cmvy);
}

/* Single-list explicit weighting over samples just written (8.4.2.3.2),
 * list selectable so B blocks predicted from list 1 use its table. */
static void rh264_weight_pred_list(rh264_frame *f, const rh264_slice_hdr *sh,
      int list, int refidx, int mbx, int mby, int bx, int by, int bw, int bh)
{
   int ox = mbx*16 + bx, oy = mby*16 + by, y;
   int lw, lo, ld;
   const int16_t *tlw, *tlo; const int16_t (*tcw)[2]; const int16_t (*tco)[2];
   if (!sh->wp_valid) return;
   if (refidx < 0) refidx = 0;
   if (refidx > 31) refidx = 31;
   tlw = list ? sh->wp1_lw : sh->wp_lw; tlo = list ? sh->wp1_lo : sh->wp_lo;
   tcw = list ? sh->wp1_cw : sh->wp_cw; tco = list ? sh->wp1_co : sh->wp_co;
   lw = tlw[refidx]; lo = RH264_BDS(f, tlo[refidx]); ld = sh->luma_log2_denom;
   /* identity weights skipped, as in rh264_weight_pred */
   if (lw != (1 << ld) || lo)
      for (y = 0; y < bh; y++)
         rh264_wp_row(f, f->Y + RH264_OFF(f, (oy+y)*f->ystride + ox), bw, lw,
               ld >= 1 ? 1 << (ld-1) : 0, ld, lo);
   {
      int c422 = (f->cmbh == 16) && !f->c444;
      int cox = f->c444 ? ox : (ox >> 1);
      int coy = (f->c444 || c422) ? oy : (oy >> 1);
      int cbw = f->c444 ? bw : (bw >> 1);
      int cbh = (f->c444 || c422) ? bh : (bh >> 1), c;
      int cd = sh->chroma_log2_denom;
      uint8_t *planes[2];
      planes[0] = f->U; planes[1] = f->V;
      for (c = 0; c < 2; c++)
      {
         int cw = tcw[refidx][c], co = RH264_BDS(f, tco[refidx][c]);
         if (cw == (1 << cd) && !co)
            continue;
         for (y = 0; y < cbh; y++)
            rh264_wp_row(f, planes[c] + RH264_OFF(f, (coy+y)*f->cstride + cox), cbw, cw,
                  cd >= 1 ? 1 << (cd-1) : 0, cd, co);
      }
   }
}

/* Predict one block from (r0,mv0) and/or (r1,mv1); a list is unused when its
 * reference index is negative. Combines per weighted_bipred_idc: default
 * rounding average, explicit tables, or implicit POC-derived weights, which
 * apply to bi-predicted blocks only (8.4.2.3). */
static void rh264_b_pred_block(rh264_frame *f, const rh264_bctx *bc,
      const rh264_slice_hdr *sh, int mbx, int mby, int bx, int by,
      int bw, int bh, int r0, int mv0x, int mv0y, int r1, int mv1x, int mv1y)
{
   int ox = mbx*16 + bx, oy = mby*16 + by;
   if (r0 >= 0 && r1 >= 0)
   {
      /* chroma temporaries hold 8x8 for 4:2:0, 8x16 for 4:2:2 and
       * 16x16 (stride 16) for 4:4:4; all six live in the frame's
       * scratch arena (rh264_frame.scr_bipred) rather than on the stack */
      uint8_t *t0y = f->scr_bipred,                     *t0u = t0y + RH264_OFF(f, 256), *t0v = t0y + RH264_OFF(f, 512);
      uint8_t *t1y = f->scr_bipred + RH264_OFF(f, 768), *t1u = t1y + RH264_OFF(f, 256), *t1v = t1y + RH264_OFF(f, 512);
      int c;
      int c444 = f->c444;
      int c422 = (f->cmbh == 16) && !c444;
      int cs  = c444 ? 16 : 8;
      int cox = c444 ? ox : (ox >> 1), coy = (c422 || c444) ? oy : (oy >> 1);
      int cbw = c444 ? bw : (bw >> 1), cbh = (c422 || c444) ? bh : (bh >> 1);
      rh264_b_mc_tmp(f, t0y, t0u, t0v, bc->l0[r0], ox, oy, bw, bh, mv0x, mv0y,
            f->field, c422, c444);
      rh264_b_mc_tmp(f, t1y, t1u, t1v, bc->l1[r1], ox, oy, bw, bh, mv1x, mv1y,
            f->field, c422, c444);
      if (bc->wbidc == 1 && sh->wp_valid)
      {
         int ld = sh->luma_log2_denom, cd = sh->chroma_log2_denom;
         int w0 = sh->wp_lw[r0 > 31 ? 31 : r0], w1 = sh->wp1_lw[r1 > 31 ? 31 : r1];
         /* offsets in the sample domain (<< (bd - 8)) before averaging */
         int o  = (RH264_BDS(f, sh->wp_lo[r0 > 31 ? 31 : r0])
                 + RH264_BDS(f, sh->wp1_lo[r1 > 31 ? 31 : r1]) + 1) >> 1;
         /* both weights 2^denom and no offset make it the plain
          * rounded average, ((a + b) 2^denom + 2^denom) >> (denom + 1) */
         if (w0 == (1 << ld) && w1 == w0 && !o)
            rh264_avg_block(f, f->Y + RH264_OFF(f, oy*f->ystride + ox),
                  f->ystride, t0y, 16, t1y, 16, bw, bh);
         else
            rh264_bi_block(f, f->Y + RH264_OFF(f, oy*f->ystride + ox), f->ystride,
                  t0y, 16, t1y, 16, bw, bh, w0, w1, 1 << ld, ld + 1, o);
         for (c = 0; c < 2; c++)
         {
            const uint8_t *s0 = c ? t0v : t0u, *s1 = c ? t1v : t1u;
            uint8_t *pl = c ? f->V : f->U;
            int cw0 = sh->wp_cw[r0 > 31 ? 31 : r0][c];
            int cw1 = sh->wp1_cw[r1 > 31 ? 31 : r1][c];
            int co  = (RH264_BDS(f, sh->wp_co[r0 > 31 ? 31 : r0][c])
                     + RH264_BDS(f, sh->wp1_co[r1 > 31 ? 31 : r1][c]) + 1) >> 1;
            if (cw0 == (1 << cd) && cw1 == cw0 && !co)
               rh264_avg_block(f, pl + RH264_OFF(f, coy*f->cstride + cox),
                     f->cstride, s0, cs, s1, cs, cbw, cbh);
            else
               rh264_bi_block(f, pl + RH264_OFF(f, coy*f->cstride + cox), f->cstride,
                     s0, cs, s1, cs, cbw, cbh, cw0, cw1, 1 << cd, cd + 1, co);
         }
      }
      else if (bc->wbidc == 2)
      {
         int w1 = bc->w1imp[r0 > 31 ? 31 : r0][r1 > 31 ? 31 : r1];
         int w0 = 64 - w1;
         if (w1 == 32)
         {
            /* equal implicit weights: (32a + 32b + 32) >> 6, the plain
             * rounded average */
            rh264_avg_block(f, f->Y + RH264_OFF(f, oy*f->ystride + ox),
                  f->ystride, t0y, 16, t1y, 16, bw, bh);
            for (c = 0; c < 2; c++)
               rh264_avg_block(f, (c ? f->V : f->U)
                     + RH264_OFF(f, coy*f->cstride + cox), f->cstride,
                     c ? t0v : t0u, cs, c ? t1v : t1u, cs, cbw, cbh);
         }
         else
         {
            rh264_bi_block(f, f->Y + RH264_OFF(f, oy*f->ystride + ox), f->ystride,
                  t0y, 16, t1y, 16, bw, bh, w0, w1, 32, 6, 0);
            for (c = 0; c < 2; c++)
            {
               const uint8_t *s0 = c ? t0v : t0u, *s1 = c ? t1v : t1u;
               uint8_t *pl = c ? f->V : f->U;
               rh264_bi_block(f, pl + RH264_OFF(f, coy*f->cstride + cox), f->cstride,
                     s0, cs, s1, cs, cbw, cbh, w0, w1, 32, 6, 0);
            }
         }
      }
      else
      {
         rh264_avg_block(f, f->Y + RH264_OFF(f, oy*f->ystride + ox), f->ystride,
               t0y, 16, t1y, 16, bw, bh);
         for (c = 0; c < 2; c++)
            rh264_avg_block(f, (c ? f->V : f->U) + RH264_OFF(f, coy*f->cstride + cox),
                  f->cstride, c ? t0v : t0u, cs, c ? t1v : t1u, cs,
                  cbw, cbh);
      }
   }
   else if (r0 >= 0)
   {
      rh264_inter_pred_block(f, bc->l0[r0], mbx, mby, bx, by, bw, bh,
            mv0x, mv0y);
      if (bc->wbidc == 1)
         rh264_weight_pred_list(f, sh, 0, r0, mbx, mby, bx, by, bw, bh);
   }
   else if (r1 >= 0)
   {
      rh264_inter_pred_block(f, bc->l1[r1], mbx, mby, bx, by, bw, bh,
            mv1x, mv1y);
      if (bc->wbidc == 1)
         rh264_weight_pred_list(f, sh, 1, r1, mbx, mby, bx, by, bw, bh);
   }
}

/* Round a 4x4 grid coordinate to the corner block of its 8x8 when
 * direct_8x8_inference is on (the RSD rule). */
static RH264_INLINE int rh264_rsd(int x)
{ return (x & 2) ? (x | 1) : (x & ~1); }

/* Co-located "not moving" test for spatial direct (8.4.1.2.2): the block in
 * RefPicList1[0] uses reference 0 of one of its own lists with a vector
 * within one quarter-pel. */
static RH264_INLINE int rh264_col_zero(const rh264_mv *c)
{
   /* evaluated whole rather than branched on: whether the co-located
    * block sits still is as unpredictable as the picture.  A component
    * within one quarter-pel is v + 1 in 0..2 */
   unsigned z0 = (unsigned)(c->ref == 0)
               & (unsigned)((unsigned)(c->mvx + 1) <= 2u)
               & (unsigned)((unsigned)(c->mvy + 1) <= 2u);
   unsigned z1 = (unsigned)(c->ref == -1) & (unsigned)(c->ref1 == 0)
               & (unsigned)((unsigned)(c->mvx1 + 1) <= 2u)
               & (unsigned)((unsigned)(c->mvy1 + 1) <= 2u);
   return (int)(z0 | z1);
}

/* Spatial direct per-macroblock preparation (8.4.1.2.2): the minimum
 * non-negative reference of the three MV predictor neighbours per list, and
 * the 16x16 median predictor toward it. */
static void rh264_b_direct_prepare(const rh264_bctx *bc, const rh264_mv *mvg,
      int gwmax, int ghmax, int mbx, int mby,
      int *l0r, int *l1r, int *pm0x, int *pm0y, int *pm1x, int *pm1y)
{
   int gx = mbx * 4, gy = mby * 4;
   const rh264_mv *A = rh264_mv_at(mvg, gwmax, gx - 1, gy,     gwmax, ghmax);
   const rh264_mv *B = rh264_mv_at(mvg, gwmax, gx,     gy - 1, gwmax, ghmax);
   const rh264_mv *C = rh264_mv_at(mvg, gwmax, gx + 4, gy - 1, gwmax, ghmax);
   const rh264_mv *D = rh264_mv_at(mvg, gwmax, gx - 1, gy - 1, gwmax, ghmax);
   int list;
   if (!C || C->ref == -2) C = D;
   for (list = 0; list < 2; list++)
   {
      int ra = (A && A->ref != -2) ? rh264_cell_ref(A, list) : -1;
      int rb = (B && B->ref != -2) ? rh264_cell_ref(B, list) : -1;
      int rc = (C && C->ref != -2) ? rh264_cell_ref(C, list) : -1;
      unsigned ua = (unsigned char)(signed char)ra;
      unsigned ub = (unsigned char)(signed char)rb;
      unsigned uc = (unsigned char)(signed char)rc;
      unsigned um = ua < ub ? ua : ub;
      int r;
      if (uc < um) um = uc;
      r = (signed char)um;
      if (list == 0) *l0r = r; else *l1r = r;
      if (r >= 0)
         rh264_pred_mv_ldir(mvg, gwmax, gwmax, ghmax, gx, gy, 4, r,
               RH264_MVP_MEDIAN, list, list == 0 ? pm0x : pm1x,
               list == 0 ? pm0y : pm1y);
   }
}

/* Derive and store the motion of one direct 4x4 run (a full 8x8 quadrant
 * under direct_8x8_inference, a single 4x4 otherwise). Returns 0, or -1 for
 * a temporal mapping the current list 0 cannot satisfy. */
static int rh264_b_direct_cells(const rh264_bctx *bc, rh264_mv *mvg,
      int gwmax, int mbx, int mby, int bx4, int by4, int n4,
      int l0r, int l1r, int pm0x, int pm0y, int pm1x, int pm1y)
{
   int gx = mbx * 4 + bx4, gy = mby * 4 + by4;
   int cgx = bc->d8x8 ? mbx * 4 + rh264_rsd(bx4) : gx;
   int cgy = bc->d8x8 ? mby * 4 + rh264_rsd(by4) : gy;
   const rh264_mv *col = &bc->colg[cgy * gwmax + cgx];
   /* every cell of the run gets the same motion: one cell is built,
    * both lists, and copied over the run */
   rh264_mv z;
   int x, y;
   memset(&z, 0, sizeof(z));
   z.dir = 1;   /* direct blocks are left out of their neighbours'
                 * reference-index contexts (9.3.3.1.1.6) */
   if (bc->direct_spatial)
   {
      int r0 = l0r, r1 = l1r;
      int m0x = pm0x, m0y = pm0y, m1x = pm1x, m1y = pm1y;
      if (r0 < 0 && r1 < 0) { r0 = 0; r1 = 0; m0x = m0y = m1x = m1y = 0; }
      else
      {
         int colzero = rh264_col_zero(col);
         if (r0 < 0) { m0x = m0y = 0; }
         else if (colzero && r0 == 0) { m0x = m0y = 0; }
         if (r1 < 0) { m1x = m1y = 0; }
         else if (colzero && r1 == 0) { m1x = m1y = 0; }
      }
      z.ref = (int8_t)r0;
      if (r0 >= 0)
      {
         z.mvx = (int16_t)m0x; z.mvy = (int16_t)m0y;
         z.pic = (int8_t)bc->pid0[r0]; z.refpoc0 = bc->l0poc[r0];
      }
      else
         z.pic = -1;
      z.ref1 = (int8_t)r1;
      if (r1 >= 0)
      {
         z.mvx1 = (int16_t)m1x; z.mvy1 = (int16_t)m1y;
         z.pic1 = (int8_t)bc->pid1[r1]; z.refpoc1 = bc->l1poc[r1];
      }
      else
         z.pic1 = -1;
   }
   else
   {
      /* Temporal (8.4.1.2.3): scale the co-located vector by the POC
       * distances; its reference maps into the current list 0 by picture. */
      int cref, cmx = 0, cmy = 0, cpoc = 0;
      if (col->ref >= 0)
      { cref = col->ref; cmx = col->mvx; cmy = col->mvy; cpoc = col->refpoc0; }
      else if (col->ref1 >= 0)
      { cref = col->ref1; cmx = col->mvx1; cmy = col->mvy1; cpoc = col->refpoc1; }
      else cref = -1;
      if (cref < 0)
      {
         z.ref  = 0; z.pic  = (int8_t)bc->pid0[0]; z.refpoc0 = bc->l0poc[0];
         z.ref1 = 0; z.pic1 = (int8_t)bc->pid1[0]; z.refpoc1 = bc->l1poc[0];
      }
      else
      {
         int mi = -1, i;
         int m0x, m0y, m1x, m1y;
         for (i = 0; i < bc->n0; i++)
            if (bc->l0poc[i] == cpoc) { mi = i; break; }
         if (mi < 0) return -1;
         if (bc->mvscale[mi > 31 ? 31 : mi] == 9999)
         { m0x = cmx; m0y = cmy; m1x = 0; m1y = 0; }
         else
         {
            int sc = bc->mvscale[mi > 31 ? 31 : mi];
            m0x = (sc * cmx + 128) >> 8;
            m0y = (sc * cmy + 128) >> 8;
            m1x = m0x - cmx;
            m1y = m0y - cmy;
         }
         z.mvx  = (int16_t)m0x; z.mvy  = (int16_t)m0y;
         z.ref  = (int8_t)mi;   z.pic  = (int8_t)bc->pid0[mi];
         z.refpoc0 = bc->l0poc[mi];
         z.mvx1 = (int16_t)m1x; z.mvy1 = (int16_t)m1y;
         z.ref1 = 0;            z.pic1 = (int8_t)bc->pid1[0];
         z.refpoc1 = bc->l1poc[0];
      }
   }
   for (y = 0; y < n4; y++)
   {
      rh264_mv *row = &mvg[(gy + y) * gwmax + gx];
      for (x = 0; x < n4; x++)
         row[x] = z;
   }
   return 0;
}

/* Fill every direct 4x4 of one 8x8 quadrant and motion compensate it from
 * the stored cells. Used by B_Skip, B_Direct_16x16 and direct 8x8 subs. */
/* Direct cells within a region carry the same motion whenever the
 * derivation was uniform - always per quadrant under 8x8 inference,
 * and across the whole macroblock for the common static/skip case.
 * Equal list refs imply equal reference pictures and equal weights,
 * so equal (ref, mv) pairs make the compensation itself identical
 * and it can run once over the widest uniform block. */
static RH264_INLINE int rh264_mv_same(const rh264_mv *a, const rh264_mv *b)
{
   /* the four vector components as one word, the comparisons combined
    * without branching */
   uint64_t va, vb;
   memcpy(&va, &a->mvx, 8);
   memcpy(&vb, &b->mvx, 8);
   return (va == vb) & (a->ref == b->ref) & (a->ref1 == b->ref1);
}

/* Compensate one quadrant off its cells: one 8x8 block when the four
 * cells agree, per 4x4 otherwise. */
static void rh264_b_comp_quadrant(rh264_frame *f, const rh264_bctx *bc,
      const rh264_slice_hdr *sh, const rh264_mv *mvg, int gwmax,
      int mbx, int mby, int qx, int qy)
{
   const rh264_mv *m0 = &mvg[(mby*4 + qy*2) * gwmax + mbx*4 + qx*2];
   int sx, sy;
   if (rh264_mv_same(m0, m0 + 1)
         && rh264_mv_same(m0, m0 + gwmax)
         && rh264_mv_same(m0, m0 + gwmax + 1))
   {
      rh264_b_pred_block(f, bc, sh, mbx, mby, qx * 8, qy * 8, 8, 8,
            m0->ref, m0->mvx, m0->mvy, m0->ref1, m0->mvx1, m0->mvy1);
      return;
   }
   for (sy = 0; sy < 2; sy++)
      for (sx = 0; sx < 2; sx++)
      {
         const rh264_mv *m = &mvg[(mby*4 + qy*2 + sy) * gwmax
                                 + mbx*4 + qx*2 + sx];
         rh264_b_pred_block(f, bc, sh, mbx, mby,
               (qx*2 + sx) * 4, (qy*2 + sy) * 4, 4, 4,
               m->ref, m->mvx, m->mvy, m->ref1, m->mvx1, m->mvy1);
      }
}

/* Compensate a whole macroblock off its 4x4 cells at the widest uniform
 * granularity: one 16x16 block when all sixteen cells agree (static
 * areas, skips), else per quadrant. */
static void rh264_b_comp_mb(rh264_frame *f, const rh264_bctx *bc,
      const rh264_slice_hdr *sh, const rh264_mv *mvg, int gwmax,
      int mbx, int mby)
{
   const rh264_mv *m0 = &mvg[(mby*4) * gwmax + mbx*4];
   int x, y, q, uni = 1;
   for (y = 0; y < 4 && uni; y++)
      for (x = 0; x < 4 && uni; x++)
         uni = rh264_mv_same(m0, &m0[y * gwmax + x]);
   if (uni)
   {
      rh264_b_pred_block(f, bc, sh, mbx, mby, 0, 0, 16, 16,
            m0->ref, m0->mvx, m0->mvy, m0->ref1, m0->mvx1, m0->mvy1);
      return;
   }
   for (q = 0; q < 4; q++)
      rh264_b_comp_quadrant(f, bc, sh, mvg, gwmax, mbx, mby, q & 1, q >> 1);
}

/* Derive one direct quadrant's cells.  Compensation happens at the
 * macroblock level (rh264_b_comp_mb) so uniform motion merges into the
 * widest possible block. */
static int rh264_b_direct_quadrant(const rh264_bctx *bc,
      rh264_mv *mvg, int gwmax, int mbx, int mby,
      int qx, int qy, int l0r, int l1r, int pm0x, int pm0y, int pm1x, int pm1y)
{
   int sx, sy;
   int step = bc->d8x8 ? 2 : 1;
   for (sy = 0; sy < 2; sy += step)
      for (sx = 0; sx < 2; sx += step)
      {
         if (rh264_b_direct_cells(bc, mvg, gwmax, mbx, mby,
               qx * 2 + sx, qy * 2 + sy, step, l0r, l1r,
               pm0x, pm0y, pm1x, pm1y) != 0)
            return -1;
      }
   return 0;
}

/* Direct prediction of a whole macroblock (B_Skip / B_Direct_16x16). */
static int rh264_b_direct_mb(rh264_frame *f, const rh264_bctx *bc,
      const rh264_slice_hdr *sh, rh264_mv *mvg, int gwmax, int ghmax,
      int mbx, int mby)
{
   int l0r = -1, l1r = -1, pm0x = 0, pm0y = 0, pm1x = 0, pm1y = 0, q;
   if (bc->direct_spatial)
      rh264_b_direct_prepare(bc, mvg, gwmax, ghmax, mbx, mby,
            &l0r, &l1r, &pm0x, &pm0y, &pm1x, &pm1y);
   for (q = 0; q < 4; q++)
      if (rh264_b_direct_quadrant(bc, mvg, gwmax, mbx, mby,
            q & 1, q >> 1, l0r, l1r, pm0x, pm0y, pm1x, pm1y) != 0)
         return -1;
   rh264_b_comp_mb(f, bc, sh, mvg, gwmax, mbx, mby);
   return 0;
}


/* B macroblock and sub-macroblock types (Tables 7-14 and 7-18), reduced to a
 * partition shape and a per-partition prediction usage: 0 = list 0 only,
 * 1 = list 1 only, 2 = both, 3 = direct. */
#define RH264_BSHAPE_D16   0  /* B_Direct_16x16 */
#define RH264_BSHAPE_16x16 1
#define RH264_BSHAPE_16x8  2
#define RH264_BSHAPE_8x16  3
#define RH264_BSHAPE_8x8   4

static const int8_t rh264_b_pdir16[4]    = {3, 0, 1, 2};
/* mb_type 4..21: shape alternates 16x8/8x16 within each pair, the
 * prediction-usage pair advances every second type. */
static const int8_t rh264_b_pdir2[9][2] = {
   {0,0},{1,1},{0,1},{1,0},{0,2},{1,2},{2,0},{2,1},{2,2}};
/* sub_mb_type: shape (8x8/8x4/4x8/4x4 as partition W,H) + usage */
static const int8_t rh264_b_sub_w[13]    = {8, 8, 8, 8, 8, 4, 8, 4, 8, 4, 4, 4, 4};
static const int8_t rh264_b_sub_h[13]    = {8, 8, 8, 8, 4, 8, 4, 8, 4, 8, 4, 4, 4};
static const int8_t rh264_b_sub_pdir[13] = {3, 0, 1, 2, 0, 0, 1, 1, 2, 2, 0, 1, 2};

/* One non-direct B partition, both lists resolved: predict per list off the
 * grid, fill the cells, compensate. Refs below zero mean the list is unused. */
static void rh264_b_part(rh264_frame *f, const rh264_bctx *bc,
      const rh264_slice_hdr *sh, rh264_mv *mvg, int gwmax, int ghmax,
      int mbx, int mby, int bx, int by, int bw, int bh,
      int r0, int mvd0x, int mvd0y, int r1, int mvd1x, int mvd1y,
      int hint, int *m0x, int *m0y, int *m1x, int *m1y)
{
   int gx = mbx*4 + (bx>>2), gy = mby*4 + (by>>2);
   int bw4 = bw>>2, bh4 = bh>>2;
   int pmx, pmy;
   *m0x = *m0y = *m1x = *m1y = 0;
   if (r0 >= 0)
   {
      rh264_pred_mv_ldir(mvg, gwmax, gwmax, ghmax, gx, gy, bw4, r0, hint, 0,
            &pmx, &pmy);
      *m0x = pmx + mvd0x; *m0y = pmy + mvd0y;
   }
   rh264_mv_fill_list(mvg, gwmax, gx, gy, bw4, bh4, 0,
         *m0x, *m0y, r0, r0 < 0 ? -1 : bc->pid0[r0],
         r0 < 0 ? 0 : bc->l0poc[r0], 0);
   if (r1 >= 0)
   {
      rh264_pred_mv_ldir(mvg, gwmax, gwmax, ghmax, gx, gy, bw4, r1, hint, 1,
            &pmx, &pmy);
      *m1x = pmx + mvd1x; *m1y = pmy + mvd1y;
   }
   rh264_mv_fill_list(mvg, gwmax, gx, gy, bw4, bh4, 1,
         *m1x, *m1y, r1, r1 < 0 ? -1 : bc->pid1[r1],
         r1 < 0 ? 0 : bc->l1poc[r1], 0);
}

/* Full B-slice decode, CAVLC entropy. Mirrors rh264_decode_pslice in
 * structure; motion syntax follows 7.3.5.1/7.3.5.2: every reference index
 * of list 0 then list 1, then every vector difference in the same order.
 * Direct blocks derive their motion before any of that is read, so the MV
 * predictors of following partitions see them (8.4.1.2). */
static int rh264_decode_bslice(rh264_bits *b, const rh264_sps *sps,
      const rh264_pps *pps, rh264_slice_hdr *sh, rh264_frame *f,
      const rh264_bctx *bc, rh264_mv *mvg, int *end_mb)
{
   int mbaddr = sh->first_mb_in_slice, total = f->mbw * f->mbh;
   int gw = f->mbw * 4, cgw = f->mbw * 2;
   int gwmax = f->mbw * 4, ghmax = f->mbh * 4;
   int prev_skipped = 0;
   int skip_run = -1;
   f->qp = sh->slice_qp;
   f->chroma_qp_offset = pps->chroma_qp_index_offset;
   f->constrained_intra = pps->constrained_intra_pred_flag;
   f->chroma_qp_offset2 = pps->chroma_qp_index_offset2;
   f->tb = sps->tb;
   if (bc->n0 < 1 || bc->n1 < 1) return -1;

   /* the coefficient/mode context describes the picture being decoded; a
    * continuation slice must keep what earlier slices of it produced */
   if (sh->first_mb_in_slice == 0)
   {
      memset(f->nzL, 0, (size_t)gw * f->mbh * 4);
      memset(f->mbt8, 0, (size_t)f->mbw * f->mbh);
      memset(f->nzC[0], 0, (size_t)cgw * f->mbh * (f->cmbh/4));
      memset(f->nzC[1], 0, (size_t)cgw * f->mbh * (f->cmbh/4));
      memset(f->i4mode, 0xff, (size_t)gw * f->mbh * 4);
   }

   while (mbaddr < total)
   {
      int mbx, mby;
      int mb_type;
      rh264_mb_pos(mbaddr, f->mbw, f->mbaff, &mbx, &mby);
      /* temporal direct reads RefPicList1[0]'s co-located cells of this
       * row, from a picture that may still be decoding: its motion is
       * final with the row's samples, so the same wait, for this row
       * and the pair row under it, once per macroblock (it returns at
       * once when the rows are there) */
      if (bc->n1 > 0 && bc->l1[0])
         rh264_ref_wait_rows(f, bc->l1[0], mby * 16, 32, 0);

      /* no further run or macroblock once the slice's RBSP is exhausted */
      if (skip_run <= 0 && !rh264_more_rbsp(b)) break;
      if (skip_run < 0) skip_run = rh264_ue(b);
      if (skip_run > 0)
      {
         /* B_Skip: direct prediction of the whole macroblock, no residual. */
         if (rh264_b_direct_mb(f, bc, sh, mvg, gwmax, ghmax, mbx, mby) != 0)
            return -1;
         rh264_inter_clear_i4mode(f, mbx, mby);
         { int cx, cy; for (cy = 0; cy < 4; cy++) for (cx = 0; cx < 4; cx++)
              f->nzL[(mby * 4 + cy) * gw + mbx * 4 + cx] = 0; }
         rh264_nzc_clear(f, mbx, mby);
         f->mbqp[mby * f->mbw + mbx] = (uint8_t)RH264_QPP(f);
         skip_run--; mbaddr++;
         prev_skipped = 1;
         continue;
      }
      skip_run = -1;
      /* see the P-slice loop: the pair flag precedes the top macroblock,
       * or the bottom one when the top was skipped (7.3.4) */
      if (f->mbaff && (!(mbaddr & 1) || prev_skipped) && rh264_u1(b))
         return -1;
      prev_skipped = 0;
      mb_type = rh264_ue(b);
      /* B mb_type is 0..22 for the inter types and 23..48 for the intra
       * ones (Table 7-14).  An over-long code returns 0xFFFFFFFF from
       * rh264_ue, which lands in the int as -1: too small for the intra
       * branch below, but accepted by the mb_type <= 3 case, where it
       * indexes rh264_b_pdir16[-1]. */
      if ((unsigned)mb_type > 48) return -3;

      if (mb_type >= 23)
      {
         /* Intra macroblock in a B slice: offset by 23 (Table 7-14). */
         int imb = mb_type - 23;
         int cx, cy;
         if (rh264_decode_intra_mb_cavlc(b, f, mbx, mby, imb,
               pps->transform_8x8_mode, sh->first_mb_in_slice) < 0) return -1;
         for (cy = 0; cy < 4; cy++) for (cx = 0; cx < 4; cx++)
         {
            rh264_mv *m = &mvg[(mby * 4 + cy) * gwmax + mbx * 4 + cx];
            memset(m, 0, sizeof(*m));
            m->ref = -1; m->pic = -1; m->ref1 = -1; m->pic1 = -1; m->intra = 1;
         }
         f->mbqp[mby * f->mbw + mbx] = (uint8_t)RH264_QPP(f);
      }
      else
      {
         int cbp, cbp_luma, cbp_chroma;
         int t8 = 0, t8ok;
         uint8_t *u = f->U + RH264_OFF(f, (mby * f->cmbh) * f->cstride + mbx * 8);
         uint8_t *v = f->V + RH264_OFF(f, (mby * f->cmbh) * f->cstride + mbx * 8);
         rh264_inter_clear_i4mode(f, mbx, mby);
         /* noSubMbPartSizeLessThan8x8Flag (7.3.5): direct prediction
          * qualifies only under direct_8x8_inference; B_8x8 refines this
          * per sub-partition below. */
         t8ok = (mb_type >= 1 && mb_type <= 21) ? 1
              : (mb_type == 0 ? bc->d8x8 : 0);

         if (mb_type == 0)
         {
            if (rh264_b_direct_mb(f, bc, sh, mvg, gwmax, ghmax, mbx, mby) != 0)
               return -1;
         }
         else if (mb_type <= 3)
         {
            /* 16x16 single partition */
            int pd = rh264_b_pdir16[mb_type];
            int r0 = -1, r1 = -1, d0x = 0, d0y = 0, d1x = 0, d1y = 0;
            int m0x, m0y, m1x, m1y;
            if (pd == 0 || pd == 2) r0 = rh264_ref_idx_te(b, bc->n0);
            if (pd == 1 || pd == 2) r1 = rh264_ref_idx_te(b, bc->n1);
            if (r0 >= 0) { d0x = rh264_se(b); d0y = rh264_se(b); }
            if (r1 >= 0) { d1x = rh264_se(b); d1y = rh264_se(b); }
            rh264_b_part(f, bc, sh, mvg, gwmax, ghmax, mbx, mby, 0, 0, 16, 16,
                  r0, d0x, d0y, r1, d1x, d1y, RH264_MVP_MEDIAN,
                  &m0x, &m0y, &m1x, &m1y);
            rh264_b_pred_block(f, bc, sh, mbx, mby, 0, 0, 16, 16,
                  r0, m0x, m0y, r1, m1x, m1y);
         }
         else if (mb_type <= 21)
         {
            /* Two partitions, 16x8 when the offset is even, 8x16 odd. */
            int is16x8 = ((mb_type & 1) == 0);
            int pd0 = rh264_b_pdir2[(mb_type - 4) >> 1][0];
            int pd1 = rh264_b_pdir2[(mb_type - 4) >> 1][1];
            int r0a = -1, r0b = -1, r1a = -1, r1b = -1;
            int d0[4], d1[4];
            int pi;
            int hints[2];
            int m0[4], m1[4];
            hints[0] = is16x8 ? RH264_MVP_B : RH264_MVP_A;
            hints[1] = is16x8 ? RH264_MVP_A : RH264_MVP_C;
            if (pd0 == 0 || pd0 == 2) r0a = rh264_ref_idx_te(b, bc->n0);
            if (pd1 == 0 || pd1 == 2) r0b = rh264_ref_idx_te(b, bc->n0);
            if (pd0 == 1 || pd0 == 2) r1a = rh264_ref_idx_te(b, bc->n1);
            if (pd1 == 1 || pd1 == 2) r1b = rh264_ref_idx_te(b, bc->n1);
            d0[0] = d0[1] = d0[2] = d0[3] = 0;
            d1[0] = d1[1] = d1[2] = d1[3] = 0;
            if (r0a >= 0) { d0[0] = rh264_se(b); d0[1] = rh264_se(b); }
            if (r0b >= 0) { d0[2] = rh264_se(b); d0[3] = rh264_se(b); }
            if (r1a >= 0) { d1[0] = rh264_se(b); d1[1] = rh264_se(b); }
            if (r1b >= 0) { d1[2] = rh264_se(b); d1[3] = rh264_se(b); }
            for (pi = 0; pi < 2; pi++)
            {
               int bx = is16x8 ? 0 : pi * 8, by = is16x8 ? pi * 8 : 0;
               int bw = is16x8 ? 16 : 8,     bh = is16x8 ? 8 : 16;
               int rr0 = pi ? r0b : r0a, rr1 = pi ? r1b : r1a;
               rh264_b_part(f, bc, sh, mvg, gwmax, ghmax, mbx, mby,
                     bx, by, bw, bh, rr0, d0[pi*2], d0[pi*2+1],
                     rr1, d1[pi*2], d1[pi*2+1], hints[pi],
                     &m0[pi*2], &m0[pi*2+1], &m1[pi*2], &m1[pi*2+1]);
               rh264_b_pred_block(f, bc, sh, mbx, mby, bx, by, bw, bh,
                     rr0, m0[pi*2], m0[pi*2+1], rr1, m1[pi*2], m1[pi*2+1]);
            }
         }
         else
         {
            /* B_8x8: sub types, direct derivation, then all list 0 refs,
             * all list 1 refs, all list 0 mvds, all list 1 mvds. */
            int sub[4], rf0[4], rf1[4], p;
            int l0r = -1, l1r = -1, pm0x = 0, pm0y = 0, pm1x = 0, pm1y = 0;
            int need_prepare = 0;
            int16_t sd0[4][8], sd1[4][8];  /* mvd pairs, up to 4 per sub */
            for (p = 0; p < 4; p++)
            {
               sub[p] = rh264_ue(b);
               if ((unsigned)sub[p] > 12) return -3;
               if (rh264_b_sub_pdir[sub[p]] == 3) need_prepare = 1;
            }
            t8ok = 1;
            for (p = 0; p < 4; p++)
            {
               if (rh264_b_sub_pdir[sub[p]] == 3)
               { if (!bc->d8x8) t8ok = 0; }
               else if (rh264_b_sub_w[sub[p]] != 8
                     || rh264_b_sub_h[sub[p]] != 8)
                  t8ok = 0;
            }
            if (need_prepare)
            {
               if (bc->direct_spatial)
                  rh264_b_direct_prepare(bc, mvg, gwmax, ghmax, mbx, mby,
                        &l0r, &l1r, &pm0x, &pm0y, &pm1x, &pm1y);
               for (p = 0; p < 4; p++)
                  if (rh264_b_sub_pdir[sub[p]] == 3)
                  {
                     int sx, sy;
                     int step = bc->d8x8 ? 2 : 1;
                     for (sy = 0; sy < 2; sy += step)
                        for (sx = 0; sx < 2; sx += step)
                           if (rh264_b_direct_cells(bc, mvg, gwmax, mbx, mby,
                                 (p & 1) * 2 + sx, (p >> 1) * 2 + sy, step,
                                 l0r, l1r, pm0x, pm0y, pm1x, pm1y) != 0)
                              return -1;
                  }
            }
            for (p = 0; p < 4; p++)
            {
               int pd = rh264_b_sub_pdir[sub[p]];
               rf0[p] = (pd == 0 || pd == 2) ? rh264_ref_idx_te(b, bc->n0) : -1;
            }
            for (p = 0; p < 4; p++)
            {
               int pd = rh264_b_sub_pdir[sub[p]];
               rf1[p] = (pd == 1 || pd == 2) ? rh264_ref_idx_te(b, bc->n1) : -1;
            }
            /* All vector differences are read first, in stream order
             * (7.3.5.2): they carry no prediction. Motion then resolves one
             * 8x8 at a time in decode order, both lists together, so the MV
             * predictor of a sub-block sees earlier sub-blocks resolved and
             * later non-direct ones still undecoded, which is what makes
             * the C-to-D neighbour substitution fire correctly (6.4.11.7,
             * 8.4.1.3). Direct sub-blocks were derived before any of this
             * and count as resolved throughout. */
            for (p = 0; p < 4; p++)
            {
               int k = 64 / (rh264_b_sub_w[sub[p]] * rh264_b_sub_h[sub[p]]);
               int q;
               if (rf0[p] >= 0)
                  for (q = 0; q < k; q++)
                  { sd0[p][q*2] = (int16_t)rh264_se(b);
                    sd0[p][q*2+1] = (int16_t)rh264_se(b); }
            }
            for (p = 0; p < 4; p++)
            {
               int k = 64 / (rh264_b_sub_w[sub[p]] * rh264_b_sub_h[sub[p]]);
               int q;
               if (rf1[p] >= 0)
                  for (q = 0; q < k; q++)
                  { sd1[p][q*2] = (int16_t)rh264_se(b);
                    sd1[p][q*2+1] = (int16_t)rh264_se(b); }
            }
            for (p = 0; p < 4; p++)
            {
               int sw = rh264_b_sub_w[sub[p]], shh = rh264_b_sub_h[sub[p]];
               int px = (p & 1) * 8, py = (p >> 1) * 8;
               int list;
               if (rh264_b_sub_pdir[sub[p]] == 3) continue;
               for (list = 0; list < 2; list++)
               {
                  int rf = list ? rf1[p] : rf0[p];
                  const int16_t *sd = list ? sd1[p] : sd0[p];
                  int q = 0, oy, ox;
                  for (oy = 0; oy < 8; oy += shh)
                     for (ox = 0; ox < 8; ox += sw)
                     {
                        int gx = mbx*4 + ((px+ox)>>2), gy = mby*4 + ((py+oy)>>2);
                        int pmx = 0, pmy = 0, mx = 0, my = 0;
                        if (rf >= 0)
                        {
                           rh264_pred_mv_ldir(mvg, gwmax, gwmax, ghmax, gx, gy,
                                 sw>>2, rf, RH264_MVP_MEDIAN, list, &pmx, &pmy);
                           mx = pmx + sd[q*2]; my = pmy + sd[q*2+1];
                        }
                        rh264_mv_fill_list(mvg, gwmax, gx, gy, sw>>2, shh>>2,
                              list, mx, my, rf,
                              rf < 0 ? -1 : (list ? bc->pid1[rf] : bc->pid0[rf]),
                              rf < 0 ? 0 : (list ? bc->l1poc[rf] : bc->l0poc[rf]),
                              0);
                        q++;
                     }
               }
            }
            /* compensate off the cells, widest uniform block first
             * (direct subs included) */
            rh264_b_comp_mb(f, bc, sh, mvg, gwmax, mbx, mby);
         }

         cbp = rh264_ue(b);
         if (f->c444) { if ((unsigned)cbp >= 16) return -3; cbp = rh264_cbp_inter_noc[cbp]; }
         else { if ((unsigned)cbp >= 48) return -3; cbp = rh264_cbp_inter[cbp]; }
         cbp_luma = cbp & 15; cbp_chroma = cbp >> 4;
         /* transform_size_8x8_flag sits between the cbp and mb_qp_delta
          * (7.3.5) and is only coded when the partitioning allows it */
         if (cbp_luma && pps->transform_8x8_mode && t8ok)
         {
            t8 = rh264_u1(b);
            if (t8) f->mbt8[mby*f->mbw + mbx] = 1;
         }
         if (cbp_luma || cbp_chroma)
         {
            int d = rh264_se(b);
            if (rh264_qp_apply_delta(f, d)) return -1;
         }
         if (rh264_inter_luma_residual(b, f, mbx, mby, cbp_luma, t8,
               sh->first_mb_in_slice) < 0) return -1;
         if (f->c444)
         {
            if (rh264_inter_chroma444_residual(b, f, mbx, mby, cbp_luma, t8,
                  sh->first_mb_in_slice) < 0) return -1;
         }
         else if (cbp_chroma)
         {
            if (rh264_decode_chroma_residual(b, f, mbx, mby, u, v, cbp_chroma,
                  sh->first_mb_in_slice, 1, 0) < 0)
               return -1;
         }
         else
            rh264_nzc_clear(f, mbx, mby);
         f->mbqp[mby * f->mbw + mbx] = (uint8_t)RH264_QPP(f);
      }
      mbaddr++;
   }
   if (end_mb) *end_mb = mbaddr;
   return 0;
}

/* mvlim is the vertical threshold: motion vectors are in quarter luma
 * samples of the picture being decoded, but 8.7.2.1 states the limit in
 * quarter samples of a FRAME, and a field's samples are twice as far
 * apart vertically - so a field picture compares against 2, not 4. */
static RH264_INLINE int rh264_bs_mvcmp(int ax, int ay, int bx, int by,
      int mvlim)
{
   return (ax - bx >= 4 || bx - ax >= 4
        || ay - by >= mvlim || by - ay >= mvlim);
}

/* Whether two cells carry the same motion from the same pictures: both
 * vectors and the four reference fields, compared as two words */
static RH264_INLINE int rh264_mv_same_pic(const rh264_mv *a, const rh264_mv *b)
{
   uint64_t va, vb;
   uint32_t ra, rb;
   memcpy(&va, &a->mvx, 8);
   memcpy(&vb, &b->mvx, 8);
   memcpy(&ra, &a->ref, 4);
   memcpy(&rb, &b->ref, 4);
   return va == vb && ra == rb;
}

/* Boundary strength between blocks P and Q per 8.7.2.1, over both prediction
 * lists. Reference pictures compare as unordered pairs; a list a block does
 * not use carries picture -1 with a zero vector, so one-list blocks fall out
 * of the same comparisons. When both blocks predict twice from one picture,
 * the vectors must differ under both pairings for the edge to strengthen. */
/* fieldhoriz marks a horizontal macroblock edge in a field picture,
 * where an intra edge filters at strength 3 rather than 4 (8.7.2.1). */
static RH264_INLINE int rh264_inter_bs(int mbedge, int intra_p, int intra_q,
      int cbf_p, int cbf_q, const rh264_mv *mp, const rh264_mv *mq,
      int fieldhoriz, int mvlim)
{
   int p0, p1, q0, q1;
   if (intra_p || intra_q)
      return (mbedge && !fieldhoriz) ? 4 : 3;
   if (cbf_p || cbf_q)
      return 2;
   /* Identical cells - same lists, same vectors - always derive 0:
    * every reference comparison matches and every vector difference is
    * zero.  Static and skipped regions make this the common case, so
    * take it before the general list-pairing walk below. */
   if (rh264_mv_same_pic(mp, mq))
      return 0;
   p0 = mp->ref  < 0 ? -1 : mp->pic;
   p1 = mp->ref1 < 0 ? -1 : mp->pic1;
   q0 = mq->ref  < 0 ? -1 : mq->pic;
   q1 = mq->ref1 < 0 ? -1 : mq->pic1;
   if (!((p0 == q0 && p1 == q1) || (p0 == q1 && p1 == q0)))
      return 1;   /* different reference pictures */
   if (p0 != p1)
   {
      if (p0 == q0)
         return rh264_bs_mvcmp(mp->mvx,  mp->mvy,  mq->mvx,  mq->mvy, mvlim)
              | rh264_bs_mvcmp(mp->mvx1, mp->mvy1, mq->mvx1, mq->mvy1, mvlim);
      return rh264_bs_mvcmp(mp->mvx,  mp->mvy,  mq->mvx1, mq->mvy1, mvlim)
           | rh264_bs_mvcmp(mp->mvx1, mp->mvy1, mq->mvx,  mq->mvy, mvlim);
   }
   return (rh264_bs_mvcmp(mp->mvx,  mp->mvy,  mq->mvx,  mq->mvy, mvlim)
         | rh264_bs_mvcmp(mp->mvx1, mp->mvy1, mq->mvx1, mq->mvy1, mvlim))
       && (rh264_bs_mvcmp(mp->mvx,  mp->mvy,  mq->mvx1, mq->mvy1, mvlim)
         | rh264_bs_mvcmp(mp->mvx1, mp->mvy1, mq->mvx,  mq->mvy, mvlim));
}

/* In-loop deblocking for a P-slice: identical filter kernels to the intra
 * rh264_deblock, but the boundary strength is derived per 4-sample edge segment
 * from the macroblock/partition geometry carried in the MV grid (mvg) and the
 * per-4x4 non-zero-coefficient flags (f->nzL / f->nzC). */
/* Coefficient presence of the luma 4x4 block at grid (gx,gy) for the
 * deblocking bS derivation (8.7.2.1). Inside an 8x8-transform macroblock the
 * criterion is the covering 8x8 block's coded status, while nzL keeps the
 * per-4x4 total_coeff values that CAVLC nC prediction needs, so the four
 * cells of the 8x8 are OR-ed together here. */
/* tC0 by bS for one indexA (Table 8-17, scaled to the bit depth):
 * entry 0 for an edge left alone, 1..3 for bS 1..3, and bS 4 reading
 * the bS 3 column as the per-segment lookups did - so the strengths of
 * an edge's segments map to their tC0 with one load each */
static RH264_INLINE void rh264_tc_row(const rh264_frame *f, int idxA, int *t)
{
   t[0] = 0;
   t[1] = RH264_BDS(f, rh264_tc0[0][idxA]);
   t[2] = RH264_BDS(f, rh264_tc0[1][idxA]);
   t[3] = RH264_BDS(f, rh264_tc0[2][idxA]);
   t[4] = t[3];
}
static unsigned rh264_mb_nzmask(const rh264_frame *f, int gw,
      int mbx, int mby)
{
   const uint8_t *nz = f->nzL + (size_t)(mby*4)*gw + mbx*4;
   unsigned m = 0;
#ifdef RH264_SSE2
   {
      /* the four rows of four flags side by side, one compare, one
       * movemask: bit y*4 + x set for a zero flag */
      uint32_t r0, r1, r2, r3;
      memcpy(&r0, nz,          4);
      memcpy(&r1, nz + gw,     4);
      memcpy(&r2, nz + 2*gw,   4);
      memcpy(&r3, nz + 3*gw,   4);
      m = 0xffffu & ~(unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(
            _mm_set_epi32((int)r3, (int)r2, (int)r1, (int)r0),
            _mm_setzero_si128()));
   }
#else
   {
      int x, y;
      for (y = 0; y < 4; y++)
         for (x = 0; x < 4; x++)
            m |= (unsigned)(nz[y*gw + x] != 0) << (y*4 + x);
   }
#endif
   if (m && f->mbt8 && f->mbt8[mby*f->mbw + mbx])
   {
      unsigned q = 0;
      if (m & 0x0033u) q |= 0x0033u;
      if (m & 0x00ccu) q |= 0x00ccu;
      if (m & 0x3300u) q |= 0x3300u;
      if (m & 0xcc00u) q |= 0xcc00u;
      m = q;
   }
   return m;
}

static void rh264_deblock_pslice(rh264_frame *f, const signed char *sidc,
      const signed char *soA, const signed char *soB, const rh264_mv *mvg,
      int mby0, int mby1)
{
   int mbx, mby, edge, seg, mba, nedge;
   int gw = f->mbw * 4, cgw = f->mbw * 2;
   int nx = 0, ny = mby0;   /* raster position, without pair scanning */

   /* address order, not raster: see rh264_deblock */
   for (mba = mby0 * f->mbw; mba < mby1 * f->mbw; mba++)
   {
      int mbi, sl, oA, oB, qp, mbt8;
      unsigned curm, lftm, topm;
      rh264_tb_save *tbs = f->scr_tbsave;
      if (f->mbaff)
         rh264_mb_pos(mba, f->mbw, f->mbaff, &mbx, &mby);
      else
      {
         /* the address steps through the rows: no division needed */
         mbx = nx; mby = ny;
         if (++nx == f->mbw) { nx = 0; ny++; }
      }
      mbi = mby*f->mbw+mbx;
      sl  = f->mbslice ? f->mbslice[mbi] : 0;
      oA = soA[sl]; oB = soB[sl];
      qp  = rh264_mbqp(f, mbi);
      mbt8 = f->mbt8 ? f->mbt8[mbi] : 0;
      if (sidc[sl] == 1) continue;   /* filter disabled for this slice */
      rh264_tb_deblock_save(f, mbx, mby, tbs);
      curm = rh264_mb_nzmask(f, gw, mbx, mby);
      lftm = topm = 0;
      /* the neighbours' flags next to this macroblock: the right column
       * of the left one's mask, the bottom row of the top one's (an 8x8
       * transform's flag already covering its four cells there) */
      if (mbx > 0)
      {
         unsigned l = rh264_mb_nzmask(f, gw, mbx - 1, mby);
         lftm = ((l >> 3) & 1u) | ((l >> 6) & 2u)
              | ((l >> 9) & 4u) | ((l >> 12) & 8u);
      }
      if (mby > 0)
         topm = rh264_mb_nzmask(f, gw, mbx, mby - 1) >> 12;

      /* An inter macroblock with no luma coefficients and one motion
       * over all sixteen cells has bS 0 on every internal edge, in
       * every plane: only its outer edges are left to filter */
      nedge = 4;
      if (!curm)
      {
         /* within one macroblock a reference index names one picture,
          * so comparing the picture ids as well changes nothing and
          * lets the cells compare as two words */
         const rh264_mv *c0 = &mvg[(mby*4)*gw + mbx*4];
         int r, k, same = !c0->intra;
         for (r = 0; same && r < 4; r++)
            for (k = 0; same && k < 4; k++)
               same = rh264_mv_same_pic(c0, &c0[r*gw + k]);
         if (same)
            nedge = 1;
      }

      /* ---- vertical edges (filter columns), left to right ---- */
      for (edge = 0; edge < nedge; edge++)
      {
         int x = mbx*16 + edge*4;
         int mbedge = (edge == 0);
         int qpavg, qpp;
         int vbS[4];
         if (mbt8 && (edge & 1)) continue;  /* 8x8 transform: no 4x4 edges */
         if (mbedge && mbx == 0) continue;
         if (mbedge && sidc[sl] == 2 && f->mbslice
               && f->mbslice[mbi-1] != sl)
            continue;   /* no filtering across the slice boundary */
         qpp = mbedge ? ((f->mbqp ? (int)f->mbqp[mby*f->mbw+mbx-1] - f->qpbd : qp)) : qp;
         qpavg = mbedge ? ((qp + qpp + 1) >> 1) : qp;
         /* four 4-sample segments down the edge, each its own bS.
          * Adjacent active segments of the same class (bS < 4 or
          * bS == 4) run as one eight-lane pass; the chroma walk below
          * reuses the gathered strengths. */
         {
            int st[4], tcr[5];
            int a, be, idxA, idxB;
            idxA = qpavg+oA; if(idxA<0)idxA=0; else if(idxA>51)idxA=51;
            idxB = qpavg+oB; if(idxB<0)idxB=0; else if(idxB>51)idxB=51;
            a = RH264_BDS(f, rh264_alpha[idxA]); be = RH264_BDS(f, rh264_beta[idxB]);
            rh264_tc_row(f, idxA, tcr);
            for (seg = 0; seg < 4; seg++)
            {
               int gy = mby*4 + seg;
               int gxq = mbx*4 + edge;        /* Q block column   */
               int gxp = gxq - 1;             /* P block (left)   */
               const rh264_mv *mp = &mvg[gy*gw + gxp];
               const rh264_mv *mq = &mvg[gy*gw + gxq];
               int nzp = mbedge ? (int)((lftm >> seg) & 1u)
                                : (int)((curm >> (seg*4 + edge - 1)) & 1u);
               int nzq = (int)((curm >> (seg*4 + edge)) & 1u);
               vbS[seg] = rh264_inter_bs(mbedge, mp->intra, mq->intra,
                     nzp, nzq, mp, mq, 0,
                     f->field ? 2 : 4);
               st[seg] = tcr[vbS[seg]];
            }
            for (seg = 0; seg < 4; )
            {
               if (!vbS[seg]) { seg++; continue; }
               if (seg + 1 < 4 && vbS[seg+1]
                     && (vbS[seg] == 4) == (vbS[seg+1] == 4))
               {
                  rh264_filter_luma_edge_pair(f, 
                        f->Y + RH264_OFF(f, (mby*16+seg*4)*f->ystride + x), 1,
                        f->ystride, vbS[seg], st[seg],
                        vbS[seg+1], st[seg+1], a, be);
                  seg += 2;
                  continue;
               }
               rh264_filter_luma_edge_n(f, 
                     f->Y + RH264_OFF(f, (mby*16+seg*4)*f->ystride + x), 1,
                     f->ystride, 4, vbS[seg], a, be, st[seg]);
               seg++;
            }
         }
         if (f->c444)
         {
            /* luma-sized chroma, filtered as luma at the chroma qP,
             * the same per-segment strengths */
            int cc;
            for (cc = 0; cc < 2; cc++)
            {
               int coff = cc?f->chroma_qp_offset2:f->chroma_qp_offset;
               int qc = rh264_chroma_qp(qp, coff, f->qpbd);
               int cqpavg = mbedge ?
                  ((qc + rh264_chroma_qp(qpp, coff, f->qpbd) + 1)>>1) : qc;
               int cA=cqpavg+oA, cB=cqpavg+oB, ca, cbe;
               uint8_t *pl = cc?f->V:f->U;
               if(cA<0)cA=0; else if(cA>51)cA=51;
               if(cB<0)cB=0; else if(cB>51)cB=51;
               ca=RH264_BDS(f, rh264_alpha[cA]); cbe=RH264_BDS(f, rh264_beta[cB]);
               for (seg = 0; seg < 4; seg++)
               {
                  int ct;
                  if (!vbS[seg]) continue;
                  ct = RH264_BDS(f, rh264_tc0[vbS[seg]==4?2:vbS[seg]-1][cA]);
                  rh264_filter_luma_edge_n(f, 
                        pl + RH264_OFF(f, (size_t)(mby*16+seg*4)*f->cstride + x), 1,
                        f->cstride, 4, vbS[seg], ca, cbe, ct);
               }
            }
         }
         /* chroma on even luma edges: the whole chroma edge in one
          * pass, lanes carrying each luma segment's strength and tc
          * (4:2:2 keeps the luma height, so a segment covers twice as
          * many chroma rows) */
         else if ((edge&1)==0 && (vbS[0]|vbS[1]|vbS[2]|vbS[3]))
         {
            int cx = mbx*8 + (edge>>1)*4, cc;
            for (cc = 0; cc < 2; cc++)
            {
               int coff = cc?f->chroma_qp_offset2:f->chroma_qp_offset;
               int qc = rh264_chroma_qp(qp, coff, f->qpbd);
               int cqpavg = mbedge ?
                  ((qc + rh264_chroma_qp(qpp, coff, f->qpbd) + 1)>>1) : qc;
               int cA=cqpavg+oA, cB=cqpavg+oB, ca, cbe, si;
               int ctv[4], ctr[5];
               uint8_t *pl = cc?f->V:f->U;
               if(cA<0)cA=0; else if(cA>51)cA=51;
               if(cB<0)cB=0; else if(cB>51)cB=51;
               ca=RH264_BDS(f, rh264_alpha[cA]); cbe=RH264_BDS(f, rh264_beta[cB]);
               rh264_tc_row(f, cA, ctr);
               for (si = 0; si < 4; si++)
                  ctv[si] = ctr[vbS[si]];
               rh264_filter_chroma_edge_seg(f, 
                     pl + RH264_OFF(f, (size_t)(mby*f->cmbh)*f->cstride + cx),
                     1, f->cstride, f->cmbh, vbS, ctv, ca, cbe);
            }
            (void)cgw;
         }
      }

      /* ---- horizontal edges (filter rows), top to bottom ---- */
      for (edge = 0; edge < nedge; edge++)
      {
         int y = mby*16 + edge*4;
         int mbedge = (edge == 0);
         int qpavg, qpp;
         int hbS[4];
         /* the 8x8 transform removes the LUMA edges inside each block,
          * not the chroma ones - which 4:2:2 has at every luma edge */
         int do_luma = !(mbt8 && (edge & 1));
         if (!do_luma && (f->cmbh != 16 || f->c444)) continue;
         if (mbedge && mby == 0) continue;
         if (mbedge && sidc[sl] == 2 && f->mbslice
               && f->mbslice[mbi-f->mbw] != sl)
            continue;   /* no filtering across the slice boundary */
         qpp = mbedge ? ((f->mbqp ? (int)f->mbqp[(mby-1)*f->mbw+mbx] - f->qpbd : qp)) : qp;
         qpavg = mbedge ? ((qp + qpp + 1) >> 1) : qp;
         {
            int st[4], tcr[5];
            int a, be, idxA, idxB;
            idxA = qpavg+oA; if(idxA<0)idxA=0; else if(idxA>51)idxA=51;
            idxB = qpavg+oB; if(idxB<0)idxB=0; else if(idxB>51)idxB=51;
            a = RH264_BDS(f, rh264_alpha[idxA]); be = RH264_BDS(f, rh264_beta[idxB]);
            rh264_tc_row(f, idxA, tcr);
            for (seg = 0; seg < 4; seg++)
            {
               int gx = mbx*4 + seg;
               int gyq = mby*4 + edge;        /* Q block row    */
               int gyp = gyq - 1;             /* P block (above)*/
               const rh264_mv *mp = &mvg[gyp*gw + gx];
               const rh264_mv *mq = &mvg[gyq*gw + gx];
               int nzp = mbedge ? (int)((topm >> seg) & 1u)
                                : (int)((curm >> ((edge-1)*4 + seg)) & 1u);
               int nzq = (int)((curm >> (edge*4 + seg)) & 1u);
               hbS[seg] = rh264_inter_bs(mbedge, mp->intra, mq->intra,
                     nzp, nzq, mp, mq, f->field,
                     f->field ? 2 : 4);
               st[seg] = tcr[hbS[seg]];
            }
            for (seg = 0; do_luma && seg < 4; )
            {
               if (!hbS[seg]) { seg++; continue; }
               if (seg + 1 < 4 && hbS[seg+1]
                     && (hbS[seg] == 4) == (hbS[seg+1] == 4))
               {
                  rh264_filter_luma_edge_pair(f, 
                        f->Y + RH264_OFF(f, y*f->ystride + mbx*16 + seg*4),
                        f->ystride, 1, hbS[seg], st[seg],
                        hbS[seg+1], st[seg+1], a, be);
                  seg += 2;
                  continue;
               }
               rh264_filter_luma_edge_n(f, 
                     f->Y + RH264_OFF(f, y*f->ystride + mbx*16 + seg*4),
                     f->ystride, 1, 4, hbS[seg], a, be, st[seg]);
               seg++;
            }
         }
         if (f->c444)
         {
            int cc;
            for (cc = 0; cc < 2; cc++)
            {
               int coff = cc?f->chroma_qp_offset2:f->chroma_qp_offset;
               int qc = rh264_chroma_qp(qp, coff, f->qpbd);
               int cqpavg = mbedge ?
                  ((qc + rh264_chroma_qp(qpp, coff, f->qpbd) + 1)>>1) : qc;
               int cA=cqpavg+oA, cB=cqpavg+oB, ca, cbe;
               uint8_t *pl = cc?f->V:f->U;
               if(cA<0)cA=0; else if(cA>51)cA=51;
               if(cB<0)cB=0; else if(cB>51)cB=51;
               ca=RH264_BDS(f, rh264_alpha[cA]); cbe=RH264_BDS(f, rh264_beta[cB]);
               for (seg = 0; seg < 4; seg++)
               {
                  int ct;
                  if (!hbS[seg]) continue;
                  ct = RH264_BDS(f, rh264_tc0[hbS[seg]==4?2:hbS[seg]-1][cA]);
                  rh264_filter_luma_edge_n(f, 
                        pl + RH264_OFF(f, (size_t)y*f->cstride + mbx*16 + seg*4),
                        f->cstride, 1, 4, hbS[seg], ca, cbe, ct);
               }
            }
         }
         /* chroma edge in one pass, lanes along x carrying each luma
          * segment's strength and tc */
         else if (((edge&1)==0 || f->cmbh==16)
               && (hbS[0]|hbS[1]|hbS[2]|hbS[3]))
         {
            int cy = mby*f->cmbh
                   + ((f->cmbh==16) ? edge*4 : (edge>>1)*4), cc;
            for (cc = 0; cc < 2; cc++)
            {
               int coff = cc?f->chroma_qp_offset2:f->chroma_qp_offset;
               int qc = rh264_chroma_qp(qp, coff, f->qpbd);
               int cqpavg = mbedge ?
                  ((qc + rh264_chroma_qp(qpp, coff, f->qpbd) + 1)>>1) : qc;
               int cA=cqpavg+oA, cB=cqpavg+oB, ca, cbe, si;
               int ctv[4], ctr[5];
               uint8_t *pl = cc?f->V:f->U;
               if(cA<0)cA=0; else if(cA>51)cA=51;
               if(cB<0)cB=0; else if(cB>51)cB=51;
               ca=RH264_BDS(f, rh264_alpha[cA]); cbe=RH264_BDS(f, rh264_beta[cB]);
               rh264_tc_row(f, cA, ctr);
               for (si = 0; si < 4; si++)
                  ctv[si] = ctr[hbS[si]];
               rh264_filter_chroma_edge_seg(f, 
                     pl + RH264_OFF(f, (size_t)cy*f->cstride + mbx*8),
                     f->cstride, 1, 8, hbS, ctv, ca, cbe);
            }
         }
      }
      rh264_tb_deblock_restore(f, mbx, mby, tbs);
   }
}

static int rh264_decode_islice(rh264_bits *b,const rh264_sps *sps,
      const rh264_pps *pps,rh264_slice_hdr *sh,rh264_frame *f,int *end_mb){
   int mbaddr=sh->first_mb_in_slice, total=f->mbw*f->mbh;
   f->qp=sh->slice_qp;
   f->chroma_qp_offset=pps->chroma_qp_index_offset;
   f->chroma_qp_offset2=pps->chroma_qp_index_offset2;
   f->tb=sps->tb;
   /* the coefficient/mode context describes the picture being decoded; a
    * continuation slice must keep what earlier slices of it produced.
    * The P/B decoders reset it the same way; here the IDR path had it
    * covered by the caller's frame reset, but a non-IDR I picture
    * arrives with the previous picture's context - in particular its
    * 8x8-transform flags, which the intra branches only ever set, and
    * which steer the deblocking edge set (8.7). */
   if (sh->first_mb_in_slice == 0)
   {
      int gw2=f->mbw*4, cgw2=f->mbw*2;
      memset(f->nzL, 0, (size_t)gw2 * f->mbh * 4);
      memset(f->mbt8, 0, (size_t)f->mbw * f->mbh);
      memset(f->nzC[0], 0, (size_t)cgw2 * f->mbh * (f->cmbh/4));
      memset(f->nzC[1], 0, (size_t)cgw2 * f->mbh * (f->cmbh/4));
      memset(f->i4mode, 0xff, (size_t)gw2 * f->mbh * 4);
   }
   while(mbaddr<total){
      int mbx, mby;
      int mb_type;
      rh264_mb_pos(mbaddr, f->mbw, f->mbaff, &mbx, &mby);
      if(!rh264_more_rbsp(b)) break;   /* end of this slice's data */
      /* mb_field_decoding_flag is sent once per macroblock pair, ahead
       * of its top macroblock (7.3.4).  A field-coded pair is refused:
       * its two macroblocks hold alternate lines and every neighbour
       * around them is derived differently. */
      if(f->mbaff && !(mbaddr & 1) && rh264_u1(b)) return -1;
      mb_type=rh264_ue(b);

      if(rh264_decode_intra_mb_cavlc(b,f,mbx,mby,mb_type,
            pps->transform_8x8_mode,sh->first_mb_in_slice)<0) return -1;
      if(f->mbqp) f->mbqp[mby*f->mbw+mbx]=(uint8_t)RH264_QPP(f);
         if (f->row_done && mbx == f->mbw - 1)
            f->row_done(f->row_user, mby);
      mbaddr++;
   }
   if(end_mb) *end_mb=mbaddr;
   return 0;
}


/* ==================== rh264_video.h ==================== */
/* rh264_video -- persistent H.264 intra decoder wrapper (rvp8_video-style).
 * Included after rh264_mb.h; provides the public rh264_video_* API. */




/* The CABAC engine state, declared here (rather than with the engine
 * below) because struct rh264_video embeds one in its slice-decode
 * workspace. */
typedef struct {
   const uint8_t *buf, *end;
   uint32_t range;      /* codIRange */
   /* codIOffset << RH264_CB_S, with the next bitcnt bits of the
    * stream right below it and zeros under those: a renormalisation
    * shifts the whole word up and takes bitcnt down, and bytes come in
    * a few at a time when it runs low.  codIOffset sitting at a fixed
    * bit leaves the per-bin comparison against range a constant shift
    * away */
   uint64_t val;
   int      bitcnt;
   int      pad;        /* zero bytes fed in past end */
   uint8_t  state[1024];   /* pStateIdx * 2 | valMPS, fused */
   /* fused state transitions, built from rh264_transIdxMPS/LPS at init:
    * next[0] follows an MPS, next[1] an LPS (including the valMPS flip
    * at pStateIdx 0, 9.3.3.2.1.1) */
   uint8_t  next[2][128];
   /* field-coded picture: the significance maps use their own context
    * offsets and, for 8x8 blocks, their own position-to-context map
    * (Tables 9-11 and 9-43) */
   int      field;
   /* 4:2:2 chroma: the DC block holds eight coefficients and its
    * significance map counts them in pairs (NumC8x8 = 2, 9.3.3.1.3) */
   int      c422;
   int      c444;   /* 4:4:4: the 8x8 coded_block_flag is coded (cat 5/9/13) */
   /* Per-macroblock residual scratch.  These were block-scope locals
    * in the slice and macroblock decoders (the chroma set alone is
    * 1.3 KiB), stacking on frames that already sit three deep on the
    * decode path; every use is transient within one residual block,
    * and one macroblock decodes at a time per cabac state. */
   int32_t  r8_scan[64], r8_coef[64], r8_r[64];
   int32_t  cr_dcs[2][8], cr_cdc[2][8], cr_cac[2][8][16];
} rh264_cabac;

#define RH264_MAX_SLICES 64

/* One slice's data decode, queued on its picture's context: the RBSP
 * (its own copy - the context's unescape scratch is reused per NAL),
 * the bit position the slice data starts at, the slice header, and
 * the reference lists as the submitter built them, pointing at the
 * context's snapshots. The queue is what a pool thread drains; drained
 * on the submitting thread it is the decoder as it was. */
typedef struct rh264_slice_job
{
   struct rh264_slice_job *next;
   uint8_t  *rbsp;       /* the buffer the slice was unescaped into */
   size_t    rbsp_cap;   /* its capacity, for its return to the pool */
   size_t    rbsp_len;
   size_t    bitpos;
   rh264_slice_hdr sh;
   int       kind;       /* 0 intra, 1 P, 2 B */
   int       cabac;
   int       nref;
   const rh264_frame *l0[34];
   signed char picid[34];
   int       l0poc[34];
   rh264_bctx bc;        /* B only */
} rh264_slice_job;

/* Everything that belongs to the picture being decoded, as distinct
 * from the sequence: the working frame, its motion grids, the slice
 * bookkeeping, the slice scratch and the NAL unescape buffer. One of
 * these per picture in flight is what lets pictures decode on
 * different threads; the sequencing state - parameter sets, the DPB,
 * picture order, reference marking - stays in rh264_video and is
 * updated in decode order by the thread that submits. */
typedef struct rh264_pic_ctx
{
   uint8_t  *rbsp_scratch;
   size_t    rbsp_scratch_cap;
   /* The parameter sets this picture decodes with, copied when it
    * opens: a later SPS or PPS in the stream must not change them
    * under a picture still decoding. */
   rh264_sps sps;
   rh264_pps pps;
   /* The pictures this one predicts from, as copies of their frames
    * holding their own references on the sample and motion blocks.
    * A slice decoder reads its lists through these rather than through
    * the DPB slots, so a slot taken by a later picture while this one
    * still decodes cannot pull a reference out from under it. Released
    * at the finish. */
   rh264_frame refs[RH264_MAX_REFS * 2 + 2];
   int         nrefs;
   int         alloc_w, alloc_h;  /* geometry the context's frame is for */
   int       pic_open;
   int       saw_switching;
   int       pic_kind;          /* 1 IDR-I, 2 recovery-I, 3 P, 4 B    */
   int       pic_ref;           /* nal_ref_idc of the picture         */
   int       pic_end;           /* first undecoded MB address         */
   int       pic_nslices;
   int       pic_first[RH264_MAX_SLICES];  /* first_mb per slice      */
   signed char pic_idc[RH264_MAX_SLICES];  /* deblock idc per slice   */
   signed char pic_oA[RH264_MAX_SLICES];   /* alpha offset per slice  */
   signed char pic_oB[RH264_MAX_SLICES];   /* beta offset per slice   */
   rh264_mv *pic_mvg;           /* picture MV grid accumulated across
                                   slices (v->cur->mvg is per-slice scratch) */
   rh264_frame f;
   rh264_mv  *mvg;              /* motion-vector grid for the frame being decoded */
   /* the grid the slice now decoding writes: pic_mvg itself for a
    * picture's first slice, mvg (other slices' cells "not decoded")
    * for any later one, which is then folded in */
   rh264_mv  *slice_mvg;
   struct
   {
      rh264_slice_hdr sh;
      rh264_bctx      bc;
      const rh264_frame *ordered[RH264_MAX_REFS];
      int opoc[RH264_MAX_REFS], opn[RH264_MAX_REFS];
      int oslot[RH264_MAX_REFS];
      int l0slot[34], l1slot[34];
      int lpn0[34], lpn1[34];
      int lpn[34], l0poc[34];
      signed char picid[34];
      const rh264_frame *l0[34];   /* a P slice's list, once built */
      rh264_cabac cb;
   } sscr;
   /* The queued slices of this picture, in order, and how the run of
    * them ended: 0 while they are still fine, the decoder's -1 once one
    * refused. */
   rh264_slice_job *jobs, *jobs_tail;
   int         job_rc;
   int         completed;     /* rh264_ctx_complete_picture has run */
   int         posted;        /* the queue went to the pool */
   struct rh264_video *owner; /* the decoder, for the pool of buffers */
   retro_atomic_int_t busy;   /* a pool thread is still on this picture */
   const struct rh264_vlc_set *vlc;
} rh264_pic_ctx;

#define RH264_MAX_CTX 8
#define RH264_RBSP_POOL 32   /* slice jobs kept between slices */

/* A pool slot holds a pointer or nothing, and changes hands in one
 * atomic swap: whoever swaps a pointer out owns it. Without pointer
 * atomics a decoder runs on the one thread that calls it and keeps no
 * pool shared between decoders, so its slots are plain. */
#ifdef RETRO_ATOMIC_HAS_PTR
typedef retro_atomic_ptr_t rh264_slot_t;
#define rh264_slot_peek(s)    retro_atomic_load_relaxed_ptr(s)
#define rh264_slot_take(s)    retro_atomic_exchange_ptr((s), NULL)
#define rh264_slot_fill(s, p) retro_atomic_cas_ptr((s), NULL, (p))
#else
typedef void *rh264_slot_t;
#define rh264_slot_peek(s)    (*(s))

static void *rh264_slot_take(rh264_slot_t *s)
{
   void *p = *s;
   *s      = NULL;
   return p;
}

static int rh264_slot_fill(rh264_slot_t *s, void *p)
{
   if (*s)
      return 0;
   *s = p;
   return 1;
}
#endif

struct rh264_video
{
   /* One context per picture that can be in flight. A new picture
    * takes the next one round; with one context that is the same one
    * every time, which is the decoder as it was. Field pairs stay on
    * the context their first field took. */
   rh264_pic_ctx  ctx[RH264_MAX_CTX];
   rh264_pic_ctx *cur;          /* the picture being decoded */
   int            nctx;         /* contexts in rotation, 1..RH264_MAX_CTX */
#ifdef HAVE_THREADS
   void          *pool;         /* tpool_t the pictures decode on, or NULL */
#endif
   int            threaded;     /* pictures decode on the pool, in flight */
   int            broken;       /* a posted picture's decode refused */
   /* What the pipeline did, for a bench to print: pictures posted,
    * the sum over posts of pictures then in flight, posts that found
    * the most possible in flight, joins that had to wait, pops that
    * held a due picture for being incomplete, pops that waited for
    * one because the queue was full. */
   int            st_posted, st_inflight_sum, st_inflight_max;
   int            st_join_waits, st_pop_held, st_pop_waits;
   /* Finished slice jobs between uses, each with the buffer its slice
    * was unescaped into: a slice is unescaped into the context's
    * buffer, the queued job takes that very buffer, and the context
    * takes the one a pooled job came back with. So a slice is
    * unescaped once, into memory that is neither made nor copied per
    * slice. */
   rh264_slot_t   job_pool[RH264_RBSP_POOL];

   /* unescaped-RBSP scratch for slice NALs, grown on demand and kept
    * for the decoder's lifetime */
   rh264_sps sps;
   rh264_pps pps;
   int       have_sps, have_pps;
   int       nal_length_size;   /* from avcC; 0 => Annex-B input */
   /* picture assembly across slices */
   int       cur_field, pair_open, pair_frame_num, pair_poc;
   /* A switching slice was seen.  Those are references, so once one is
    * refused nothing after it can be reconstructed either; the stream
    * is given up rather than decoded into drift. */
   /* When set, a non-reference picture (nal_ref_idc 0) is consumed
    * without being decoded. Nothing predicts from it, so the pictures
    * after it decode exactly as they would have; only its own output
    * is missing, which is what a caller that has fallen behind the
    * clock wants. */
   retro_atomic_int_t skip_nonref;  /* set from the caller's thread, read by the decode's */
   int       skip_pic;               /* the picture whose first slice was dropped: all of it is */
   int       dropped;           /* the last decode call passed a picture over */
   /* DPB slot the pair's first field opened, so the second can fill it */
   int       pair_slot;
   rh264_frame dpb[RH264_MAX_REFS];  /* short-term reference pictures        */
   int        dpb_slot[RH264_MAX_REFS]; /* slot indices, most recent first   */
   int        dpb_pn[RH264_MAX_REFS];   /* PicNum per slot                   */
   int        dpb_len;          /* number of valid reference pictures        */
   int        dpb_size;         /* slots allocated                           */
   int        dpb_poc[RH264_MAX_REFS];  /* picture order count per slot      */
   int        have_ref;         /* a reference frame is available (post-IDR)     */
   int        last_picnum;      /* frame_num of the picture just decoded         */
   int        last_poc;         /* POC of the picture just decoded               */
   int        alloc_w, alloc_h; /* geometry the plane buffers were sized for */
   /* Picture order count derivation state (8.2.1). prev_* track the previous
    * reference picture in decode order for type 0; fn_offset accumulates
    * frame_num wraps for type 2. */
   int        prev_poc_msb, prev_poc_lsb, prev_frame_num, fn_offset;
   /* the frame's two field order counts, beside the one derived */
   int        poc_top, poc_bot;
   /* Display-order output. Decoded pictures queue here and leave lowest
    * (generation, POC) first, so B pictures come out in presentation order.
    * gen counts IDRs: POC restarts at an IDR, and every picture after it in
    * decode order also follows it in output order (8.2.1). */
   rh264_frame out[RH264_OUT_SLOTS];
   int        out_poc[RH264_OUT_SLOTS];
   int        out_gen[RH264_OUT_SLOTS];
   int        out_used[RH264_OUT_SLOTS];
   int        out_len;          /* pictures queued and not yet shown         */
   int        out_show;         /* slot most recently handed out, -1 if none */
   /* Inverse CAVLC tables of this instance; vlc_ready 0 (a failed build)
    * routes all residual decode through the serial matchers instead. */
   struct rh264_vlc_set vlc;
   int        vlc_ready;
   int        idr_gen;          /* IDR generation of the current picture     */
   int        reorder_delay;    /* output delay in pictures, fixed per SPS   */
   /* marking ops of the picture just decoded, executed before it is stored */
   int        pend_n_mmco;
   uint8_t    pend_mmco_op[RH264_MAX_REFS*2];
   int32_t    pend_mmco_a[RH264_MAX_REFS*2], pend_mmco_b[RH264_MAX_REFS*2];
   int        pend_idr_ltr;     /* IDR long_term_reference_flag           */
   signed char dpb_lt[RH264_MAX_REFS]; /* long_term_frame_idx/slot; -1 short */
   /* Which fields of each stored frame hold decoded data: bit 0 top,
    * bit 1 bottom.  A frame picture sets both; the two fields of a
    * complementary pair fill one entry between them. */
   uint8_t     dpb_fields[RH264_MAX_REFS];
   /* Each field of a stored pair carries its own order count; dpb_poc
    * keeps the frame's (the smaller of the two), which is what frame
    * references compare against. */
   int         dpb_poc_fld[RH264_MAX_REFS][2];
   /* Reference field views handed to the slice decoders; each aliases a
    * stored frame with field strides. */
   rh264_frame fieldview[68];   /* both B lists need their own views */
   /* Identity of each view's picture, stable across lists: deblocking
    * compares these to ask whether two blocks used the same reference,
    * and the same field can sit at different view slots in list 0 and
    * list 1.  Frames use their buffer slot, fields 32 + slot*2 +
    * parity, which cannot collide with it. */
   signed char fieldview_id[68];
   int        max_lt_idx;       /* MaxLongTermFrameIdx; -1 none allowed   */
   /* The hardware path's state, while the pictures decode on a
    * backend (rh264_video_set_hw); NULL otherwise. */
   struct rh264_hw *hw;
   /* Slice-decode workspace for rh264_video_decode_inter.  As locals
    * these were a 7.5 KiB frame -- the slice header, the B-list
    * context with its implicit-weight table, and the reference-list
    * construction arrays -- under which the per-macroblock CABAC and
    * MC frames still stack, against the tree's 8 KiB thread-stack
    * floor.  One slice decodes at a time per rh264_video, and nothing
    * here survives the call, so the workspace lives with the rest of
    * the (heap-allocated) decoder state. */
};

/* ---- allocation helpers ---- */

/* A frame's plane block carries a reference count in a header ahead
 * of the samples, so a decoded picture can be at once the working
 * frame's, a reference in the DPB and a picture waiting to be shown
 * without being copied between them: each holder takes a reference,
 * and the block goes when the last one lets go. The header keeps the
 * samples 64-byte aligned, which is what the arena layout assumes. */
#define RH264_PLANES_HDR 64

/* Blocks are recycled rather than freed. The working frame takes a
 * fresh block for nearly every picture - the one it holds is a
 * reference's or a queued picture's by then - and a 4K block is twelve
 * megabytes: on an allocator that hands such sizes to the kernel and
 * back (the C runtime MinGW builds against does) that is a page-zeroed
 * mapping per picture, twice, where the swap it replaced allocated
 * nothing. A released block is parked in one of a few slots, and the
 * next of that length comes from there. The slots are one set per
 * process, shared by every decoder and thread in it. */
typedef struct rh264_block_hdr
{
   /* Holders let go from pool threads and the sequence's thread alike:
    * the count is atomic, and the last one out frees or recycles. */
   retro_atomic_int_t refs;
   size_t len;
   /* How many macroblock rows of the picture in this block are final:
    * reconstructed, filtered, and no longer written by anything -
    * counted up from the top by the row hook as each row's filtering
    * completes, and set to the whole picture at the finish. A picture
    * decoding from this one as a reference reads up to a row and no
    * further, so this is the one number it has to wait on; on a single
    * thread it is complete before anyone asks. */
   retro_atomic_int_t rows_final;
} rh264_block_hdr;

static rh264_block_hdr *rh264_block_of(const void *data)
{
   return (rh264_block_hdr*)((const uint8_t*)data - RH264_PLANES_HDR);
}

static int rh264_block_rows_final(const void *data)
{
   return retro_atomic_load_acquire_int(&rh264_block_of(data)->rows_final);
}

#ifdef HAVE_THREADS
/* A test knob: hold each row's publication for a random number of
 * thread yields before making it, so that a picture reading from this one finds its rows
 * missing far more often than real content ever arranges, and the
 * wait is what decodes the picture rather than luck. Output must be
 * byte-exact under it, or the wait is wrong. 0 (the default) is off.
 * Debug only: rh264_video_set_publish_delay() sets it. */
static int rh264_publish_delay_us;   /* yields, at most, per row */
static retro_atomic_int_t rh264_publish_seed;   /* every thread draws from it */
#endif

/* Publish that @rows rows of the picture in @data are final. */
static void rh264_block_publish_rows(void *data, int rows)
{
   rh264_block_hdr *b = rh264_block_of(data);
#ifdef HAVE_THREADS
   if (rh264_publish_delay_us > 0 && rows > 0)
   {
      /* a small linear congruential draw, and that many yields: a
       * test knob, so a spin that gives the core away is fine here */
      unsigned r = (unsigned)retro_atomic_fetch_add_int(&rh264_publish_seed, 0x9E3779B1);
      unsigned n;
      r = r * 1103515245u + 12345u;
      n = (r >> 16) % (unsigned)rh264_publish_delay_us;
      while (n--)
         sthread_yield();
   }
#endif
   retro_atomic_store_release_int(&b->rows_final, rows);
#ifdef HAVE_THREADS
   if (rh264_rows_ec_ok)
      retro_eventcount_notify(&rh264_rows_ec);
#endif
}

/* Planes released by any decoder wait here for the next of the same
 * size. Shared between decoders on any threads, so only where the
 * slots are atomic; elsewhere a block is simply freed. */
#ifdef RETRO_ATOMIC_HAS_PTR
#define RH264_FREE_BLOCKS 16
#else
#define RH264_FREE_BLOCKS 0
#endif

#if RH264_FREE_BLOCKS
static rh264_slot_t rh264_free_blocks[RH264_FREE_BLOCKS];
#endif

static int rh264_block_park(rh264_block_hdr *b)
{
#if RH264_FREE_BLOCKS
   int i;
   for (i = 0; i < RH264_FREE_BLOCKS; i++)
      if (    !rh264_slot_peek(&rh264_free_blocks[i])
            && rh264_slot_fill(&rh264_free_blocks[i], b))
         return 1;
#else
   (void)b;
#endif
   return 0;
}

static uint8_t *rh264_block_new(size_t len)
{
   rh264_block_hdr *b;
#if RH264_FREE_BLOCKS
   int i;
   for (i = 0; i < RH264_FREE_BLOCKS; i++)
   {
      if (     !rh264_slot_peek(&rh264_free_blocks[i])
            || !(b = (rh264_block_hdr*)rh264_slot_take(&rh264_free_blocks[i])))
         continue;
      if (b->len == len)
      {
         retro_atomic_store_release_int(&b->refs, 1);
         retro_atomic_store_release_int(&b->rows_final, 0);
         return (uint8_t*)b + RH264_PLANES_HDR;
      }
      /* another size: its length is only readable once owned */
      if (     !rh264_slot_fill(&rh264_free_blocks[i], b)
            && !rh264_block_park(b))
         free(b);
   }
#endif
   b = (rh264_block_hdr*)calloc(len + RH264_PLANES_HDR, 1);
   if (!b)
      return NULL;
   retro_atomic_store_release_int(&b->refs, 1);
   b->len  = len;
   return (uint8_t*)b + RH264_PLANES_HDR;
}

static void rh264_block_release(void *data)
{
   rh264_block_hdr *b;
   if (!data)
      return;
   b = (rh264_block_hdr*)((uint8_t*)data - RH264_PLANES_HDR);
   if (retro_atomic_fetch_sub_int(&b->refs, 1) != 1)
      return;
   if (!rh264_block_park(b))
      free(b);
}

static rh264_slice_job *rh264_job_take(rh264_video *v)
{
   int i;
   for (i = 0; i < RH264_RBSP_POOL; i++)
   {
      void *j;
      if (     rh264_slot_peek(&v->job_pool[i])
            && (j = rh264_slot_take(&v->job_pool[i])))
         return (rh264_slice_job*)j;
   }
   return NULL;
}

/* Back to the pool with its buffer, or freed when the pool is full. */
static void rh264_job_park(rh264_video *v, rh264_slice_job *j)
{
   int i;
   for (i = 0; i < RH264_RBSP_POOL; i++)
      if (    !rh264_slot_peek(&v->job_pool[i])
            && rh264_slot_fill(&v->job_pool[i], j))
         return;
   free(j->rbsp);
   free(j);
}

static uint8_t *rh264_planes_new(size_t len)
{
   return rh264_block_new(len);
}

static void rh264_planes_release(uint8_t *planes)
{
   rh264_block_release(planes);
}

static int rh264_planes_refs(const uint8_t *planes)
{
   return planes ? retro_atomic_load_acquire_int(&rh264_block_of(planes)->refs) : 0;
}

/* @dst takes @src's plane block, letting go of whatever it held. The
 * geometry is the same on both sides: every frame here is allocated
 * from the one SPS. */
static void rh264_frame_share_planes(rh264_frame *dst, const rh264_frame *src)
{
   if (dst->planes == src->planes)
      return;
   rh264_planes_release(dst->planes);
   dst->planes = src->planes;
   retro_atomic_fetch_add_int(&rh264_block_of(dst->planes)->refs, 1);
   dst->Yb = src->Yb; dst->Ub = src->Ub; dst->Vb = src->Vb;
   dst->Y  = dst->Yb; dst->U  = dst->Ub; dst->V  = dst->Vb;
}

/* A frame about to be written into needs a block of its own: while
 * another holder shares the one it has, it takes a fresh one, keeping
 * the layout (the plane views are offsets into the block). */
static int rh264_frame_own_planes(rh264_frame *f, size_t plane_len)
{
   uint8_t *fresh;
   size_t o_ub, o_vb;
   if (rh264_planes_refs(f->planes) <= 1)
      return 0;
   o_ub  = (size_t)(f->Ub - f->planes);
   o_vb  = (size_t)(f->Vb - f->planes);
   fresh = rh264_planes_new(plane_len);
   if (!fresh)
      return -1;
   rh264_planes_release(f->planes);
   f->planes = fresh;
   f->Yb = fresh; f->Ub = fresh + o_ub; f->Vb = fresh + o_vb;
   f->Y  = f->Yb; f->U  = f->Ub; f->V  = f->Vb;
   return 0;
}

/* The motion-vector grid of a reference picture gets the same
 * treatment as its samples: one counted block, shared with the
 * picture's working grid rather than copied into the slot. A frame
 * picture's motion stands for both parities, so mvg and mvg2 point at
 * the one block; field pairs keep the copy into the frame's own
 * grids, which stay carved out of meta. mvg_shared says which the
 * slot holds, since only a counted block is released. */
static rh264_mv *rh264_mvg_new(size_t cells)
{
   return (rh264_mv*)rh264_block_new(cells * sizeof(rh264_mv));
}

static void rh264_mvg_release(rh264_mv *g)
{
   rh264_block_release(g);
}

static int rh264_mvg_refs(const rh264_mv *g)
{
   return g ? retro_atomic_load_acquire_int(&rh264_block_of(g)->refs) : 0;
}

/* The slot takes the working grid; whatever counted block it held
 * goes, and a meta-carved grid is simply no longer pointed at. */
static void rh264_frame_share_mvg(rh264_frame *dst, rh264_mv *g)
{
   if (dst->mvg_shared)
      rh264_mvg_release(dst->mvg);
   retro_atomic_fetch_add_int(&rh264_block_of(g)->refs, 1);
   dst->mvg        = g;
   dst->mvg2       = g;
   dst->mvg_shared = 1;
}

/* A field about to be copied into the slot's grids needs the frame's
 * own, meta-carved ones back. */
static void rh264_frame_own_mvg(rh264_frame *f)
{
   if (!f->mvg_meta && f->mvg_cells)
   {
      /* first field into this slot: the slot's own grids, one per
       * parity, made now and kept with the frame */
      f->mvg_meta  = rh264_mvg_new(f->mvg_cells);
      f->mvg2_meta = rh264_mvg_new(f->mvg_cells);
   }
   if (f->mvg_shared)
      rh264_mvg_release(f->mvg);
   f->mvg        = f->mvg_meta;
   f->mvg2       = f->mvg2_meta;
   f->mvg_shared = 0;
}

/* A reference list entry becomes a snapshot held by the context: the
 * same frame, with a reference taken on each counted block it points
 * at. Entries naming the same picture share one snapshot. */
static const rh264_frame *rh264_ctx_snapshot(rh264_pic_ctx *c,
      const rh264_frame *src)
{
   int i;
   rh264_frame *d;
   for (i = 0; i < c->nrefs; i++)
      if (c->refs[i].planes == src->planes && c->refs[i].mvg == src->mvg
            && c->refs[i].field == src->field && c->refs[i].poc == src->poc)
         return &c->refs[i];
   if (c->nrefs >= (int)(sizeof(c->refs) / sizeof(c->refs[0])))
      return src;                       /* over the bound: read the slot */
   d = &c->refs[c->nrefs++];
   *d = *src;
   if (d->planes)
      retro_atomic_fetch_add_int(&rh264_block_of(d->planes)->refs, 1);
   if (d->mvg_shared && d->mvg)
      retro_atomic_fetch_add_int(&rh264_block_of(d->mvg)->refs, 1);
   /* the snapshot owns nothing carved out of the slot's meta */
   d->meta = NULL;
   d->mvg_meta = NULL; d->mvg2_meta = NULL;
   return d;
}

static void rh264_ctx_snapshot_list(rh264_pic_ctx *c,
      const rh264_frame **list, int n)
{
   int i;
   for (i = 0; i < n; i++)
      if (list[i])
         list[i] = rh264_ctx_snapshot(c, list[i]);
}

static void rh264_ctx_release_refs(rh264_pic_ctx *c)
{
   int i;
   for (i = 0; i < c->nrefs; i++)
   {
      rh264_planes_release(c->refs[i].planes);
      if (c->refs[i].mvg_shared)
         rh264_mvg_release(c->refs[i].mvg);
      c->refs[i].planes = NULL;
      c->refs[i].mvg    = NULL;
   }
   c->nrefs = 0;
}

static void rh264_frame_free(rh264_frame *f)
{
   rh264_planes_release(f->planes);
   if (f->mvg_shared)
      rh264_mvg_release(f->mvg);
   rh264_mvg_release(f->mvg_meta);
   rh264_mvg_release(f->mvg2_meta);
   free(f->meta);
   memset(f, 0, sizeof(*f));
}

/* Region spacing inside a frame's plane and meta blocks, in bytes:
 * every plane and map starts on a 64-byte boundary. */
#define RH264_ARENA_ALIGN 64
#define RH264_ARENA_NEXT(cur, bytes) \
   ((((cur) + (bytes) + RH264_ARENA_ALIGN - 1) / RH264_ARENA_ALIGN) \
    * RH264_ARENA_ALIGN)

/* Allocate a frame's planes and per-block maps from the SPS geometry:
 * one zeroed block for the three planes and one for the maps, with the
 * motion grids of a reference picture (with_mv) or the slice-reader
 * scratch of the working frame (with_scratch) carved into the latter
 * as well. */
#define RH264_FRAME_ALLOC_PLAIN   0
#define RH264_FRAME_ALLOC_REF     1
#define RH264_FRAME_ALLOC_WORKING 2
#define RH264_FRAME_ALLOC_SHELL   3   /* bookkeeping only; the block is shared in */
static int rh264_frame_alloc(rh264_frame *f, const rh264_sps *sps,
      int mode)
{
   int with_mv      = (mode == RH264_FRAME_ALLOC_REF);
   int with_samples = (mode != RH264_FRAME_ALLOC_SHELL);
   int with_scratch = (mode == RH264_FRAME_ALLOC_WORKING);
   int mbw = sps->pic_width_in_mbs;
   int mbh = sps->frame_mbs > 0 ? sps->frame_mbs
           : (sps->frame_height + 15) / 16;
   size_t ylen, clen, o_ub, o_vb, plane_len;
   size_t grid, mbs, mvlen, o_nzl, o_nzc0, o_nzc1, o_mbqp, o_mbt8,
          o_mbslice, o_mvg, o_mvg2, meta_len;
   size_t rowlen, amlen, o_row, o_toprow, o_skiprow, o_topskip,
          o_typerow, o_toptype, o_am0, o_am1, o_bipred, o_mc, o_tbsave;
   rh264_frame_free(f);
   f->w = sps->frame_width;  f->h = sps->frame_height;
   f->cropx = sps->crop_x;   f->cropy = sps->crop_y;
   f->mbw = mbw;             f->mbh = mbh;
   f->c444    = (sps->chroma_format_idc == 3);
   f->bd      = sps->bit_depth_luma ? sps->bit_depth_luma : 8;
   f->bsh     = (f->bd > 8) ? 1 : 0;
   f->qpbd    = 6 * (f->bd - 8);
   f->ystride = mbw * 16;    f->cstride = f->c444 ? mbw * 16 : mbw * 8;
   f->mbh_frame = mbh; f->field = 0;
   f->cmbh = (sps->chroma_format_idc >= 2) ? 16 : 8;
   f->ysb = f->ystride; f->csb = f->cstride;

   ylen      = ((size_t)f->ysb * mbh * 16) << f->bsh;
   clen      = ((size_t)f->csb * mbh * f->cmbh) << f->bsh;
   o_ub      = RH264_ARENA_NEXT(0,    ylen);
   o_vb      = RH264_ARENA_NEXT(o_ub, clen);
   plane_len = RH264_ARENA_NEXT(o_vb, clen);

   grid      = (size_t)mbw * 4 * mbh * 4;
   mbs       = (size_t)mbw * mbh;
   /* A reference's own grids are made on demand (rh264_frame_own_mvg):
    * a frame picture shares the working grid, and only a field pair
    * copies into grids of its own. Carving them here allocated and
    * zeroed two full grids per slot that nothing used. */
   mvlen     = 0;
   (void)with_mv;
   o_nzl     = RH264_ARENA_NEXT(0,         grid);
   /* the chroma coefficient-count grids are luma shaped in 4:4:4 and
    * never larger than that in 4:2:x */
   o_nzc0    = RH264_ARENA_NEXT(o_nzl,     grid);
   o_nzc1    = RH264_ARENA_NEXT(o_nzc0,    grid);
   o_mbqp    = RH264_ARENA_NEXT(o_nzc1,    grid);
   o_mbt8    = RH264_ARENA_NEXT(o_mbqp,    mbs);
   o_mbslice = RH264_ARENA_NEXT(o_mbt8,    mbs);
   o_mvg     = RH264_ARENA_NEXT(o_mbslice, mbs);
   o_mvg2    = RH264_ARENA_NEXT(o_mvg,     mvlen);
   o_row     = RH264_ARENA_NEXT(o_mvg2,    mvlen);
   rowlen    = with_scratch ? ((size_t)mbw + 2) * sizeof(struct rh264_cbf_s) : 0;
   amlen     = with_scratch ? grid * 2 * sizeof(int16_t) : 0;
   o_toprow  = RH264_ARENA_NEXT(o_row,     rowlen);
   o_skiprow = RH264_ARENA_NEXT(o_toprow,  rowlen);
   o_topskip = RH264_ARENA_NEXT(o_skiprow, with_scratch ? (size_t)mbw + 2 : 0);
   o_typerow = RH264_ARENA_NEXT(o_topskip, with_scratch ? (size_t)mbw + 2 : 0);
   o_toptype = RH264_ARENA_NEXT(o_typerow, with_scratch ? (size_t)mbw + 2 : 0);
   o_am0     = RH264_ARENA_NEXT(o_toptype, with_scratch ? (size_t)mbw + 2 : 0);
   o_am1     = RH264_ARENA_NEXT(o_am0,     amlen);
   o_bipred  = RH264_ARENA_NEXT(o_am1,     amlen);
   o_mc      = RH264_ARENA_NEXT(o_bipred,  with_scratch ? (size_t)6 * 256 * 2 : 0);
   o_tbsave  = RH264_ARENA_NEXT(o_mc,      with_scratch ? (size_t)4096 : 0);
   meta_len  = RH264_ARENA_NEXT(o_tbsave,  with_scratch ? sizeof(struct rh264_tb_save_s) : 0);

   f->planes = with_samples ? rh264_planes_new(plane_len) : NULL;
   f->plane_len = plane_len;
   f->meta   = (uint8_t*)calloc(meta_len, 1);
   if ((with_samples && !f->planes) || !f->meta)
   {
      rh264_frame_free(f);
      return -1;
   }
   f->Yb     = f->planes;
   f->Ub     = f->planes ? f->planes + o_ub : NULL;
   f->Vb     = f->planes ? f->planes + o_vb : NULL;
   f->Y = f->Yb; f->U = f->Ub; f->V = f->Vb;
   f->i4mode = f->meta;
   f->nzL    = f->meta + o_nzl;
   f->nzC[0] = f->meta + o_nzc0;
   f->nzC[1] = f->meta + o_nzc1;
   f->mbqp   = f->meta + o_mbqp;
   f->mbt8   = f->meta + o_mbt8;
   f->mbslice= f->meta + o_mbslice;
   if (with_mv)
   {
      f->mvg  = NULL;
      f->mvg2 = NULL;
      f->mvg_meta  = NULL;
      f->mvg2_meta = NULL;
      f->mvg_cells = grid;
   }
   if (with_scratch)
   {
      f->scr_row     = (struct rh264_cbf_s*)(f->meta + o_row);
      f->scr_toprow  = (struct rh264_cbf_s*)(f->meta + o_toprow);
      f->scr_skiprow = f->meta + o_skiprow;
      f->scr_topskip = f->meta + o_topskip;
      f->scr_typerow = f->meta + o_typerow;
      f->scr_toptype = f->meta + o_toptype;
      f->scr_am0     = (int16_t*)(f->meta + o_am0);
      f->scr_am1     = (int16_t*)(f->meta + o_am1);
      f->scr_bipred  = f->meta + o_bipred;
      f->scr_mc      = f->meta + o_mc;
      f->scr_tbsave  = (struct rh264_tb_save_s*)(f->meta + o_tbsave);
   }
   return 0;
}

static void rh264_frame_reset_ex(rh264_frame *f, int keep)
{
   int mbw = f->mbw, mbh = f->mbh_frame;
   if (!keep) {
   memset(f->Yb, 0, RH264_OFF(f, (size_t)f->ysb * mbh * 16));
   memset(f->Ub, 0, RH264_OFF(f, (size_t)f->csb * mbh * f->cmbh));
   memset(f->Vb, 0, RH264_OFF(f, (size_t)f->csb * mbh * f->cmbh)); }
   memset(f->i4mode, 0xff, (size_t)mbw * 4 * mbh * 4);
   memset(f->nzL, 0, (size_t)mbw * 4 * mbh * 4);
   memset(f->nzC[0], 0, (size_t)mbw*2 * mbh*(f->cmbh/4));
   memset(f->nzC[1], 0, (size_t)mbw*2 * mbh*(f->cmbh/4));
   memset(f->mbqp, 0, (size_t)mbw * mbh);
   memset(f->mbt8, 0, (size_t)mbw * mbh);
   memset(f->mbslice, 0, (size_t)mbw * mbh);
}

static void rh264_frame_set_field(rh264_frame *f, int fl)
{
   /* a bottom-field view reads the motion its own field wrote */
   if (fl == 2 && f->mvg2) f->mvg = f->mvg2;
   f->field=fl;
   if(fl){ f->Y=f->Yb+RH264_OFF(f, fl==2?f->ysb:0); f->U=f->Ub+RH264_OFF(f, fl==2?f->csb:0);
           f->V=f->Vb+RH264_OFF(f, fl==2?f->csb:0);
           f->ystride=f->ysb*2; f->cstride=f->csb*2; f->mbh=f->mbh_frame/2; }
   else  { f->Y=f->Yb; f->U=f->Ub; f->V=f->Vb;
           f->ystride=f->ysb; f->cstride=f->csb; f->mbh=f->mbh_frame; }
}

/* (Re)allocate the frame buffers if the SPS geometry changed. */
/* The context's working frame and grids, for the sequence's current
 * geometry: made when the context has none, made afresh when the
 * geometry changed under it, and otherwise made the context's own
 * again where the last picture left them shared. */
static int rh264_ctx_prepare(rh264_video *v, rh264_pic_ctx *c)
{
   if (c->f.Yb && c->alloc_w == v->sps.frame_width
              && c->alloc_h == v->sps.frame_height)
   {
      if (c->pic_mvg && rh264_mvg_refs(c->pic_mvg) > 1)
      {
         rh264_mv *fresh = rh264_mvg_new((size_t)(c->f.mbw * 4) * (c->f.mbh * 4));
         if (!fresh)
            return -1;
         rh264_mvg_release(c->pic_mvg);
         c->pic_mvg = fresh;
         c->slice_mvg = NULL;
      }
      return rh264_frame_own_planes(&c->f, c->f.plane_len);
   }
   if (rh264_frame_alloc(&c->f, &v->sps, RH264_FRAME_ALLOC_WORKING) != 0) return -1;
   free(c->mvg);
   rh264_mvg_release(c->pic_mvg);
   {
      int gwmax = c->f.mbw * 4;
      int ghmax = (v->sps.frame_mbs > 0 ? v->sps.frame_mbs
                 : (v->sps.frame_height + 15) / 16) * 4;
      c->mvg = (rh264_mv*)calloc((size_t)gwmax * ghmax, sizeof(rh264_mv));
      c->pic_mvg = rh264_mvg_new((size_t)gwmax * ghmax);
      c->slice_mvg = NULL;
      if (!c->mvg || !c->pic_mvg) return -1;
   }
   c->pic_open = 0;
   c->alloc_w  = v->sps.frame_width;
   c->alloc_h  = v->sps.frame_height;
   return 0;
}

/* The sequence's DPB and output queue for the current geometry, then
 * the current context's working frame. The DPB is remade only when
 * the geometry changed; a context arriving at a geometry the sequence
 * already has takes only its own frame. */
static int rh264_frame_alloc_if_needed(rh264_video *v)
{
   if (!v->have_sps) return -1;
   if (v->alloc_w != v->sps.frame_width || v->alloc_h != v->sps.frame_height)
   {
      int n = v->sps.max_num_ref_frames, i;
      if (n < 1) n = 1;
      if (n > RH264_MAX_REFS) n = RH264_MAX_REFS;
      for (i = 0; i < v->dpb_size; i++) rh264_frame_free(&v->dpb[i]);
      for (i = 0; i < n; i++)
         if (rh264_frame_alloc(&v->dpb[i], &v->sps, RH264_FRAME_ALLOC_REF) != 0) return -1;
      v->dpb_size = n;
      v->dpb_len  = 0;
      for (i = 0; i < RH264_OUT_SLOTS; i++)
      { rh264_frame_free(&v->out[i]); v->out_used[i] = 0; }
      v->out_len = 0; v->out_show = -1;
      v->max_lt_idx = -1;
      v->have_ref = 0;
      {
         int d;
         if (v->sps.vui_num_reorder >= 0) d = v->sps.vui_num_reorder;
         else if (v->sps.profile_idc == 66) d = 0;
         else d = v->sps.max_num_ref_frames;
         if (d > RH264_MAX_REFS) d = RH264_MAX_REFS;
         if (d < 0) d = 0;
         v->reorder_delay = d;
      }
      v->alloc_w = v->sps.frame_width;
      v->alloc_h = v->sps.frame_height;
   }
   /* The context is readied when its picture opens (rh264_video_next_ctx);
    * here only a first frame, for a slice arriving before any open. */
   if (!v->cur->f.Yb)
      return rh264_ctx_prepare(v, v->cur);
   return 0;
}

/* ---- public API ---- */

static uint8_t *rh264_unescape_scratch(struct rh264_video *v,
      const uint8_t *nal, size_t len, size_t *out_size)
{
   uint8_t *r; size_t i, o = 0;
   size_t need = len ? len : 1;
   if (need > v->cur->rbsp_scratch_cap)
   {
      if (!(r = (uint8_t*)realloc(v->cur->rbsp_scratch, need))) return NULL;
      v->cur->rbsp_scratch     = r;
      v->cur->rbsp_scratch_cap = need;
   }
   r = v->cur->rbsp_scratch;
   for (i = 0; i < len; i++)
   {
      if (i >= 2 && nal[i] == 0x03 && nal[i-1] == 0x00 && nal[i-2] == 0x00
            && i + 1 < len && nal[i+1] <= 0x03) continue;
      r[o++] = nal[i];
   }
   *out_size = o; return r;
}

rh264_video *rh264_video_open(void)
{
   rh264_video *v = (rh264_video*)calloc(1, sizeof(*v));
   if (!v) return NULL;
   v->cur  = &v->ctx[0];
   v->nctx = 1;
   {
      int k;
      for (k = 0; k < RH264_MAX_CTX; k++)
         v->ctx[k].owner = v;
   }
   /* Invert the CAVLC tables for this instance; on failure the decoder
    * runs on the serial matchers instead. */
   v->vlc_ready = rh264_vlc_init(&v->vlc) == 0;
   /* Annex-B until rh264_video_set_extradata reports the avcC length
    * size; sample format is decided by that call, never sniffed. */
   v->nal_length_size = 0;
   v->out_show = -1;
   return v;
}

static void rh264_hw_free(rh264_video *v);

void rh264_video_close(rh264_video *v)
{
   if (!v) return;
   rh264_hw_free(v);
   {
      int i;
      for (i = 0; i < RH264_MAX_CTX; i++)
      {
#ifdef HAVE_THREADS
         rh264_ctx_join(v, &v->ctx[i]);
#endif
         while (v->ctx[i].jobs)
         {
            rh264_slice_job *j = v->ctx[i].jobs;
            v->ctx[i].jobs = j->next;
            free(j->rbsp);
            free(j);
         }
         rh264_ctx_release_refs(&v->ctx[i]);
         rh264_frame_free(&v->ctx[i].f);
         free(v->ctx[i].rbsp_scratch);
         free(v->ctx[i].mvg);
         rh264_mvg_release(v->ctx[i].pic_mvg);
      }
      for (i = 0; i < RH264_MAX_REFS; i++) rh264_frame_free(&v->dpb[i]);
      for (i = 0; i < RH264_OUT_SLOTS; i++) rh264_frame_free(&v->out[i]);
      for (;;)
      {
         rh264_slice_job *j = rh264_job_take(v);
         if (!j)
            break;
         free(j->rbsp);
         free(j);
      }
   }
   rh264_vlc_free(&v->vlc);
   free(v);
}

/* parse one SPS or PPS NAL (RBSP already unescaped elsewhere). */
static void rh264_video_take_ps(rh264_video *v, const uint8_t *nal, size_t len)
{
   int type;
   size_t rl;
   uint8_t *rbsp;
   if (len < 1) return;
   type = nal[0] & 0x1f;
   rbsp = rh264_unescape(nal + 1, len - 1, &rl);
   if (!rbsp) return;
   if (type == 7) { if (rh264_parse_sps(rbsp, rl, &v->sps)) v->have_sps = 1; }
   else if (type == 8) { if (rh264_parse_pps(rbsp, rl, &v->pps,
         v->have_sps && v->sps.chroma_format_idc == 3)) v->have_pps = 1; }
   free(rbsp);
}

int rh264_video_set_extradata(rh264_video *v, const uint8_t *avcc, size_t len)
{
   size_t p = 0;
   int i, n;
   if (!v || !avcc || len < 7) return -1;
   /* AVCDecoderConfigurationRecord:
    * [0]=1 [1]=profile [2]=compat [3]=level
    * [4]=0xFC|(lengthSizeMinusOne) [5]=0xE0|numSPS ... */
   v->nal_length_size = (avcc[4] & 0x03) + 1;
   n = avcc[5] & 0x1f;
   p = 6;
   for (i = 0; i < n; i++)
   {
      size_t sl;
      if (p + 2 > len) return -1;
      sl = ((size_t)avcc[p] << 8) | avcc[p+1]; p += 2;
      if (p + sl > len) return -1;
      rh264_video_take_ps(v, avcc + p, sl);
      p += sl;
   }
   if (p + 1 > len) return -1;
   n = avcc[p++]; /* numPPS */
   for (i = 0; i < n; i++)
   {
      size_t sl;
      if (p + 2 > len) return -1;
      sl = ((size_t)avcc[p] << 8) | avcc[p+1]; p += 2;
      if (p + sl > len) return -1;
      rh264_video_take_ps(v, avcc + p, sl);
      p += sl;
   }
   return (v->have_sps && v->have_pps) ? 0 : -1;
}

/* Decode a single IDR NAL into v->cur->f (already sized). Returns 0 on success. */
/* ==================== CABAC (Main-profile I-slice) ==================== */
/* Clean-room CABAC entropy decoder for Main-profile intra frames.
 * Byte-exact vs openh264 for I_4x4 / I_16x16 macroblocks (4:2:0). */

/* ---- rh264_cabac_tables.h ---- */
/* rh264 CABAC core tables (H.264 clause 9.3.3.2). These are the universal
 * spec tables, identical across all decoders. */

/* Table 9-33, rangeTabLPS[pStateIdx][qCodIRangeIdx], laid out for the
 * engine: entry qCodIRangeIdx * 128 + state, state being pStateIdx * 2 |
 * valMPS as the contexts hold it.  That index is (range & 0xC0) * 2 +
 * state, range being 256..510 at every bin: a mask and an add, no
 * shifts on the per-bin dependency chain. */
static const uint8_t rh264_cb_lps[512]={
   128,128,128,128,128,128,123,123,116,116,111,111,105,105,100,100,
    95, 95, 90, 90, 85, 85, 81, 81, 77, 77, 73, 73, 69, 69, 66, 66,
    62, 62, 59, 59, 56, 56, 53, 53, 51, 51, 48, 48, 46, 46, 43, 43,
    41, 41, 39, 39, 37, 37, 35, 35, 33, 33, 32, 32, 30, 30, 29, 29,
    27, 27, 26, 26, 24, 24, 23, 23, 22, 22, 21, 21, 20, 20, 19, 19,
    18, 18, 17, 17, 16, 16, 15, 15, 14, 14, 14, 14, 13, 13, 12, 12,
    12, 12, 11, 11, 11, 11, 10, 10, 10, 10,  9,  9,  9,  9,  8,  8,
     8,  8,  7,  7,  7,  7,  7,  7,  6,  6,  6,  6,  6,  6,  2,  2,
   176,176,167,167,158,158,150,150,142,142,135,135,128,128,122,122,
   116,116,110,110,104,104, 99, 99, 94, 94, 89, 89, 85, 85, 80, 80,
    76, 76, 72, 72, 69, 69, 65, 65, 62, 62, 59, 59, 56, 56, 53, 53,
    50, 50, 48, 48, 45, 45, 43, 43, 41, 41, 39, 39, 37, 37, 35, 35,
    33, 33, 31, 31, 30, 30, 28, 28, 27, 27, 26, 26, 24, 24, 23, 23,
    22, 22, 21, 21, 20, 20, 19, 19, 18, 18, 17, 17, 16, 16, 15, 15,
    14, 14, 14, 14, 13, 13, 12, 12, 12, 12, 11, 11, 11, 11, 10, 10,
     9,  9,  9,  9,  9,  9,  8,  8,  8,  8,  7,  7,  7,  7,  2,  2,
   208,208,197,197,187,187,178,178,169,169,160,160,152,152,144,144,
   137,137,130,130,123,123,117,117,111,111,105,105,100,100, 95, 95,
    90, 90, 86, 86, 81, 81, 77, 77, 73, 73, 69, 69, 66, 66, 63, 63,
    59, 59, 56, 56, 54, 54, 51, 51, 48, 48, 46, 46, 43, 43, 41, 41,
    39, 39, 37, 37, 35, 35, 33, 33, 32, 32, 30, 30, 29, 29, 27, 27,
    26, 26, 25, 25, 23, 23, 22, 22, 21, 21, 20, 20, 19, 19, 18, 18,
    17, 17, 16, 16, 15, 15, 15, 15, 14, 14, 13, 13, 12, 12, 12, 12,
    11, 11, 11, 11, 10, 10, 10, 10,  9,  9,  9,  9,  8,  8,  2,  2,
   240,240,227,227,216,216,205,205,195,195,185,185,175,175,166,166,
   158,158,150,150,142,142,135,135,128,128,122,122,116,116,110,110,
   104,104, 99, 99, 94, 94, 89, 89, 85, 85, 80, 80, 76, 76, 72, 72,
    69, 69, 65, 65, 62, 62, 59, 59, 56, 56, 53, 53, 50, 50, 48, 48,
    45, 45, 43, 43, 41, 41, 39, 39, 37, 37, 35, 35, 33, 33, 31, 31,
    30, 30, 28, 28, 27, 27, 25, 25, 24, 24, 23, 23, 22, 22, 21, 21,
    20, 20, 19, 19, 18, 18, 17, 17, 16, 16, 15, 15, 14, 14, 14, 14,
    13, 13, 12, 12, 12, 12, 11, 11, 11, 11, 10, 10,  9,  9,  2,  2};

/* Table 9-34: transIdxLPS / transIdxMPS (pStateIdx transitions) */
static const uint8_t rh264_transIdxLPS[64]={
  0, 0, 1, 2, 2, 4, 4, 5, 6, 7, 8, 9, 9,11,11,12,
 13,13,15,15,16,16,18,18,19,19,21,21,22,22,23,24,
 24,25,26,26,27,27,28,29,29,30,30,30,31,32,32,33,
 33,33,34,34,35,35,35,36,36,36,37,37,37,38,38,63};
static const uint8_t rh264_transIdxMPS[64]={
  1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,
 17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
 33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,
 49,50,51,52,53,54,55,56,57,58,59,60,61,62,62,63};

/* ---- rh264_cabac_init.h ---- */
/* rh264 CABAC context init tables (m,n), spec Tables 9-12..9-33.
   Contexts 0..459 were extracted from libopenh264 g_kiCabacGlobalContextIdx
   (variants 0..3); 460..1023 - the 4:4:4 coded_block_flag / significance /
   level contexts of ctxBlockCat 5..13 (7.4.2.1.1 ChromaArrayType 3) - were
   extracted from libavcodec cabac_context_init_I / _PB, whose entries 0..459
   agree with the libopenh264 ones in every position (checked, both slice
   kinds and all three cabac_init_idc). */
#define RH264_CABAC_NCTX 1024
static const int8_t rh264_cabac_init_I[1024][2]={
  {20,-15},{2,54},{3,74},{20,-15},{2,54},{3,74},{-28,127},{-23,104},{-6,53},
  {-1,54},{7,51},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
  {0,0},{0,0},{0,0},{0,41},{0,63},{0,63},{0,63},{-9,83},{4,86},{0,97},{-7,72},
  {13,41},{3,62},{0,11},{1,55},{0,69},{-17,127},{-13,102},{0,82},{-7,74},
  {-21,107},{-27,127},{-31,127},{-24,127},{-18,95},{-27,127},{-21,114},
  {-30,127},{-17,123},{-12,115},{-16,122},{-11,115},{-12,63},{-2,68},{-15,84},
  {-13,104},{-3,70},{-8,93},{-10,90},{-30,127},{-1,74},{-6,97},{-7,91},
  {-20,127},{-4,56},{-5,82},{-7,76},{-22,125},{-7,93},{-11,87},{-3,77},
  {-5,71},{-4,63},{-4,68},{-12,84},{-7,62},{-7,65},{8,61},{5,56},{-2,66},
  {1,64},{0,61},{-2,78},{1,50},{7,52},{10,35},{0,44},{11,38},{1,45},{0,46},
  {5,44},{31,17},{1,51},{7,50},{28,19},{16,33},{14,62},{-13,108},{-15,100},
  {-13,101},{-13,91},{-12,94},{-10,88},{-16,84},{-10,86},{-7,83},{-13,87},
  {-19,94},{1,70},{0,72},{-5,74},{18,59},{-8,102},{-15,100},{0,95},{-4,75},
  {2,72},{-11,75},{-3,71},{15,46},{-13,69},{0,62},{0,65},{21,37},{-15,72},
  {9,57},{16,54},{0,62},{12,72},{24,0},{15,9},{8,25},{13,18},{15,9},{13,19},
  {10,37},{12,18},{6,29},{20,33},{15,30},{4,45},{1,58},{0,62},{7,61},{12,38},
  {11,45},{15,39},{11,42},{13,44},{16,45},{12,41},{10,49},{30,34},{18,42},
  {10,55},{17,51},{17,46},{0,89},{26,-19},{22,-17},{26,-17},{30,-25},{28,-20},
  {33,-23},{37,-27},{33,-23},{40,-28},{38,-17},{33,-11},{40,-15},{41,-6},
  {38,1},{41,17},{30,-6},{27,3},{26,22},{37,-16},{35,-4},{38,-8},{38,-3},
  {37,3},{38,5},{42,0},{35,16},{39,22},{14,48},{27,37},{21,60},{12,68},{2,97},
  {-3,71},{-6,42},{-5,50},{-3,54},{-2,62},{0,58},{1,63},{-2,72},{-1,74},
  {-9,91},{-5,67},{-5,27},{-3,39},{-2,44},{0,46},{-16,64},{-8,68},{-10,78},
  {-6,77},{-10,86},{-12,92},{-15,55},{-10,60},{-6,62},{-4,65},{-12,73},
  {-8,76},{-7,80},{-9,88},{-17,110},{-11,97},{-20,84},{-11,79},{-6,73},
  {-4,74},{-13,86},{-13,96},{-11,97},{-19,117},{-8,78},{-5,33},{-4,48},
  {-2,53},{-3,62},{-13,71},{-10,79},{-12,86},{-13,90},{-14,97},{0,0},{-6,93},
  {-6,84},{-8,79},{0,66},{-1,71},{0,62},{-2,60},{-2,59},{-5,75},{-3,62},
  {-4,58},{-9,66},{-1,79},{0,71},{3,68},{10,44},{-7,62},{15,36},{14,40},
  {16,27},{12,29},{1,44},{20,36},{18,32},{5,42},{1,48},{10,62},{17,46},{9,64},
  {-12,104},{-11,97},{-16,96},{-7,88},{-8,85},{-7,85},{-9,85},{-13,88},{4,66},
  {-3,77},{-3,76},{-6,76},{10,58},{-1,76},{-1,83},{-7,99},{-14,95},{2,95},
  {0,76},{-5,74},{0,70},{-11,75},{1,68},{0,65},{-14,73},{3,62},{4,62},{-1,68},
  {-13,75},{11,55},{5,64},{12,70},{15,6},{6,19},{7,16},{12,14},{18,13},
  {13,11},{13,15},{15,16},{12,23},{13,23},{15,20},{14,26},{14,44},{17,40},
  {17,47},{24,17},{21,21},{25,22},{31,27},{22,29},{19,35},{14,50},{10,57},
  {7,63},{-2,77},{-4,82},{-3,94},{9,69},{-12,109},{36,-35},{36,-34},{32,-26},
  {37,-30},{44,-32},{34,-18},{34,-15},{40,-15},{33,-7},{35,-5},{33,0},{38,2},
  {33,13},{23,35},{13,58},{29,-3},{26,0},{22,30},{31,-7},{35,-15},{34,-3},
  {34,3},{36,-1},{34,5},{32,11},{35,5},{34,12},{39,11},{30,29},{34,26},
  {29,39},{19,66},{31,21},{31,31},{25,50},{-17,120},{-20,112},{-18,114},
  {-11,85},{-15,92},{-14,89},{-26,71},{-15,81},{-14,80},{0,68},{-14,70},
  {-24,56},{-23,68},{-24,50},{-11,74},{23,-13},{26,-13},{40,-15},{49,-14},
  {44,3},{45,6},{44,34},{33,54},{19,82},{-3,75},{-1,23},{1,34},{1,43},{0,54},
  {-2,55},{0,61},{1,64},{0,68},{-9,92},{-14,106},{-13,97},{-15,90},{-12,90},
  {-18,88},{-10,73},{-9,79},{-14,86},{-10,73},{-10,70},{-10,69},{-5,66},
  {-9,64},{-5,58},{2,59},{21,-10},{24,-11},{28,-8},{28,-1},{29,3},{29,9},
  {35,20},{29,36},{14,67},{-17,123},{-12,115},{-16,122},{-11,115},{-12,63},
  {-2,68},{-15,84},{-13,104},{-3,70},{-8,93},{-10,90},{-30,127},{-17,123},
  {-12,115},{-16,122},{-11,115},{-12,63},{-2,68},{-15,84},{-13,104},{-3,70},
  {-8,93},{-10,90},{-30,127},{-7,93},{-11,87},{-3,77},{-5,71},{-4,63},{-4,68},
  {-12,84},{-7,62},{-7,65},{8,61},{5,56},{-2,66},{1,64},{0,61},{-2,78},{1,50},
  {7,52},{10,35},{0,44},{11,38},{1,45},{0,46},{5,44},{31,17},{1,51},{7,50},
  {28,19},{16,33},{14,62},{-13,108},{-15,100},{-13,101},{-13,91},{-12,94},
  {-10,88},{-16,84},{-10,86},{-7,83},{-13,87},{-19,94},{1,70},{0,72},{-5,74},
  {18,59},{-7,93},{-11,87},{-3,77},{-5,71},{-4,63},{-4,68},{-12,84},{-7,62},
  {-7,65},{8,61},{5,56},{-2,66},{1,64},{0,61},{-2,78},{1,50},{7,52},{10,35},
  {0,44},{11,38},{1,45},{0,46},{5,44},{31,17},{1,51},{7,50},{28,19},{16,33},
  {14,62},{-13,108},{-15,100},{-13,101},{-13,91},{-12,94},{-10,88},{-16,84},
  {-10,86},{-7,83},{-13,87},{-19,94},{1,70},{0,72},{-5,74},{18,59},{24,0},
  {15,9},{8,25},{13,18},{15,9},{13,19},{10,37},{12,18},{6,29},{20,33},{15,30},
  {4,45},{1,58},{0,62},{7,61},{12,38},{11,45},{15,39},{11,42},{13,44},{16,45},
  {12,41},{10,49},{30,34},{18,42},{10,55},{17,51},{17,46},{0,89},{26,-19},
  {22,-17},{26,-17},{30,-25},{28,-20},{33,-23},{37,-27},{33,-23},{40,-28},
  {38,-17},{33,-11},{40,-15},{41,-6},{38,1},{41,17},{24,0},{15,9},{8,25},
  {13,18},{15,9},{13,19},{10,37},{12,18},{6,29},{20,33},{15,30},{4,45},{1,58},
  {0,62},{7,61},{12,38},{11,45},{15,39},{11,42},{13,44},{16,45},{12,41},
  {10,49},{30,34},{18,42},{10,55},{17,51},{17,46},{0,89},{26,-19},{22,-17},
  {26,-17},{30,-25},{28,-20},{33,-23},{37,-27},{33,-23},{40,-28},{38,-17},
  {33,-11},{40,-15},{41,-6},{38,1},{41,17},{-17,120},{-20,112},{-18,114},
  {-11,85},{-15,92},{-14,89},{-26,71},{-15,81},{-14,80},{0,68},{-14,70},
  {-24,56},{-23,68},{-24,50},{-11,74},{-14,106},{-13,97},{-15,90},{-12,90},
  {-18,88},{-10,73},{-9,79},{-14,86},{-10,73},{-10,70},{-10,69},{-5,66},
  {-9,64},{-5,58},{2,59},{23,-13},{26,-13},{40,-15},{49,-14},{44,3},{45,6},
  {44,34},{33,54},{19,82},{21,-10},{24,-11},{28,-8},{28,-1},{29,3},{29,9},
  {35,20},{29,36},{14,67},{-3,75},{-1,23},{1,34},{1,43},{0,54},{-2,55},{0,61},
  {1,64},{0,68},{-9,92},{-17,120},{-20,112},{-18,114},{-11,85},{-15,92},
  {-14,89},{-26,71},{-15,81},{-14,80},{0,68},{-14,70},{-24,56},{-23,68},
  {-24,50},{-11,74},{-14,106},{-13,97},{-15,90},{-12,90},{-18,88},{-10,73},
  {-9,79},{-14,86},{-10,73},{-10,70},{-10,69},{-5,66},{-9,64},{-5,58},{2,59},
  {23,-13},{26,-13},{40,-15},{49,-14},{44,3},{45,6},{44,34},{33,54},{19,82},
  {21,-10},{24,-11},{28,-8},{28,-1},{29,3},{29,9},{35,20},{29,36},{14,67},
  {-3,75},{-1,23},{1,34},{1,43},{0,54},{-2,55},{0,61},{1,64},{0,68},{-9,92},
  {-6,93},{-6,84},{-8,79},{0,66},{-1,71},{0,62},{-2,60},{-2,59},{-5,75},
  {-3,62},{-4,58},{-9,66},{-1,79},{0,71},{3,68},{10,44},{-7,62},{15,36},
  {14,40},{16,27},{12,29},{1,44},{20,36},{18,32},{5,42},{1,48},{10,62},
  {17,46},{9,64},{-12,104},{-11,97},{-16,96},{-7,88},{-8,85},{-7,85},{-9,85},
  {-13,88},{4,66},{-3,77},{-3,76},{-6,76},{10,58},{-1,76},{-1,83},{-6,93},
  {-6,84},{-8,79},{0,66},{-1,71},{0,62},{-2,60},{-2,59},{-5,75},{-3,62},
  {-4,58},{-9,66},{-1,79},{0,71},{3,68},{10,44},{-7,62},{15,36},{14,40},
  {16,27},{12,29},{1,44},{20,36},{18,32},{5,42},{1,48},{10,62},{17,46},{9,64},
  {-12,104},{-11,97},{-16,96},{-7,88},{-8,85},{-7,85},{-9,85},{-13,88},{4,66},
  {-3,77},{-3,76},{-6,76},{10,58},{-1,76},{-1,83},{15,6},{6,19},{7,16},
  {12,14},{18,13},{13,11},{13,15},{15,16},{12,23},{13,23},{15,20},{14,26},
  {14,44},{17,40},{17,47},{24,17},{21,21},{25,22},{31,27},{22,29},{19,35},
  {14,50},{10,57},{7,63},{-2,77},{-4,82},{-3,94},{9,69},{-12,109},{36,-35},
  {36,-34},{32,-26},{37,-30},{44,-32},{34,-18},{34,-15},{40,-15},{33,-7},
  {35,-5},{33,0},{38,2},{33,13},{23,35},{13,58},{15,6},{6,19},{7,16},{12,14},
  {18,13},{13,11},{13,15},{15,16},{12,23},{13,23},{15,20},{14,26},{14,44},
  {17,40},{17,47},{24,17},{21,21},{25,22},{31,27},{22,29},{19,35},{14,50},
  {10,57},{7,63},{-2,77},{-4,82},{-3,94},{9,69},{-12,109},{36,-35},{36,-34},
  {32,-26},{37,-30},{44,-32},{34,-18},{34,-15},{40,-15},{33,-7},{35,-5},
  {33,0},{38,2},{33,13},{23,35},{13,58},{-3,71},{-6,42},{-5,50},{-3,54},
  {-2,62},{0,58},{1,63},{-2,72},{-1,74},{-9,91},{-5,67},{-5,27},{-3,39},
  {-2,44},{0,46},{-16,64},{-8,68},{-10,78},{-6,77},{-10,86},{-12,92},{-15,55},
  {-10,60},{-6,62},{-4,65},{-12,73},{-8,76},{-7,80},{-9,88},{-17,110},{-3,71},
  {-6,42},{-5,50},{-3,54},{-2,62},{0,58},{1,63},{-2,72},{-1,74},{-9,91},
  {-5,67},{-5,27},{-3,39},{-2,44},{0,46},{-16,64},{-8,68},{-10,78},{-6,77},
  {-10,86},{-12,92},{-15,55},{-10,60},{-6,62},{-4,65},{-12,73},{-8,76},
  {-7,80},{-9,88},{-17,110},{-3,70},{-8,93},{-10,90},{-30,127},{-3,70},
  {-8,93},{-10,90},{-30,127},{-3,70},{-8,93},{-10,90},{-30,127}};
static const int8_t rh264_cabac_init_PB[3][1024][2]={
{
  {20,-15},{2,54},{3,74},{20,-15},{2,54},{3,74},{-28,127},{-23,104},{-6,53},
  {-1,54},{7,51},{23,33},{23,2},{21,0},{1,9},{0,49},{-37,118},{5,57},{-13,78},
  {-11,65},{1,62},{12,49},{-4,73},{17,50},{18,64},{9,43},{29,0},{26,67},
  {16,90},{9,104},{-46,127},{-20,104},{1,67},{-13,78},{-11,65},{1,62},{-6,86},
  {-17,95},{-6,61},{9,45},{-3,69},{-6,81},{-11,96},{6,55},{7,67},{-5,86},
  {2,88},{0,58},{-3,76},{-10,94},{5,54},{4,69},{-3,81},{0,88},{-7,67},{-5,74},
  {-4,74},{-5,80},{-7,72},{1,58},{0,41},{0,63},{0,63},{0,63},{-9,83},{4,86},
  {0,97},{-7,72},{13,41},{3,62},{0,45},{-4,78},{-3,96},{-27,126},{-28,98},
  {-25,101},{-23,67},{-28,82},{-20,94},{-16,83},{-22,110},{-21,91},{-18,102},
  {-13,93},{-29,127},{-7,92},{-5,89},{-7,96},{-13,108},{-3,46},{-1,65},
  {-1,57},{-9,93},{-3,74},{-9,92},{-8,87},{-23,126},{5,54},{6,60},{6,59},
  {6,69},{-1,48},{0,68},{-4,69},{-8,88},{-2,85},{-6,78},{-1,75},{-7,77},
  {2,54},{5,50},{-3,68},{1,50},{6,42},{-4,81},{1,63},{-4,70},{0,67},{2,57},
  {-2,76},{11,35},{4,64},{1,61},{11,35},{18,25},{12,24},{13,29},{13,36},
  {-10,93},{-7,73},{-2,73},{13,46},{9,49},{-7,100},{9,53},{2,53},{5,53},
  {-2,61},{0,56},{0,56},{-13,63},{-5,60},{-1,62},{4,57},{-6,69},{4,57},
  {14,39},{4,51},{13,68},{3,64},{1,61},{9,63},{7,50},{16,39},{5,44},{4,52},
  {11,48},{-5,60},{-1,59},{0,59},{22,33},{5,44},{14,43},{-1,78},{0,60},{9,69},
  {11,28},{2,40},{3,44},{0,49},{0,46},{2,44},{2,51},{0,47},{4,39},{2,62},
  {6,46},{0,54},{3,54},{2,58},{4,63},{6,51},{6,57},{7,53},{6,52},{6,55},
  {11,45},{14,36},{8,53},{-1,82},{7,55},{-3,78},{15,46},{22,31},{-1,84},
  {25,7},{30,-7},{28,3},{28,4},{32,0},{34,-1},{30,6},{30,6},{32,9},{31,19},
  {26,27},{26,30},{37,20},{28,34},{17,70},{1,67},{5,59},{9,67},{16,30},
  {18,32},{18,35},{22,29},{24,31},{23,38},{18,43},{20,41},{11,63},{9,59},
  {9,64},{-1,94},{-2,89},{-9,108},{-6,76},{-2,44},{0,45},{0,52},{-3,64},
  {-2,59},{-4,70},{-4,75},{-8,82},{-17,102},{-9,77},{3,24},{0,42},{0,48},
  {0,55},{-6,59},{-7,71},{-12,83},{-11,87},{-30,119},{1,58},{-3,29},{-1,36},
  {1,38},{2,43},{-6,55},{0,58},{0,64},{-3,74},{-10,90},{0,70},{-4,29},{5,31},
  {7,42},{1,59},{-2,58},{-3,72},{-3,81},{-11,97},{0,58},{8,5},{10,14},{14,18},
  {13,27},{2,40},{0,58},{-3,70},{-6,79},{-8,85},{0,0},{-13,106},{-16,106},
  {-10,87},{-21,114},{-18,110},{-14,98},{-22,110},{-21,106},{-18,103},
  {-21,107},{-23,108},{-26,112},{-10,96},{-12,95},{-5,91},{-9,93},{-22,94},
  {-5,86},{9,67},{-4,80},{-10,85},{-1,70},{7,60},{9,58},{5,61},{12,50},
  {15,50},{18,49},{17,54},{10,41},{7,46},{-1,51},{7,49},{8,52},{9,41},{6,47},
  {2,55},{13,41},{10,44},{6,50},{5,53},{13,49},{4,63},{6,64},{-2,69},{-2,59},
  {6,70},{10,44},{9,31},{12,43},{3,53},{14,34},{10,38},{-3,52},{13,40},
  {17,32},{7,44},{7,38},{13,50},{10,57},{26,43},{14,11},{11,14},{9,11},
  {18,11},{21,9},{23,-2},{32,-15},{32,-15},{34,-21},{39,-23},{42,-33},
  {41,-31},{46,-28},{38,-12},{21,29},{45,-24},{53,-45},{48,-26},{65,-43},
  {43,-19},{39,-10},{30,9},{18,26},{20,27},{0,57},{-14,82},{-5,75},{-19,97},
  {-35,125},{27,0},{28,0},{31,-4},{27,6},{34,8},{30,10},{24,22},{33,19},
  {22,32},{26,31},{21,41},{26,44},{23,47},{16,65},{14,71},{8,60},{6,63},
  {17,65},{21,24},{23,20},{26,23},{27,32},{28,23},{28,24},{23,40},{24,32},
  {28,29},{23,42},{19,57},{22,53},{22,61},{11,86},{12,40},{11,51},{14,59},
  {-4,79},{-7,71},{-5,69},{-9,70},{-8,66},{-10,68},{-19,73},{-12,69},{-16,70},
  {-15,67},{-20,62},{-19,70},{-16,66},{-22,65},{-20,63},{9,-2},{26,-9},
  {33,-9},{39,-7},{41,-2},{45,3},{49,9},{45,27},{36,59},{-6,66},{-7,35},
  {-7,42},{-8,45},{-5,48},{-12,56},{-6,60},{-5,62},{-8,66},{-8,76},{-5,85},
  {-6,81},{-10,77},{-7,81},{-17,80},{-18,73},{-4,74},{-10,83},{-9,71},{-9,67},
  {-1,61},{-8,66},{-14,66},{0,59},{2,59},{21,-13},{33,-14},{39,-7},{46,-2},
  {51,2},{60,6},{61,17},{55,34},{42,62},{-7,92},{-5,89},{-7,96},{-13,108},
  {-3,46},{-1,65},{-1,57},{-9,93},{-3,74},{-9,92},{-8,87},{-23,126},{-7,92},
  {-5,89},{-7,96},{-13,108},{-3,46},{-1,65},{-1,57},{-9,93},{-3,74},{-9,92},
  {-8,87},{-23,126},{-2,85},{-6,78},{-1,75},{-7,77},{2,54},{5,50},{-3,68},
  {1,50},{6,42},{-4,81},{1,63},{-4,70},{0,67},{2,57},{-2,76},{11,35},{4,64},
  {1,61},{11,35},{18,25},{12,24},{13,29},{13,36},{-10,93},{-7,73},{-2,73},
  {13,46},{9,49},{-7,100},{9,53},{2,53},{5,53},{-2,61},{0,56},{0,56},{-13,63},
  {-5,60},{-1,62},{4,57},{-6,69},{4,57},{14,39},{4,51},{13,68},{-2,85},
  {-6,78},{-1,75},{-7,77},{2,54},{5,50},{-3,68},{1,50},{6,42},{-4,81},{1,63},
  {-4,70},{0,67},{2,57},{-2,76},{11,35},{4,64},{1,61},{11,35},{18,25},{12,24},
  {13,29},{13,36},{-10,93},{-7,73},{-2,73},{13,46},{9,49},{-7,100},{9,53},
  {2,53},{5,53},{-2,61},{0,56},{0,56},{-13,63},{-5,60},{-1,62},{4,57},{-6,69},
  {4,57},{14,39},{4,51},{13,68},{11,28},{2,40},{3,44},{0,49},{0,46},{2,44},
  {2,51},{0,47},{4,39},{2,62},{6,46},{0,54},{3,54},{2,58},{4,63},{6,51},
  {6,57},{7,53},{6,52},{6,55},{11,45},{14,36},{8,53},{-1,82},{7,55},{-3,78},
  {15,46},{22,31},{-1,84},{25,7},{30,-7},{28,3},{28,4},{32,0},{34,-1},{30,6},
  {30,6},{32,9},{31,19},{26,27},{26,30},{37,20},{28,34},{17,70},{11,28},
  {2,40},{3,44},{0,49},{0,46},{2,44},{2,51},{0,47},{4,39},{2,62},{6,46},
  {0,54},{3,54},{2,58},{4,63},{6,51},{6,57},{7,53},{6,52},{6,55},{11,45},
  {14,36},{8,53},{-1,82},{7,55},{-3,78},{15,46},{22,31},{-1,84},{25,7},
  {30,-7},{28,3},{28,4},{32,0},{34,-1},{30,6},{30,6},{32,9},{31,19},{26,27},
  {26,30},{37,20},{28,34},{17,70},{-4,79},{-7,71},{-5,69},{-9,70},{-8,66},
  {-10,68},{-19,73},{-12,69},{-16,70},{-15,67},{-20,62},{-19,70},{-16,66},
  {-22,65},{-20,63},{-5,85},{-6,81},{-10,77},{-7,81},{-17,80},{-18,73},
  {-4,74},{-10,83},{-9,71},{-9,67},{-1,61},{-8,66},{-14,66},{0,59},{2,59},
  {9,-2},{26,-9},{33,-9},{39,-7},{41,-2},{45,3},{49,9},{45,27},{36,59},
  {21,-13},{33,-14},{39,-7},{46,-2},{51,2},{60,6},{61,17},{55,34},{42,62},
  {-6,66},{-7,35},{-7,42},{-8,45},{-5,48},{-12,56},{-6,60},{-5,62},{-8,66},
  {-8,76},{-4,79},{-7,71},{-5,69},{-9,70},{-8,66},{-10,68},{-19,73},{-12,69},
  {-16,70},{-15,67},{-20,62},{-19,70},{-16,66},{-22,65},{-20,63},{-5,85},
  {-6,81},{-10,77},{-7,81},{-17,80},{-18,73},{-4,74},{-10,83},{-9,71},{-9,67},
  {-1,61},{-8,66},{-14,66},{0,59},{2,59},{9,-2},{26,-9},{33,-9},{39,-7},
  {41,-2},{45,3},{49,9},{45,27},{36,59},{21,-13},{33,-14},{39,-7},{46,-2},
  {51,2},{60,6},{61,17},{55,34},{42,62},{-6,66},{-7,35},{-7,42},{-8,45},
  {-5,48},{-12,56},{-6,60},{-5,62},{-8,66},{-8,76},{-13,106},{-16,106},
  {-10,87},{-21,114},{-18,110},{-14,98},{-22,110},{-21,106},{-18,103},
  {-21,107},{-23,108},{-26,112},{-10,96},{-12,95},{-5,91},{-9,93},{-22,94},
  {-5,86},{9,67},{-4,80},{-10,85},{-1,70},{7,60},{9,58},{5,61},{12,50},
  {15,50},{18,49},{17,54},{10,41},{7,46},{-1,51},{7,49},{8,52},{9,41},{6,47},
  {2,55},{13,41},{10,44},{6,50},{5,53},{13,49},{4,63},{6,64},{-13,106},
  {-16,106},{-10,87},{-21,114},{-18,110},{-14,98},{-22,110},{-21,106},
  {-18,103},{-21,107},{-23,108},{-26,112},{-10,96},{-12,95},{-5,91},{-9,93},
  {-22,94},{-5,86},{9,67},{-4,80},{-10,85},{-1,70},{7,60},{9,58},{5,61},
  {12,50},{15,50},{18,49},{17,54},{10,41},{7,46},{-1,51},{7,49},{8,52},{9,41},
  {6,47},{2,55},{13,41},{10,44},{6,50},{5,53},{13,49},{4,63},{6,64},{14,11},
  {11,14},{9,11},{18,11},{21,9},{23,-2},{32,-15},{32,-15},{34,-21},{39,-23},
  {42,-33},{41,-31},{46,-28},{38,-12},{21,29},{45,-24},{53,-45},{48,-26},
  {65,-43},{43,-19},{39,-10},{30,9},{18,26},{20,27},{0,57},{-14,82},{-5,75},
  {-19,97},{-35,125},{27,0},{28,0},{31,-4},{27,6},{34,8},{30,10},{24,22},
  {33,19},{22,32},{26,31},{21,41},{26,44},{23,47},{16,65},{14,71},{14,11},
  {11,14},{9,11},{18,11},{21,9},{23,-2},{32,-15},{32,-15},{34,-21},{39,-23},
  {42,-33},{41,-31},{46,-28},{38,-12},{21,29},{45,-24},{53,-45},{48,-26},
  {65,-43},{43,-19},{39,-10},{30,9},{18,26},{20,27},{0,57},{-14,82},{-5,75},
  {-19,97},{-35,125},{27,0},{28,0},{31,-4},{27,6},{34,8},{30,10},{24,22},
  {33,19},{22,32},{26,31},{21,41},{26,44},{23,47},{16,65},{14,71},{-6,76},
  {-2,44},{0,45},{0,52},{-3,64},{-2,59},{-4,70},{-4,75},{-8,82},{-17,102},
  {-9,77},{3,24},{0,42},{0,48},{0,55},{-6,59},{-7,71},{-12,83},{-11,87},
  {-30,119},{1,58},{-3,29},{-1,36},{1,38},{2,43},{-6,55},{0,58},{0,64},
  {-3,74},{-10,90},{-6,76},{-2,44},{0,45},{0,52},{-3,64},{-2,59},{-4,70},
  {-4,75},{-8,82},{-17,102},{-9,77},{3,24},{0,42},{0,48},{0,55},{-6,59},
  {-7,71},{-12,83},{-11,87},{-30,119},{1,58},{-3,29},{-1,36},{1,38},{2,43},
  {-6,55},{0,58},{0,64},{-3,74},{-10,90},{-3,74},{-9,92},{-8,87},{-23,126},
  {-3,74},{-9,92},{-8,87},{-23,126},{-3,74},{-9,92},{-8,87},{-23,126}},
{
  {20,-15},{2,54},{3,74},{20,-15},{2,54},{3,74},{-28,127},{-23,104},{-6,53},
  {-1,54},{7,51},{22,25},{34,0},{16,0},{-2,9},{4,41},{-29,118},{2,65},{-6,71},
  {-13,79},{5,52},{9,50},{-3,70},{10,54},{26,34},{19,22},{40,0},{57,2},
  {41,36},{26,69},{-45,127},{-15,101},{-4,76},{-6,71},{-13,79},{5,52},{6,69},
  {-13,90},{0,52},{8,43},{-2,69},{-5,82},{-10,96},{2,59},{2,75},{-3,87},
  {-3,100},{1,56},{-3,74},{-6,85},{0,59},{-3,81},{-7,86},{-5,95},{-1,66},
  {-1,77},{1,70},{-2,86},{-5,72},{0,61},{0,41},{0,63},{0,63},{0,63},{-9,83},
  {4,86},{0,97},{-7,72},{13,41},{3,62},{13,15},{7,51},{2,80},{-39,127},
  {-18,91},{-17,96},{-26,81},{-35,98},{-24,102},{-23,97},{-27,119},{-24,99},
  {-21,110},{-18,102},{-36,127},{0,80},{-5,89},{-7,94},{-4,92},{0,39},{0,65},
  {-15,84},{-35,127},{-2,73},{-12,104},{-9,91},{-31,127},{3,55},{7,56},{7,55},
  {8,61},{-3,53},{0,68},{-7,74},{-9,88},{-13,103},{-13,91},{-9,89},{-14,92},
  {-8,76},{-12,87},{-23,110},{-24,105},{-10,78},{-20,112},{-17,99},{-78,127},
  {-70,127},{-50,127},{-46,127},{-4,66},{-5,78},{-4,71},{-8,72},{2,59},
  {-1,55},{-7,70},{-6,75},{-8,89},{-34,119},{-3,75},{32,20},{30,22},{-44,127},
  {0,54},{-5,61},{0,58},{-1,60},{-3,61},{-8,67},{-25,84},{-14,74},{-5,65},
  {5,52},{2,57},{0,61},{-9,69},{-11,70},{18,55},{-4,71},{0,58},{7,61},{9,41},
  {18,25},{9,32},{5,43},{9,47},{0,44},{0,51},{2,46},{19,38},{-4,66},{15,38},
  {12,42},{9,34},{0,89},{4,45},{10,28},{10,31},{33,-11},{52,-43},{18,15},
  {28,0},{35,-22},{38,-25},{34,0},{39,-18},{32,-12},{102,-94},{0,0},{56,-15},
  {33,-4},{29,10},{37,-5},{51,-29},{39,-9},{52,-34},{69,-58},{67,-63},{44,-5},
  {32,7},{55,-29},{32,1},{0,0},{27,36},{33,-25},{34,-30},{36,-28},{38,-28},
  {38,-27},{34,-18},{35,-16},{34,-14},{32,-8},{37,-6},{35,0},{30,10},{28,18},
  {26,25},{29,41},{0,75},{2,72},{8,77},{14,35},{18,31},{17,35},{21,30},
  {17,45},{20,42},{18,45},{27,26},{16,54},{7,66},{16,56},{11,73},{10,67},
  {-10,116},{-23,112},{-15,71},{-7,61},{0,53},{-5,66},{-11,77},{-9,80},
  {-9,84},{-10,87},{-34,127},{-21,101},{-3,39},{-5,53},{-7,61},{-11,75},
  {-15,77},{-17,91},{-25,107},{-25,111},{-28,122},{-11,76},{-10,44},{-10,52},
  {-10,57},{-9,58},{-16,72},{-7,69},{-4,69},{-5,74},{-9,86},{2,66},{-9,34},
  {1,32},{11,31},{5,52},{-2,55},{-2,67},{0,73},{-8,89},{3,52},{7,4},{10,8},
  {17,8},{16,19},{3,37},{-1,61},{-5,73},{-1,70},{-4,78},{0,0},{-21,126},
  {-23,124},{-20,110},{-26,126},{-25,124},{-17,105},{-27,121},{-27,117},
  {-17,102},{-26,117},{-27,116},{-33,122},{-10,95},{-14,100},{-8,95},
  {-17,111},{-28,114},{-6,89},{-2,80},{-4,82},{-9,85},{-8,81},{-1,72},{5,64},
  {1,67},{9,56},{0,69},{1,69},{7,69},{-7,69},{-6,67},{-16,77},{-2,64},{2,61},
  {-6,67},{-3,64},{2,57},{-3,65},{-3,66},{0,62},{9,51},{-1,66},{-2,71},
  {-2,75},{-1,70},{-9,72},{14,60},{16,37},{0,47},{18,35},{11,37},{12,41},
  {10,41},{2,48},{12,41},{13,41},{0,59},{3,50},{19,40},{3,66},{18,50},{19,-6},
  {18,-6},{14,0},{26,-12},{31,-16},{33,-25},{33,-22},{37,-28},{39,-30},
  {42,-30},{47,-42},{45,-36},{49,-34},{41,-17},{32,9},{69,-71},{63,-63},
  {66,-64},{77,-74},{54,-39},{52,-35},{41,-10},{36,0},{40,-1},{30,14},{28,26},
  {23,37},{12,55},{11,65},{37,-33},{39,-36},{40,-37},{38,-30},{46,-33},
  {42,-30},{40,-24},{49,-29},{38,-12},{40,-10},{38,-3},{46,-5},{31,20},
  {29,30},{25,44},{12,48},{11,49},{26,45},{22,22},{23,22},{27,21},{33,20},
  {26,28},{30,24},{27,34},{18,42},{25,39},{18,50},{12,70},{21,54},{14,71},
  {11,83},{25,32},{21,49},{21,54},{-5,85},{-6,81},{-10,77},{-7,81},{-17,80},
  {-18,73},{-4,74},{-10,83},{-9,71},{-9,67},{-1,61},{-8,66},{-14,66},{0,59},
  {2,59},{17,-10},{32,-13},{42,-9},{49,-5},{53,0},{64,3},{68,10},{66,27},
  {47,57},{-5,71},{0,24},{-1,36},{-2,42},{-2,52},{-9,57},{-6,63},{-4,65},
  {-4,67},{-7,82},{-3,81},{-3,76},{-7,72},{-6,78},{-12,72},{-14,68},{-3,70},
  {-6,76},{-5,66},{-5,62},{0,57},{-4,61},{-9,60},{1,54},{2,58},{17,-10},
  {32,-13},{42,-9},{49,-5},{53,0},{64,3},{68,10},{66,27},{47,57},{0,80},
  {-5,89},{-7,94},{-4,92},{0,39},{0,65},{-15,84},{-35,127},{-2,73},{-12,104},
  {-9,91},{-31,127},{0,80},{-5,89},{-7,94},{-4,92},{0,39},{0,65},{-15,84},
  {-35,127},{-2,73},{-12,104},{-9,91},{-31,127},{-13,103},{-13,91},{-9,89},
  {-14,92},{-8,76},{-12,87},{-23,110},{-24,105},{-10,78},{-20,112},{-17,99},
  {-78,127},{-70,127},{-50,127},{-46,127},{-4,66},{-5,78},{-4,71},{-8,72},
  {2,59},{-1,55},{-7,70},{-6,75},{-8,89},{-34,119},{-3,75},{32,20},{30,22},
  {-44,127},{0,54},{-5,61},{0,58},{-1,60},{-3,61},{-8,67},{-25,84},{-14,74},
  {-5,65},{5,52},{2,57},{0,61},{-9,69},{-11,70},{18,55},{-13,103},{-13,91},
  {-9,89},{-14,92},{-8,76},{-12,87},{-23,110},{-24,105},{-10,78},{-20,112},
  {-17,99},{-78,127},{-70,127},{-50,127},{-46,127},{-4,66},{-5,78},{-4,71},
  {-8,72},{2,59},{-1,55},{-7,70},{-6,75},{-8,89},{-34,119},{-3,75},{32,20},
  {30,22},{-44,127},{0,54},{-5,61},{0,58},{-1,60},{-3,61},{-8,67},{-25,84},
  {-14,74},{-5,65},{5,52},{2,57},{0,61},{-9,69},{-11,70},{18,55},{4,45},
  {10,28},{10,31},{33,-11},{52,-43},{18,15},{28,0},{35,-22},{38,-25},{34,0},
  {39,-18},{32,-12},{102,-94},{0,0},{56,-15},{33,-4},{29,10},{37,-5},{51,-29},
  {39,-9},{52,-34},{69,-58},{67,-63},{44,-5},{32,7},{55,-29},{32,1},{0,0},
  {27,36},{33,-25},{34,-30},{36,-28},{38,-28},{38,-27},{34,-18},{35,-16},
  {34,-14},{32,-8},{37,-6},{35,0},{30,10},{28,18},{26,25},{29,41},{4,45},
  {10,28},{10,31},{33,-11},{52,-43},{18,15},{28,0},{35,-22},{38,-25},{34,0},
  {39,-18},{32,-12},{102,-94},{0,0},{56,-15},{33,-4},{29,10},{37,-5},{51,-29},
  {39,-9},{52,-34},{69,-58},{67,-63},{44,-5},{32,7},{55,-29},{32,1},{0,0},
  {27,36},{33,-25},{34,-30},{36,-28},{38,-28},{38,-27},{34,-18},{35,-16},
  {34,-14},{32,-8},{37,-6},{35,0},{30,10},{28,18},{26,25},{29,41},{-5,85},
  {-6,81},{-10,77},{-7,81},{-17,80},{-18,73},{-4,74},{-10,83},{-9,71},{-9,67},
  {-1,61},{-8,66},{-14,66},{0,59},{2,59},{-3,81},{-3,76},{-7,72},{-6,78},
  {-12,72},{-14,68},{-3,70},{-6,76},{-5,66},{-5,62},{0,57},{-4,61},{-9,60},
  {1,54},{2,58},{17,-10},{32,-13},{42,-9},{49,-5},{53,0},{64,3},{68,10},
  {66,27},{47,57},{17,-10},{32,-13},{42,-9},{49,-5},{53,0},{64,3},{68,10},
  {66,27},{47,57},{-5,71},{0,24},{-1,36},{-2,42},{-2,52},{-9,57},{-6,63},
  {-4,65},{-4,67},{-7,82},{-5,85},{-6,81},{-10,77},{-7,81},{-17,80},{-18,73},
  {-4,74},{-10,83},{-9,71},{-9,67},{-1,61},{-8,66},{-14,66},{0,59},{2,59},
  {-3,81},{-3,76},{-7,72},{-6,78},{-12,72},{-14,68},{-3,70},{-6,76},{-5,66},
  {-5,62},{0,57},{-4,61},{-9,60},{1,54},{2,58},{17,-10},{32,-13},{42,-9},
  {49,-5},{53,0},{64,3},{68,10},{66,27},{47,57},{17,-10},{32,-13},{42,-9},
  {49,-5},{53,0},{64,3},{68,10},{66,27},{47,57},{-5,71},{0,24},{-1,36},
  {-2,42},{-2,52},{-9,57},{-6,63},{-4,65},{-4,67},{-7,82},{-21,126},{-23,124},
  {-20,110},{-26,126},{-25,124},{-17,105},{-27,121},{-27,117},{-17,102},
  {-26,117},{-27,116},{-33,122},{-10,95},{-14,100},{-8,95},{-17,111},
  {-28,114},{-6,89},{-2,80},{-4,82},{-9,85},{-8,81},{-1,72},{5,64},{1,67},
  {9,56},{0,69},{1,69},{7,69},{-7,69},{-6,67},{-16,77},{-2,64},{2,61},{-6,67},
  {-3,64},{2,57},{-3,65},{-3,66},{0,62},{9,51},{-1,66},{-2,71},{-2,75},
  {-21,126},{-23,124},{-20,110},{-26,126},{-25,124},{-17,105},{-27,121},
  {-27,117},{-17,102},{-26,117},{-27,116},{-33,122},{-10,95},{-14,100},
  {-8,95},{-17,111},{-28,114},{-6,89},{-2,80},{-4,82},{-9,85},{-8,81},{-1,72},
  {5,64},{1,67},{9,56},{0,69},{1,69},{7,69},{-7,69},{-6,67},{-16,77},{-2,64},
  {2,61},{-6,67},{-3,64},{2,57},{-3,65},{-3,66},{0,62},{9,51},{-1,66},{-2,71},
  {-2,75},{19,-6},{18,-6},{14,0},{26,-12},{31,-16},{33,-25},{33,-22},{37,-28},
  {39,-30},{42,-30},{47,-42},{45,-36},{49,-34},{41,-17},{32,9},{69,-71},
  {63,-63},{66,-64},{77,-74},{54,-39},{52,-35},{41,-10},{36,0},{40,-1},
  {30,14},{28,26},{23,37},{12,55},{11,65},{37,-33},{39,-36},{40,-37},{38,-30},
  {46,-33},{42,-30},{40,-24},{49,-29},{38,-12},{40,-10},{38,-3},{46,-5},
  {31,20},{29,30},{25,44},{19,-6},{18,-6},{14,0},{26,-12},{31,-16},{33,-25},
  {33,-22},{37,-28},{39,-30},{42,-30},{47,-42},{45,-36},{49,-34},{41,-17},
  {32,9},{69,-71},{63,-63},{66,-64},{77,-74},{54,-39},{52,-35},{41,-10},
  {36,0},{40,-1},{30,14},{28,26},{23,37},{12,55},{11,65},{37,-33},{39,-36},
  {40,-37},{38,-30},{46,-33},{42,-30},{40,-24},{49,-29},{38,-12},{40,-10},
  {38,-3},{46,-5},{31,20},{29,30},{25,44},{-23,112},{-15,71},{-7,61},{0,53},
  {-5,66},{-11,77},{-9,80},{-9,84},{-10,87},{-34,127},{-21,101},{-3,39},
  {-5,53},{-7,61},{-11,75},{-15,77},{-17,91},{-25,107},{-25,111},{-28,122},
  {-11,76},{-10,44},{-10,52},{-10,57},{-9,58},{-16,72},{-7,69},{-4,69},
  {-5,74},{-9,86},{-23,112},{-15,71},{-7,61},{0,53},{-5,66},{-11,77},{-9,80},
  {-9,84},{-10,87},{-34,127},{-21,101},{-3,39},{-5,53},{-7,61},{-11,75},
  {-15,77},{-17,91},{-25,107},{-25,111},{-28,122},{-11,76},{-10,44},{-10,52},
  {-10,57},{-9,58},{-16,72},{-7,69},{-4,69},{-5,74},{-9,86},{-2,73},{-12,104},
  {-9,91},{-31,127},{-2,73},{-12,104},{-9,91},{-31,127},{-2,73},{-12,104},
  {-9,91},{-31,127}},
{
  {20,-15},{2,54},{3,74},{20,-15},{2,54},{3,74},{-28,127},{-23,104},{-6,53},
  {-1,54},{7,51},{29,16},{25,0},{14,0},{-10,51},{-3,62},{-27,99},{26,16},
  {-4,85},{-24,102},{5,57},{6,57},{-17,73},{14,57},{20,40},{20,10},{29,0},
  {54,0},{37,42},{12,97},{-32,127},{-22,117},{-2,74},{-4,85},{-24,102},{5,57},
  {-6,93},{-14,88},{-6,44},{4,55},{-11,89},{-15,103},{-21,116},{19,57},
  {20,58},{4,84},{6,96},{1,63},{-5,85},{-13,106},{5,63},{6,75},{-3,90},
  {-1,101},{3,55},{-4,79},{-2,75},{-12,97},{-7,50},{1,60},{0,41},{0,63},
  {0,63},{0,63},{-9,83},{4,86},{0,97},{-7,72},{13,41},{3,62},{7,34},{-9,88},
  {-20,127},{-36,127},{-17,91},{-14,95},{-25,84},{-25,86},{-12,89},{-17,91},
  {-31,127},{-14,76},{-18,103},{-13,90},{-37,127},{11,80},{5,76},{2,84},
  {5,78},{-6,55},{4,61},{-14,83},{-37,127},{-5,79},{-11,104},{-11,91},
  {-30,127},{0,65},{-2,79},{0,72},{-4,92},{-6,56},{3,68},{-8,71},{-13,98},
  {-4,86},{-12,88},{-5,82},{-3,72},{-4,67},{-8,72},{-16,89},{-9,69},{-1,59},
  {5,66},{4,57},{-4,71},{-2,71},{2,58},{-1,74},{-4,44},{-1,69},{0,62},{-7,51},
  {-4,47},{-6,42},{-3,41},{-6,53},{8,76},{-9,78},{-11,83},{9,52},{0,67},
  {-5,90},{1,67},{-15,72},{-5,75},{-8,80},{-21,83},{-21,64},{-13,31},{-25,64},
  {-29,94},{9,75},{17,63},{-8,74},{-5,35},{-2,27},{13,91},{3,65},{-7,69},
  {8,77},{-10,66},{3,62},{-3,68},{-20,81},{0,30},{1,7},{-3,23},{-21,74},
  {16,66},{-23,124},{17,37},{44,-18},{50,-34},{-22,127},{4,39},{0,42},{7,34},
  {11,29},{8,31},{6,37},{7,42},{3,40},{8,33},{13,43},{13,36},{4,47},{3,55},
  {2,58},{6,60},{8,44},{11,44},{14,42},{7,48},{4,56},{4,52},{13,37},{9,49},
  {19,58},{10,48},{12,45},{0,69},{20,33},{8,63},{35,-18},{33,-25},{28,-3},
  {24,10},{27,0},{34,-14},{52,-44},{39,-24},{19,17},{31,25},{36,29},{24,33},
  {34,15},{30,20},{22,73},{20,34},{19,31},{27,44},{19,16},{15,36},{15,36},
  {21,28},{25,21},{30,20},{31,12},{27,16},{24,42},{0,93},{14,56},{15,57},
  {26,38},{-24,127},{-24,115},{-22,82},{-9,62},{0,53},{0,59},{-14,85},
  {-13,89},{-13,94},{-11,92},{-29,127},{-21,100},{-14,57},{-12,67},{-11,71},
  {-10,77},{-21,85},{-16,88},{-23,104},{-15,98},{-37,127},{-10,82},{-8,48},
  {-8,61},{-8,66},{-7,70},{-14,75},{-10,79},{-9,83},{-12,92},{-18,108},
  {-4,79},{-22,69},{-16,75},{-2,58},{1,58},{-13,78},{-9,83},{-4,81},{-13,99},
  {-13,81},{-6,38},{-13,62},{-6,58},{-2,59},{-16,73},{-10,76},{-13,86},
  {-9,83},{-10,87},{0,0},{-22,127},{-25,127},{-25,120},{-27,127},{-19,114},
  {-23,117},{-25,118},{-26,117},{-24,113},{-28,118},{-31,120},{-37,124},
  {-10,94},{-15,102},{-10,99},{-13,106},{-50,127},{-5,92},{17,57},{-5,86},
  {-13,94},{-12,91},{-2,77},{0,71},{-1,73},{4,64},{-7,81},{5,64},{15,57},
  {1,67},{0,68},{-10,67},{1,68},{0,77},{2,64},{0,68},{-5,78},{7,55},{5,59},
  {2,65},{14,54},{15,44},{5,60},{2,70},{-2,76},{-18,86},{12,70},{5,64},
  {-12,70},{11,55},{5,56},{0,69},{2,65},{-6,74},{5,54},{7,54},{-6,76},
  {-11,82},{-2,77},{-2,77},{25,42},{17,-13},{16,-9},{17,-12},{27,-21},
  {37,-30},{41,-40},{42,-41},{48,-47},{39,-32},{46,-40},{52,-51},{46,-41},
  {52,-39},{43,-19},{32,11},{61,-55},{56,-46},{62,-50},{81,-67},{45,-20},
  {35,-2},{28,15},{34,1},{39,1},{30,17},{20,38},{18,45},{15,54},{0,79},
  {36,-16},{37,-14},{37,-17},{32,1},{34,15},{29,15},{24,25},{34,22},{31,16},
  {35,18},{31,28},{33,41},{36,28},{27,47},{21,62},{18,31},{19,26},{36,24},
  {24,23},{27,16},{24,30},{31,29},{22,41},{22,42},{16,60},{15,52},{14,60},
  {3,78},{-16,123},{21,53},{22,56},{25,61},{21,33},{19,50},{17,61},{-3,78},
  {-8,74},{-9,72},{-10,72},{-18,75},{-12,71},{-11,63},{-5,70},{-17,75},
  {-14,72},{-16,67},{-8,53},{-14,59},{-9,52},{-11,68},{9,-2},{30,-10},{31,-4},
  {33,-1},{33,7},{31,12},{37,23},{31,38},{20,64},{-9,71},{-7,37},{-8,44},
  {-11,49},{-10,56},{-12,59},{-8,63},{-9,67},{-6,68},{-10,79},{-3,78},{-8,74},
  {-9,72},{-10,72},{-18,75},{-12,71},{-11,63},{-5,70},{-17,75},{-14,72},
  {-16,67},{-8,53},{-14,59},{-9,52},{-11,68},{9,-2},{30,-10},{31,-4},{33,-1},
  {33,7},{31,12},{37,23},{31,38},{20,64},{11,80},{5,76},{2,84},{5,78},{-6,55},
  {4,61},{-14,83},{-37,127},{-5,79},{-11,104},{-11,91},{-30,127},{11,80},
  {5,76},{2,84},{5,78},{-6,55},{4,61},{-14,83},{-37,127},{-5,79},{-11,104},
  {-11,91},{-30,127},{-4,86},{-12,88},{-5,82},{-3,72},{-4,67},{-8,72},
  {-16,89},{-9,69},{-1,59},{5,66},{4,57},{-4,71},{-2,71},{2,58},{-1,74},
  {-4,44},{-1,69},{0,62},{-7,51},{-4,47},{-6,42},{-3,41},{-6,53},{8,76},
  {-9,78},{-11,83},{9,52},{0,67},{-5,90},{1,67},{-15,72},{-5,75},{-8,80},
  {-21,83},{-21,64},{-13,31},{-25,64},{-29,94},{9,75},{17,63},{-8,74},{-5,35},
  {-2,27},{13,91},{-4,86},{-12,88},{-5,82},{-3,72},{-4,67},{-8,72},{-16,89},
  {-9,69},{-1,59},{5,66},{4,57},{-4,71},{-2,71},{2,58},{-1,74},{-4,44},
  {-1,69},{0,62},{-7,51},{-4,47},{-6,42},{-3,41},{-6,53},{8,76},{-9,78},
  {-11,83},{9,52},{0,67},{-5,90},{1,67},{-15,72},{-5,75},{-8,80},{-21,83},
  {-21,64},{-13,31},{-25,64},{-29,94},{9,75},{17,63},{-8,74},{-5,35},{-2,27},
  {13,91},{4,39},{0,42},{7,34},{11,29},{8,31},{6,37},{7,42},{3,40},{8,33},
  {13,43},{13,36},{4,47},{3,55},{2,58},{6,60},{8,44},{11,44},{14,42},{7,48},
  {4,56},{4,52},{13,37},{9,49},{19,58},{10,48},{12,45},{0,69},{20,33},{8,63},
  {35,-18},{33,-25},{28,-3},{24,10},{27,0},{34,-14},{52,-44},{39,-24},{19,17},
  {31,25},{36,29},{24,33},{34,15},{30,20},{22,73},{4,39},{0,42},{7,34},
  {11,29},{8,31},{6,37},{7,42},{3,40},{8,33},{13,43},{13,36},{4,47},{3,55},
  {2,58},{6,60},{8,44},{11,44},{14,42},{7,48},{4,56},{4,52},{13,37},{9,49},
  {19,58},{10,48},{12,45},{0,69},{20,33},{8,63},{35,-18},{33,-25},{28,-3},
  {24,10},{27,0},{34,-14},{52,-44},{39,-24},{19,17},{31,25},{36,29},{24,33},
  {34,15},{30,20},{22,73},{-3,78},{-8,74},{-9,72},{-10,72},{-18,75},{-12,71},
  {-11,63},{-5,70},{-17,75},{-14,72},{-16,67},{-8,53},{-14,59},{-9,52},
  {-11,68},{-3,78},{-8,74},{-9,72},{-10,72},{-18,75},{-12,71},{-11,63},
  {-5,70},{-17,75},{-14,72},{-16,67},{-8,53},{-14,59},{-9,52},{-11,68},{9,-2},
  {30,-10},{31,-4},{33,-1},{33,7},{31,12},{37,23},{31,38},{20,64},{9,-2},
  {30,-10},{31,-4},{33,-1},{33,7},{31,12},{37,23},{31,38},{20,64},{-9,71},
  {-7,37},{-8,44},{-11,49},{-10,56},{-12,59},{-8,63},{-9,67},{-6,68},{-10,79},
  {-3,78},{-8,74},{-9,72},{-10,72},{-18,75},{-12,71},{-11,63},{-5,70},
  {-17,75},{-14,72},{-16,67},{-8,53},{-14,59},{-9,52},{-11,68},{-3,78},
  {-8,74},{-9,72},{-10,72},{-18,75},{-12,71},{-11,63},{-5,70},{-17,75},
  {-14,72},{-16,67},{-8,53},{-14,59},{-9,52},{-11,68},{9,-2},{30,-10},{31,-4},
  {33,-1},{33,7},{31,12},{37,23},{31,38},{20,64},{9,-2},{30,-10},{31,-4},
  {33,-1},{33,7},{31,12},{37,23},{31,38},{20,64},{-9,71},{-7,37},{-8,44},
  {-11,49},{-10,56},{-12,59},{-8,63},{-9,67},{-6,68},{-10,79},{-22,127},
  {-25,127},{-25,120},{-27,127},{-19,114},{-23,117},{-25,118},{-26,117},
  {-24,113},{-28,118},{-31,120},{-37,124},{-10,94},{-15,102},{-10,99},
  {-13,106},{-50,127},{-5,92},{17,57},{-5,86},{-13,94},{-12,91},{-2,77},
  {0,71},{-1,73},{4,64},{-7,81},{5,64},{15,57},{1,67},{0,68},{-10,67},{1,68},
  {0,77},{2,64},{0,68},{-5,78},{7,55},{5,59},{2,65},{14,54},{15,44},{5,60},
  {2,70},{-22,127},{-25,127},{-25,120},{-27,127},{-19,114},{-23,117},
  {-25,118},{-26,117},{-24,113},{-28,118},{-31,120},{-37,124},{-10,94},
  {-15,102},{-10,99},{-13,106},{-50,127},{-5,92},{17,57},{-5,86},{-13,94},
  {-12,91},{-2,77},{0,71},{-1,73},{4,64},{-7,81},{5,64},{15,57},{1,67},{0,68},
  {-10,67},{1,68},{0,77},{2,64},{0,68},{-5,78},{7,55},{5,59},{2,65},{14,54},
  {15,44},{5,60},{2,70},{17,-13},{16,-9},{17,-12},{27,-21},{37,-30},{41,-40},
  {42,-41},{48,-47},{39,-32},{46,-40},{52,-51},{46,-41},{52,-39},{43,-19},
  {32,11},{61,-55},{56,-46},{62,-50},{81,-67},{45,-20},{35,-2},{28,15},{34,1},
  {39,1},{30,17},{20,38},{18,45},{15,54},{0,79},{36,-16},{37,-14},{37,-17},
  {32,1},{34,15},{29,15},{24,25},{34,22},{31,16},{35,18},{31,28},{33,41},
  {36,28},{27,47},{21,62},{17,-13},{16,-9},{17,-12},{27,-21},{37,-30},
  {41,-40},{42,-41},{48,-47},{39,-32},{46,-40},{52,-51},{46,-41},{52,-39},
  {43,-19},{32,11},{61,-55},{56,-46},{62,-50},{81,-67},{45,-20},{35,-2},
  {28,15},{34,1},{39,1},{30,17},{20,38},{18,45},{15,54},{0,79},{36,-16},
  {37,-14},{37,-17},{32,1},{34,15},{29,15},{24,25},{34,22},{31,16},{35,18},
  {31,28},{33,41},{36,28},{27,47},{21,62},{-24,115},{-22,82},{-9,62},{0,53},
  {0,59},{-14,85},{-13,89},{-13,94},{-11,92},{-29,127},{-21,100},{-14,57},
  {-12,67},{-11,71},{-10,77},{-21,85},{-16,88},{-23,104},{-15,98},{-37,127},
  {-10,82},{-8,48},{-8,61},{-8,66},{-7,70},{-14,75},{-10,79},{-9,83},{-12,92},
  {-18,108},{-24,115},{-22,82},{-9,62},{0,53},{0,59},{-14,85},{-13,89},
  {-13,94},{-11,92},{-29,127},{-21,100},{-14,57},{-12,67},{-11,71},{-10,77},
  {-21,85},{-16,88},{-23,104},{-15,98},{-37,127},{-10,82},{-8,48},{-8,61},
  {-8,66},{-7,70},{-14,75},{-10,79},{-9,83},{-12,92},{-18,108},{-5,79},
  {-11,104},{-11,91},{-30,127},{-5,79},{-11,104},{-11,91},{-30,127},{-5,79},
  {-11,104},{-11,91},{-30,127}}};

/* rh264 CABAC arithmetic decoding engine (H.264 clause 9.3). I-slice only. */

/* rh264_cabac is defined further up, next to struct rh264_video,
 * which embeds one in its slice-decode workspace. */

/* codIOffset's bit position in val: nine bits above, so val stays
 * below 2^63, and room for up to 47 bits of lookahead beneath */
#define RH264_CB_S 54

/* Renormalisation shift by range: the doublings that take a range of
 * 2..510 back to 256 or more (8 minus its top bit's position), a load
 * in place of a count-leading-zeros with its zero test */
static const uint8_t rh264_cb_norm[512] = {
   0,8,7,7,6,6,6,6,5,5,5,5,5,5,5,5,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
   3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
   2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
   2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


/* Bytes past the end read as zero (end-of-data semantics) and are
 * counted in pad.  Called with bitcnt below 16 at most; tops the
 * lookahead up past 40 bits, a byte at a time. */
static void rh264_cb_refill_tail(rh264_cabac *c)
{
   while (c->bitcnt <= 40)
   {
      if (c->buf < c->end)
         c->val |= (uint64_t)*c->buf++ << (RH264_CB_S - 8 - c->bitcnt);
      else
         c->pad++;
      c->bitcnt += 8;
   }
}

/* Pull bits in for renormalisation.  Away from the end four bytes go in
 * at once (bitcnt is below 16 on every call, so it stays under 48); one
 * refill covers several renormalisations. */
static RH264_INLINE void rh264_cb_refill(rh264_cabac *c)
{
   if (c->end - c->buf >= 4)
   {
      const uint8_t *b = c->buf;
      c->val |= (((uint64_t)b[0] << 24) | ((uint64_t)b[1] << 16)
             | ((uint64_t)b[2] << 8) | b[3]) << (RH264_CB_S - 32 - c->bitcnt);
      c->buf     = b + 4;
      c->bitcnt += 32;
   }
   else
      rh264_cb_refill_tail(c);
}

static void rh264_cabac_init_engine(rh264_cabac *c,
      const uint8_t *buf, const uint8_t *end)
{
   c->buf = buf; c->end = end;
   c->range  = 510;
   c->val    = 0;
   c->bitcnt = -9;   /* the first nine bits are codIOffset */
   c->pad    = 0;
   rh264_cb_refill(c);
}

/* 9.3.1.1 context variable initialisation for an I slice at SliceQPy. */
/* init_idc < 0 selects the I-slice table (9.3.1.1). */
static void rh264_cabac_init_contexts(rh264_cabac *c, int sliceQP, int init_idc)
{
   int i;
   if (sliceQP < 0) sliceQP = 0; else if (sliceQP > 51) sliceQP = 51;
   if (init_idc > 2) init_idc = 2;
   for (i = 0; i < RH264_CABAC_NCTX; i++)
   {
      int m = (init_idc < 0) ? rh264_cabac_init_I[i][0]
                             : rh264_cabac_init_PB[init_idc][i][0];
      int n = (init_idc < 0) ? rh264_cabac_init_I[i][1]
                             : rh264_cabac_init_PB[init_idc][i][1];
      int pre = ((m * sliceQP) >> 4) + n;
      if (pre < 1) pre = 1; else if (pre > 126) pre = 126;
      if (pre <= 63) c->state[i] = (uint8_t)((63 - pre) * 2);
      else           c->state[i] = (uint8_t)((pre - 64) * 2 + 1);
   }
   for (i = 0; i < 128; i++)
   {
      int st = i >> 1, mp = i & 1;
      c->next[0][i] = (uint8_t)(rh264_transIdxMPS[st] * 2 + mp);
      c->next[1][i] = (st == 0)
            ? (uint8_t)(rh264_transIdxLPS[0] * 2 + (1 - mp))
            : (uint8_t)(rh264_transIdxLPS[st] * 2 + mp);
   }
}

static RH264_INLINE int rh264_cabac_decode(rh264_cabac *c, int ctxIdx)
{
   uint32_t s    = c->state[ctxIdx];
   uint32_t rLPS = rh264_cb_lps[((c->range & 0xC0) << 1) + s];
   uint64_t sc;
   uint32_t m;
   int bin;
   uint32_t lps;
   c->range -= rLPS;
   sc  = (uint64_t)c->range << RH264_CB_S;
   /* the LPS/MPS decision is the decoded bin itself, so as a branch it
    * mispredicts at exactly the bin entropy; taken as a select and a
    * mask instead, both outcomes cost the same straight-line code.
    * codIOffset is compared shifted down, a shift that can run while
    * range waits on its table load */
   lps = (uint32_t)(c->val >> RH264_CB_S) >= c->range;
   m   = (uint32_t)0 - lps;
   bin = (int)((s ^ lps) & 1u);
   c->val   -= sc & (uint64_t)(int64_t)(int32_t)m;
   c->range  = lps ? rLPS : c->range;
   c->state[ctxIdx] = c->next[lps][s];
   /* renormalise unconditionally: range is 2..510, so this is 0..7
    * doublings, each moving a bit of lookahead into codIOffset.  As a
    * branch on range < 256 it would follow the LPS outcome and
    * mispredict as often as the bin itself would have */
   {
      int n = rh264_cb_norm[c->range];
      c->range  <<= n;
      c->val    <<= n;
      c->bitcnt  -= n;
   }
   if (c->bitcnt < 16)
      rh264_cb_refill(c);
   return bin;
}

static RH264_INLINE int rh264_cabac_bypass(rh264_cabac *c)
{
   uint64_t sc;
   uint32_t m;
   /* one more bit into codIOffset, then the comparison; bypass bins
    * (signs, suffix bits) are near-uniform - the worst case for a
    * branch, the indifferent case for a mask.  The refill comes first:
    * the bit taken must already be there */
   if (c->bitcnt < 16)
      rh264_cb_refill(c);
   c->val <<= 1;
   c->bitcnt--;
   sc = (uint64_t)c->range << RH264_CB_S;
   m  = (uint32_t)0 - (uint32_t)(c->val >= sc);
   c->val -= sc & (uint64_t)(int64_t)(int32_t)m;
   return (int)(m & 1u);
}

static int rh264_cabac_terminate(rh264_cabac *c)
{
   c->range -= 2;
   if (c->val >= ((uint64_t)c->range << RH264_CB_S)) return 1;
   if (c->range < 256)
   {
      int n = (int)compat_clz_u32(c->range) - 23;
      c->range  <<= n;
      c->val    <<= n;
      c->bitcnt  -= n;
      if (c->bitcnt < 16)
         rh264_cb_refill(c);
   }
   return 0;
}

/* ---- rh264_cabac_mb.h ---- */
/* rh264 CABAC I-slice macroblock parsing (H.264 clause 9.3.3.1).
 * Parses I_4x4 / I_16x16 / I_PCM macroblocks and their residual using CABAC,
 * filling the same coefficient path the CAVLC decoder feeds so reconstruction
 * (intra prediction, inverse transform, dequant, deblock) is shared. */

/* ---- ctxIdxOffset anchors (spec Table 9-11, I-slice) ---- */
enum {
   CTX_MB_TYPE_I      = 3,   /* mb_type prefix for I slice (3..10)          */
   CTX_CBP_LUMA       = 73,  /* coded_block_pattern luma  (73..76)          */
   CTX_CBP_CHROMA     = 77,  /* coded_block_pattern chroma(77..84)          */
   CTX_QP_DELTA       = 60,  /* mb_qp_delta               (60..63)          */
   CTX_CHROMA_PRED    = 64,  /* intra_chroma_pred_mode    (64..67)          */
   CTX_PREV_I4        = 68,  /* prev_intra4x4_pred_mode_flag(68)            */
   CTX_REM_I4         = 69,  /* rem_intra4x4_pred_mode      (69)            */
   CTX_CBF            = 85,  /* coded_block_flag base       (85..104)       */
   CTX_SIG            = 105, /* significant_coeff_flag frame(105..165)      */
   CTX_LAST           = 166, /* last_significant_coeff_flag (166..226)      */
   CTX_ABS            = 227   /* coeff_abs_level_minus1      (227..275)      */,
   RH264_CTX_MB_SKIP_P= 11,  /* mb_skip_flag, P/SP          (11..13)   */
   RH264_CTX_MB_TYPE_P= 14,  /* mb_type prefix, P           (14..17)   */
   RH264_CTX_MB_TYPE_PI=17,  /* mb_type intra suffix, P     (17..20)   */
   RH264_CTX_SUB_TYPE_P=21,  /* sub_mb_type, P              (21..23)   */
   RH264_CTX_MB_SKIP_B= 24,  /* mb_skip_flag, B             (24..26)   */
   RH264_CTX_MB_TYPE_B= 27,  /* mb_type, B: bin0 27..29, then 30,31,32 */
   RH264_CTX_SUB_TYPE_B=36,  /* sub_mb_type, B              (36..39)   */
   RH264_CTX_REF_IDX  = 54,  /* ref_idx_lX                  (54..59)   */
   RH264_CTX_MVD_X    = 40,  /* mvd_l0 horizontal           (40..46)   */
   RH264_CTX_MVD_Y    = 47   /* mvd_l0 vertical             (47..53)   */
};

/* coded_block_flag ctxBlockCat base offsets into the 85.. range (spec
 * Table 9-40 style; ctxIdxBlockCatOffset for coded_block_flag). */
/* cat: 0=lumaDC(I16) 1=lumaAC(I16) 2=luma4x4 3=chromaDC 4=chromaAC */
static const int rh264_cbf_catoff[5]  = {0,4,8,12,16};
static const int rh264_sig_catoff[5]  = {0,15,29,44,47};
static const int rh264_last_catoff[5] = {0,15,29,44,47};
static const int rh264_abs_catoff[5]  = {0,10,20,30,39};
/* The 4:4:4 categories (Table 9-42): Cb and Cr each get the four luma
 * shapes - 6/10 Intra16x16 DC, 7/11 Intra16x16 AC, 8/12 4x4, 9/13 8x8
 * - on their own context ranges (Table 9-34).  Per category: coded_
 * block_flag base, significant / last (frame then field), and
 * coeff_abs_level_minus1. */
typedef struct { short cbf, sig, sigf, last, lastf, abs; } rh264_cat444;
static const rh264_cat444 rh264_cat444_ctx[14] = {
   {  85,105,277,166,338,227},  /* 0  - the 4:2:x luma/chroma table  */
   {  89,120,292,181,353,237},  /* 1     (kept for symmetry; the     */
   {  93,134,306,195,367,247},  /* 2      0..4 rows are not consulted) */
   {  97,149,321,210,382,257},  /* 3  */
   { 101,152,324,213,385,266},  /* 4  */
   {1012,402,436,417,451,426},  /* 5  luma 8x8: cbf only in 4:4:4   */
   { 460,484,776,572,864,952},  /* 6  Cb DC   */
   { 464,499,791,587,879,962},  /* 7  Cb AC   */
   { 468,513,805,601,893,972},  /* 8  Cb 4x4  */
   {1016,660,675,690,699,708},  /* 9  Cb 8x8  */
   { 472,528,820,616,908,982},  /* 10 Cr DC   */
   { 476,543,835,631,923,992},  /* 11 Cr AC   */
   { 480,557,849,645,937,1002}, /* 12 Cr 4x4  */
   {1020,718,733,748,757,766}   /* 13 Cr 8x8  */
};

/* 8x8 blocks (ctxBlockCat 5) sit in their own context blocks: significance
 * at 402 through the position map of Table 9-43 (frame scan), last at 417
 * through its coarser map, levels at 426. The luma coded_block_flag of an
 * 8x8 block is never coded; the coded_block_pattern bit already says it. */
#define RH264_CTX_SIG8  402
#define RH264_CTX_LAST8 417
/* field-coded equivalents (Table 9-11) */
#define RH264_CTX_SIG_F     277
#define RH264_CTX_LAST_F    338
#define RH264_CTX_SIG8_F    436
#define RH264_CTX_LAST8_F   451
#define RH264_CTX_ABS8  426
/* significant_coeff_flag ctxIdxInc for an 8x8 block of a FIELD-coded
 * picture (Table 9-43, field column); the last_significant map is
 * shared with frame coding. */
static const uint8_t rh264_sig8map_fld[64]={
   0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 7, 7, 8, 4, 5,
   6, 9,10,10, 8,11,12,11, 9, 9,10,10, 8,11,12,11,
   9, 9,10,10, 8,11,12,11, 9, 9,10,10, 8,13,13, 9,
   9,10,10, 8,13,13, 9, 9,10,10,14,14,14,14,14,14
};
static const uint8_t rh264_sig8map[64]={
    0, 1, 2, 3, 4, 5, 5, 4, 4, 3, 3, 4, 4, 4, 5, 5,
    4, 4, 4, 4, 3, 3, 6, 7, 7, 7, 8, 9,10, 9, 8, 7,
    7, 6,11,12,13,11, 6, 7, 8, 9,14,10, 9, 8, 6,11,
   12,13,11, 6, 9,14,10, 9,11,12,13,11,14,10,12,14};
static const uint8_t rh264_last8map[64]={
   0,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,
   3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4, 5,5,5,5,6,6,6,6, 7,7,7,7,8,8,8,8};
/* ctxIdxInc maps for the 4x4/chroma-DC significance and last flags, so
 * the per-coefficient derivation is a table load rather than a chain of
 * tests on values fixed for the whole block: identity for the 4x4
 * categories, and min(i,2) / min(i>>1,2) for chroma DC in 4:2:0 and
 * 4:2:2 respectively (9.3.3.1.3). */
static const uint8_t rh264_sig_ident[64]={
 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
 48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63};
static const uint8_t rh264_sig_cdc420[8]={0,1,2,2,2,2,2,2};
static const uint8_t rh264_sig_cdc422[8]={0,0,1,1,2,2,2,2};

/* significant_coeff_flag ctxIdxInc = scan position (0..14) for 4x4 blocks.
  * last_significant likewise. abs_level uses its own numDecodAbsLevel logic. */

/* ===== residual block via CABAC (9.3.2.3, 9.3.3.1.3) ===== */
/* Decode one residual block into coef[] (zig-zag order, length maxNumCoeff).
 * cat = ctxBlockCat (0=I16 lumaDC,1=I16 lumaAC,2=luma4x4,3=chromaDC,4=chromaAC).
 * cbf_ctxinc = coded_block_flag ctxIdxInc from neighbours. Returns coeff count. */
/* ctxIdxInc of the coded_block_flag of an 8x8 block (9.3.3.1.1.9,
 * ctxBlockCat 5 / 9 / 13), coded only in 4:4:4.  The neighbouring
 * 8x8 block counts when its macroblock is available, used the 8x8
 * transform and has that block's coded_block_pattern bit set; an
 * unavailable neighbour reads as 1 for an intra macroblock, an I_PCM
 * neighbour as 1, anything else as 0.  'blk' points at the plane's
 * per-4x4 flags (an 8x8's flag replicated into its four entries). */
static int rh264_cbf8_ctx(int b8, int cbp_luma, int cur_t8,
      const uint8_t *cblk, const rh264_cbf *L, const uint8_t *lblk,
      const rh264_cbf *U, const uint8_t *ublk, int have_left, int have_up,
      int intra)
{
   int bx8 = b8 & 1, by8 = b8 >> 1, a, b;
   if (bx8)
      a = ((cbp_luma >> (b8 - 1)) & 1) && cur_t8 ? cblk[(by8*2)*4 + 0] : 0;
   else if (!have_left || !L->avail)
      a = intra ? 1 : 0;
   else if (L->pcm)
      a = 1;
   else
      a = (L->t8 && ((L->cbpLuma >> (b8 + 1)) & 1)) ? lblk[(by8*2)*4 + 2] : 0;
   if (by8)
      b = ((cbp_luma >> (b8 - 2)) & 1) && cur_t8 ? cblk[0*4 + bx8*2] : 0;
   else if (!have_up || !U->avail)
      b = intra ? 1 : 0;
   else if (U->pcm)
      b = 1;
   else
      b = (U->t8 && ((U->cbpLuma >> (b8 + 2)) & 1)) ? ublk[2*4 + bx8*2] : 0;
   return a + 2*b;
}

/* The residual loop runs the engine on locals: range, offset and bit
 * count held in registers across the bins of a block, where through
 * the struct every context-state byte store (a char store, which may
 * alias anything) forces them back out to memory and in again.  The
 * same arithmetic as rh264_cabac_decode / rh264_cabac_bypass; the
 * refill goes through the struct, once every few bytes. */
#define RH264_CBL_REFILL() do { \
   if (bc < 16) { \
      c->val = val; c->bitcnt = bc; \
      rh264_cb_refill(c); \
      val = c->val; bc = c->bitcnt; \
   } } while (0)
#define RH264_CBL_DECODE(bin, ctxp) do { \
   uint8_t *sp_  = (ctxp); \
   uint32_t s_   = *sp_; \
   uint32_t r_   = rh264_cb_lps[((rng & 0xC0) << 1) + s_]; \
   uint32_t l_; \
   uint64_t sc_; \
   rng -= r_; \
   sc_  = (uint64_t)rng << RH264_CB_S; \
   l_   = (uint32_t)(val >> RH264_CB_S) >= rng; \
   (bin) = (int)((s_ ^ l_) & 1u); \
   val -= sc_ & ((uint64_t)0 - l_); \
   rng  = l_ ? r_ : rng; \
   *sp_ = nx[(l_ << 7) + s_]; \
   { \
      int n_ = rh264_cb_norm[rng]; \
      rng <<= n_; \
      val <<= n_; \
      bc   -= n_; \
   } \
   RH264_CBL_REFILL(); \
   } while (0)
#define RH264_CBL_BYPASS(bin) do { \
   uint64_t sc_; \
   uint32_t m_; \
   RH264_CBL_REFILL(); \
   val <<= 1; \
   bc--; \
   sc_ = (uint64_t)rng << RH264_CB_S; \
   m_  = (uint32_t)0 - (uint32_t)(val >= sc_); \
   val -= sc_ & (uint64_t)(int64_t)(int32_t)m_; \
   (bin) = (int)(m_ & 1u); \
   } while (0)

/* With rs, each level lands at coef[rs[scan position]] rather than at
 * its scan position: the block comes out in raster order, ready for the
 * transform.  With qm as well it lands dequantised, (level * qm[raster
 * position] + 32) >> 6, qm folding the scale and the qP shift of 8.5.12.1
 * (see rh264_qmul4) - the multiply spent on the few coefficients coded
 * rather than on all sixteen. */
static int rh264_cabac_residual_q(rh264_cabac *c, int cat, int cbf_ctxinc,
      int maxNumCoeff, int32_t *coef, const uint8_t *rs, const int32_t *qm)
{
   int i, coded, nsig = 0, sig[64];
   int is8 = (cat == 5 || cat == 9 || cat == 13);
   const rh264_cat444 *cx = (cat >= 5) ? &rh264_cat444_ctx[cat] : NULL;
   uint8_t       *st = c->state;
   const uint8_t *nx = &c->next[0][0];
   uint64_t val;
   uint32_t rng;
   int      bc;
   {
      /* in raster order the block is all 16 or 64 positions, an AC
       * block's DC slot included */
      int nfill = !rs ? maxNumCoeff : (maxNumCoeff <= 16 ? 16 : 64);
      for (i = 0; i < nfill; i++) coef[i] = 0;
   }
   val = c->val;
   rng = c->range;
   bc  = c->bitcnt;
   if (cat == 5 && !c->c444)
      coded = 1;   /* implied by the coded_block_pattern bit (4:2:x) */
   else
   {
      RH264_CBL_DECODE(coded, st + (cx ? cx->cbf
            : CTX_CBF + rh264_cbf_catoff[cat]) + cbf_ctxinc);
      if (!coded)
      {
         c->val    = val;
         c->range  = rng;
         c->bitcnt = bc;
         return 0;
      }
   }
   /* significance map (forward scan).  The context bases and the
    * ctxIdxInc maps depend only on the category and the field/4:2:2
    * flags, all fixed for the block, so they are settled once here
    * instead of per coefficient. */
   {
      const uint8_t *sm, *lm;
      int sbase, lbase, last = maxNumCoeff - 1;
      if (is8)
      {
         sm    = c->field ? rh264_sig8map_fld : rh264_sig8map;
         lm    = rh264_last8map;
         sbase = c->field ? cx->sigf  : cx->sig;
         lbase = c->field ? cx->lastf : cx->last;
      }
      else if (cx)
      {
         sm = lm = rh264_sig_ident;
         sbase = c->field ? cx->sigf  : cx->sig;
         lbase = c->field ? cx->lastf : cx->last;
      }
      else
      {
         if (cat == 3)
            sm = lm = c->c422 ? rh264_sig_cdc422 : rh264_sig_cdc420;
         else
            sm = lm = rh264_sig_ident;
         sbase = (c->field ? RH264_CTX_SIG_F  : CTX_SIG)
               + rh264_sig_catoff[cat];
         lbase = (c->field ? RH264_CTX_LAST_F : CTX_LAST)
               + rh264_last_catoff[cat];
      }
      if (sm == rh264_sig_ident)
      {
         /* the 4x4 shapes: position i is its own ctxIdxInc */
         uint8_t *ss = st + sbase, *ls = st + lbase;
         for (i = 0; i < last; i++)
         {
            int b;
            RH264_CBL_DECODE(b, ss + i);
            if (b)
            {
               sig[nsig++] = i;
               RH264_CBL_DECODE(b, ls + i);
               if (b)
                  break;
            }
         }
      }
      else
      {
         uint8_t *ss = st + sbase, *ls = st + lbase;
         for (i = 0; i < last; i++)
         {
            int b;
            RH264_CBL_DECODE(b, ss + sm[i]);
            if (b)
            {
               sig[nsig++] = i;
               RH264_CBL_DECODE(b, ls + lm[i]);
               if (b)
                  break;
            }
         }
      }
      if (i == last) sig[nsig++] = last;
   }
   /* levels (reverse scan): coeff_abs_level_minus1, its first bin on
    * ctxIdxInc c1 (1 rising to 4 while every level so far is 1, then 0
    * for good after the first level above 1), its later bins on 5 + c2
    * (levels above 1 so far, capped at 4, 3 for chroma DC), then an
    * Exp-Golomb bypass suffix past 14 (9.3.3.1.3). */
   {
      int k;
      int c1 = 1, c2 = 0;
      uint8_t *one = st + (cx ? cx->abs : CTX_ABS + rh264_abs_catoff[cat]);
      uint8_t *gt1 = one + 5;
      int maxc2  = (cat == 3) ? 3 : 4;
      for (k = nsig - 1; k >= 0; k--)
      {
         int lvl = 1, b, sign;
         RH264_CBL_DECODE(b, one + c1);
         if (b)
         {
            uint8_t *actx = gt1 + c2;
            int cnt = 1;
            lvl = 2;
            do {
               RH264_CBL_DECODE(b, actx);
               lvl += b;
            } while (b && ++cnt != 14);
            if (b)
            {
               /* Exp-Golomb, k = 0: a unary prefix, then that many
                * suffix bits */
               int icount = 0, sfx = 0;
               do {
                  RH264_CBL_BYPASS(b);
                  icount += b;
               } while (b && icount != 16);
               sfx = (1 << icount) - 1;
               while (icount--)
               {
                  RH264_CBL_BYPASS(b);
                  sfx += b << icount;
               }
               lvl += sfx;
            }
            c2++; if (c2 > maxc2) c2 = maxc2;
            c1 = 0;
         }
         else if (c1)
         {
            c1++; if (c1 > 4) c1 = 4;
         }
         RH264_CBL_BYPASS(sign);
         if (!rs)
            coef[sig[k]] = sign ? -lvl : lvl;
         else
         {
            int pos = rs[sig[k]];
            if (qm)
            {
               /* the signed level, rounded down as the separate pass
                * did; a conforming stream keeps the product within 2^27
                * (8.5.12.1 bounds the result), and anything else wraps
                * rather than overflowing */
               int32_t  v = sign ? -lvl : lvl;
               uint32_t q = (uint32_t)v * (uint32_t)qm[pos] + 32u;
               coef[pos] = (int32_t)q >> 6;
            }
            else
               coef[pos] = sign ? -lvl : lvl;
         }
      }
   }
   c->val    = val;
   c->range  = rng;
   c->bitcnt = bc;
   return nsig;
}

static int rh264_cabac_residual(rh264_cabac *c, int cat, int cbf_ctxinc,
      int maxNumCoeff, int32_t *coef)
{
   return rh264_cabac_residual_q(c, cat, cbf_ctxinc, maxNumCoeff, coef,
         NULL, NULL);
}

/* ---- rh264_cabac_slice.h ---- */
/* rh264 CABAC I-slice decode (clause 9.3.3.1). Parses I_4x4 / I_16x16
 * macroblocks + residual via CABAC and reconstructs with the shared intra /
 * transform / dequant path. 4:2:0, 4x4 transform (Main profile). */


/* chroma AC 4x4 block idx 0..3 (2x2), comp 0/1 */
/* nblk is 4 for 4:2:0 (a 2x2 arrangement) or 8 for 4:2:2 (2x4), so the
 * neighbour above a top-row block is the corresponding one in the
 * bottom row of the macroblock above. */
static int rh264_cbf_cac_ctx_n(int comp,int idx,rh264_cbf *cur,
      rh264_cbf *L,rh264_cbf *U,int nblk)
{
   int bx=idx&1, by=idx>>1, a, b;
   if (bx>0) a = cur->cAC[comp][idx-1];
   else      a = L->avail ? L->cAC[comp][by*2+1] : 1;
   if (by>0) b = cur->cAC[comp][idx-2];
   else      b = U->avail ? U->cAC[comp][nblk-2+bx] : 1;
   return a + 2*b;
}

/* Decode one I MB. Returns 0 ok, <0 error/unsupported. */
/* mbt supplies the mb_type context indices, which differ between an I slice
 * (ctxIdxOffset 3) and the intra suffix inside a P slice (offset 17, Table
 * 9-39). When pskip_prefix is set the intra/inter prefix bin has already been
 * consumed by the P-slice caller. */
/* 4:4:4: one chroma plane of a CABAC intra macroblock, decoded as
 * luma with the plane's categories (Cb 6..9, Cr 10..13), the plane's
 * own coded_block_flag state (cur->cb / cr), the luma prediction
 * modes, the chroma qP and weight matrices.  Bitstream order: after
 * all of luma, Cb then Cr. */
static int rh264_cabac_intra_plane444(rh264_cabac *cb, rh264_frame *f,
      int comp, int mbx, int mby, int is_i16, int t8,
      int i16mode, int cbp_luma, rh264_cbf *cur, const rh264_cbf *L,
      const rh264_cbf *U, int have_up, int have_left,
      int pu, int pl, int pur, int pul)
{
   /* have_up / have_left: neighbour available to the context
    * derivations; pu / pl / pur / pul: available to predict from
    * (constrained_intra_pred excludes inter neighbours from the
    * latter only) */
   int gw = f->mbw*4, gx0 = mbx*4, gy0 = mby*4;
   int stride = f->cstride;
   uint8_t *P = (comp ? f->V : f->U) + RH264_OFF(f, (size_t)mby*16*stride + mbx*16);
   uint8_t *nz = f->nzC[comp];
   uint8_t *cblk = comp ? cur->cr : cur->cb;
   const uint8_t *lblk = comp ? L->cr : L->cb, *ublk = comp ? U->cr : U->cb;
   uint8_t *cdc = comp ? &cur->crDC : &cur->cbDC;
   int ldc = comp ? L->crDC : L->cbDC, udc = comp ? U->crDC : U->cbDC;
   int cat_dc = comp ? 10 : 6, cat_ac = comp ? 11 : 7;
   int cat_4 = comp ? 12 : 8, cat_8 = comp ? 13 : 9;
   int qpc = rh264_chroma_qp_bd(f, comp ? f->chroma_qp_offset2 : f->chroma_qp_offset);
   const uint8_t *w4 = f->w4[1 + comp];
   const uint8_t *w8 = f->w8[2 + 2*comp];
   int tb = RH264_TB(f);
   int k, bx, by;

   if (is_i16)
   {
      int32_t dc[16], tmp[16], tbres[256];
      int bi;
      rh264_intra16x16(f, P, stride, i16mode, pu, pl);
      {
         int a = have_left ? (L->avail ? ldc : 1) : 1;
         int b = have_up   ? (U->avail ? udc : 1) : 1;
         int ndc = rh264_cabac_residual(cb, cat_dc, a + 2*b, 16, dc);
         *cdc = ndc ? 1 : 0;
      }
      {
         int32_t hin[16], hout[16];
         { const uint8_t *sc = RH264_SCAN4(f);
           for (k = 0; k < 16; k++) hin[sc[k]] = dc[k]; }
         if (tb) { for (k = 0; k < 16; k++) tmp[k] = hin[k]; }
         else
         {
            int per = qpc/6, rem = qpc%6;
            int LS = w4[0]*rh264_dequant4_v[rem][0];
            rh264_ihadamard4x4(hin, hout);
            for (k = 0; k < 16; k++)
            {
               int32_t val = hout[k];
               if (per >= 6) val = (int32_t)(((uint32_t)(val*LS)) << (per-6));
               else          val = (val*LS + (1 << (5-per))) >> (6-per);
               tmp[k] = val;
            }
         }
      }
      for (bi = 0; bi < 16; bi++)
      {
         int32_t ac[16], r[16];
         int raster, nzf = 0;
         bx = rh264_blk_x[bi]; by = rh264_blk_y[bi]; raster = by*4 + bx;
         for (k = 0; k < 16; k++) ac[k] = 0;
         if (cbp_luma)
         {
            int32_t scan[16];
            int a = (bx > 0) ? cblk[raster-1] : (L->avail ? lblk[by*4+3] : 1);
            int b = (by > 0) ? cblk[raster-4] : (U->avail ? ublk[12+bx] : 1);
            nzf = rh264_cabac_residual(cb, cat_ac, a + 2*b, 15, scan);
            { const uint8_t *sc = RH264_SCAN4(f);
              for (k = 0; k < 15; k++) ac[sc[k+1]] = scan[k]; }
            cblk[raster] = nzf ? 1 : 0;
         }
         ac[0] = tmp[raster];
         if (tb) rh264_tb_put4(tbres, 16, bx, by, ac);
         else
         {
            int32_t q[16];
            for (k = 0; k < 16; k++) q[k] = ac[k];
            rh264_dequant4x4(q, qpc, 1, w4); q[0] = ac[0];
            rh264_itransform4x4(q, r);
            rh264_add_residual(f, P + RH264_OFF(f, (by*4)*stride + bx*4), stride, r, 4);
         }
         nz[(gy0+by)*gw + gx0+bx] = (uint8_t)cblk[raster];
      }
      if (tb)
      {
         rh264_tb_dpcm(tbres, 16, 16, rh264_tb_luma_dpcm(i16mode));
         rh264_add_bypass(f, P, stride, tbres, 16, 16, 16);
      }
      return 0;
   }
   if (t8)
   {
      int b8;
      for (b8 = 0; b8 < 4; b8++)
      {
         int bx8 = (b8 & 1), by8 = (b8 >> 1);
         uint8_t *d = P + RH264_OFF(f, (by8*8)*stride + bx8*8);
         /* the luma pass resolved the predicted modes into the grid */
         int mode = f->i4mode[(gy0+by8*2)*gw + gx0+bx8*2];
         int hu = by8 || pu, hl = bx8 || pl;
         int hul = (bx8 && by8) ? 1 : (bx8 ? pu : (by8 ? pl : pul));
         int hur = (b8 == 0) ? pu : (b8 == 1) ? pur : (b8 == 2) ? 1 : 0;
         int coded, cy2, cx2;
         rh264_intra8x8(f, d, stride, mode, hu, hl, hul, hur);
         coded = (cbp_luma >> b8) & 1;
         if (coded)
         {
            int32_t *scan = cb->r8_scan, *coef = cb->r8_coef, *r = cb->r8_r;
            int inc = rh264_cbf8_ctx(b8, cbp_luma, 1, cblk, L, lblk, U, ublk,
                  have_left, have_up, 1);
            if (!rh264_cabac_residual(cb, cat_8, inc, 64, scan))
               coded = 0;
            for (k = 0; k < 64; k++) coef[k] = 0;
            { const uint8_t *sc = RH264_SCAN8(f);
              for (k = 0; k < 64; k++) coef[sc[k]] = scan[k]; }
            if (!coded) {}
            else if (tb)
            {
               rh264_tb_dpcm(coef, 8, 8, rh264_tb_luma_dpcm(mode));
               rh264_add_bypass(f, d, stride, coef, 8, 8, 8);
            }
            else
            {
               rh264_dequant8x8(coef, qpc, w8);
               rh264_itransform8x8(coef, r);
               rh264_add_residual(f, d, stride, r, 8);
            }
         }
         for (cy2 = 0; cy2 < 2; cy2++) for (cx2 = 0; cx2 < 2; cx2++)
         {
            cblk[(by8*2+cy2)*4 + bx8*2+cx2] = coded;
            nz[(gy0+by8*2+cy2)*gw + gx0+bx8*2+cx2] = (uint8_t)coded;
         }
      }
      return 0;
   }
   {
      int bi;
      for (bi = 0; bi < 16; bi++)
      {
         int raster, mode;
         uint8_t *d;
         int hu, hl, hur, hulb, nzf = 0;
         int32_t coef[16], r[16];
         bx = rh264_blk_x[bi]; by = rh264_blk_y[bi]; raster = by*4 + bx;
         mode = f->i4mode[(gy0+by)*gw + gx0+bx];   /* resolved by luma */
         d  = P + RH264_OFF(f, (by*4)*stride + bx*4);
         hu = (by > 0) || pu; hl = (bx > 0) || pl;
         switch (bi)
         {
            case 2: case 6: case 8: case 9: case 10: case 12: case 14:
               hur = 1; break;
            case 3: case 11: case 13: case 15:
               hur = 0; break;
            case 0: case 1: case 4:
               hur = pu; break;
            case 5:
               hur = pur; break;
            default:
               hur = 0; break;
         }
         hulb = (bx && by) ? 1 : (bx ? hu : (by ? hl : pul));
         rh264_intra4x4(f, d, stride, mode, hu, hl, hur, hulb);
         for (k = 0; k < 16; k++) coef[k] = 0;
         if (cbp_luma & (1 << (((by>>1)*2) + (bx>>1))))
         {
            int a = (bx > 0) ? cblk[raster-1] : (L->avail ? lblk[by*4+3] : 1);
            int b = (by > 0) ? cblk[raster-4] : (U->avail ? ublk[12+bx] : 1);
            int32_t scan[16];
            nzf = rh264_cabac_residual(cb, cat_4, a + 2*b, 16, scan);
            { const uint8_t *sc = RH264_SCAN4(f);
              for (k = 0; k < 16; k++) coef[sc[k]] = scan[k]; }
            cblk[raster] = nzf ? 1 : 0;
            if (tb)
            {
               rh264_tb_dpcm(coef, 4, 4, rh264_tb_luma_dpcm(mode));
               rh264_add_bypass(f, d, stride, coef, 4, 4, 4);
            }
            else
            {
               rh264_dequant4x4(coef, qpc, 0, w4);
               rh264_itransform4x4(coef, r);
               rh264_add_residual(f, d, stride, r, 4);
            }
         }
         nz[(gy0+by)*gw + gx0+bx] = (uint8_t)cblk[raster];
      }
   }
   return 0;
}

static int rh264_cabac_decode_mb_ctx(rh264_cabac *cb, const rh264_sps *sps,
      const rh264_pps *pps, rh264_frame *f, int mbx, int mby,
      int *prevQpDeltaNZ, rh264_cbf *cur, rh264_cbf *L, rh264_cbf *U,
      const int *mbt, int forced, int slice_first)
{
   /* neighbouring macroblocks only exist within the current slice (6.4.8) */
   int mbaddr=rh264_mb_addr(mbx,mby,f->mbw,f->mbaff);
   /* A neighbour is available when it belongs to this slice and has
    * already been decoded (6.4.8).  In raster order the second follows
    * from the first, but pair scanning breaks that: the up-right
    * neighbour of a pair's BOTTOM macroblock is the TOP macroblock of
    * the next pair, which comes later. */
   int nb_up = rh264_mb_addr(mbx,mby-1,f->mbw,f->mbaff);
   int nb_lf = rh264_mb_addr(mbx-1,mby,f->mbw,f->mbaff);
   int nb_ur = rh264_mb_addr(mbx+1,mby-1,f->mbw,f->mbaff);
   int nb_ul = rh264_mb_addr(mbx-1,mby-1,f->mbw,f->mbaff);
   int have_up=(mby>0) && nb_up >= slice_first && nb_up < mbaddr;
   int have_left=(mbx>0) && nb_lf >= slice_first && nb_lf < mbaddr;
   int have_ur=(mby>0) && (mbx+1<f->mbw)
      && nb_ur >= slice_first && nb_ur < mbaddr;
   int have_ul=(mby>0) && (mbx>0)
      && nb_ul >= slice_first && nb_ul < mbaddr;
   int gw=f->mbw*4, cgw=f->mbw*2, gx0=mbx*4, gy0=mby*4;
   int is_i16, i16mode=0, cbp_luma=0, cbp_chroma=0, chroma_mode=0, ctxInc, i;
   int t8 = 0;
   int modes[16];
   uint8_t *Y=f->Y + RH264_OFF(f, (mby*16)*f->ystride+mbx*16);
   uint8_t *U8=f->U + RH264_OFF(f, (mby*f->cmbh)*f->cstride+mbx*(f->c444?16:8));
   uint8_t *V8=f->V + RH264_OFF(f, (mby*f->cmbh)*f->cstride+mbx*(f->c444?16:8));
   /* Availability for intra PREDICTION, as distinct from the have_*
    * availability the context derivations use: where the picture
    * forbids predicting from inter samples (constrained_intra_pred,
    * 8.3.1.2), an inter-coded neighbour - 0xff in the mode grid - is
    * not available to predict from, while its coded_block_flags and
    * mb_type still shape the contexts (9.3.3.1.1). */
   int pu = have_up, pl = have_left, pur = have_ur, pul = have_ul;
   memset(cur,0,sizeof(*cur)); cur->avail=1;
   if (f->constrained_intra)
   {
      if (pu  && f->i4mode[(gy0-1)*gw+gx0]   == 0xff) pu  = 0;
      if (pl  && f->i4mode[gy0*gw+gx0-1]     == 0xff) pl  = 0;
      if (pur && f->i4mode[(gy0-1)*gw+gx0+4] == 0xff) pur = 0;
      if (pul && f->i4mode[(gy0-1)*gw+gx0-1] == 0xff) pul = 0;
   }

   /* mb_type bin0 ctxIdxInc: neighbours available & NOT I_4x4 */
   ctxInc = (have_left && L->avail && L->is_i16 ? 1:0)
          + (have_up   && U->avail && U->is_i16 ? 1:0);
   /* forced: the caller already knows the intra size (B slices encode it
    * in the mb_type tree); -1 reads the usual prefix bin. */
   if (forced < 0)
      is_i16 = rh264_cabac_decode(cb, mbt[0] + (mbt[6] ? ctxInc : 0));
   else
      is_i16 = forced;
   if (is_i16) {
      if (rh264_cabac_terminate(cb)) {
         /* I_PCM.  The samples begin at the byte boundary at or after
          * the arithmetic decoder's read position (the nine-bit window
          * covers the encoder's flush and stop bit, 9.3.4.5; then
          * pcm_alignment_zero_bits, 7.3.5).  This engine prefetches:
          * buf is up to seven bytes past that position and bitcnt bits
          * of it are unconsumed, so give back the whole unconsumed
          * bytes - restarting at buf itself skipped up to two sample
          * bytes and desynchronised everything after the first I_PCM
          * (x264 emits I_PCM in its lossless mode on noisy content).
          * The engine restarts behind the samples with its context
          * variables kept (9.3.1.2). */
         int r, c2, k, ch = f->cmbh, cw = f->c444 ? 16 : 8;
         rh264_bits pb;
         /* (an earlier rule stepped one bit further before rounding,
          * which only differed when the prefetch held a whole number of
          * bytes - then it started the samples a byte late) */
         {
            /* the prefetch holds bitcnt bits; the whole bytes among
             * them that came from the stream go back */
            int back = (cb->bitcnt >> 3) - cb->pad;
            if (back > 0)
               cb->buf -= back;
         }
         cb->bitcnt = 0;
         /* 256 + 2 x (ch x cw) samples of bit_depth bits, then byte
          * alignment before the engine restarts (7.3.5, 9.3.1.2) */
         if ((size_t)(cb->end - cb->buf) * 8 < (size_t)(256 + ch*cw*2) * (size_t)f->bd)
            return -1;
         rh264_bits_init(&pb, cb->buf, (size_t)(cb->end - cb->buf));
         for (r = 0; r < 16; r++) for (c2 = 0; c2 < 16; c2++)
            rh264_pcm_put(f, Y, r*f->ystride + c2, rh264_un(&pb, f->bd));
         for (r = 0; r < ch; r++) for (c2 = 0; c2 < cw; c2++)
            rh264_pcm_put(f, U8, r*f->cstride + c2, rh264_un(&pb, f->bd));
         for (r = 0; r < ch; r++) for (c2 = 0; c2 < cw; c2++)
            rh264_pcm_put(f, V8, r*f->cstride + c2, rh264_un(&pb, f->bd));
         cb->buf += (pb.bitpos + 7) >> 3;
         rh264_cabac_init_engine(cb, cb->buf, cb->end);
         /* neighbour state: mb_type counts as not-I_NxN (9.3.3.1.1.3),
          * every coded_block_flag is inferred 1 (9.3.3.1.1.9), the intra
          * prediction modes read as DC (8.3.1.1), and coefficient counts
          * are 16 (9.2.1). QP is unchanged: I_PCM carries no delta. */
         cur->is_i16 = 1;
         cur->pcm = 1;
         cur->cbpLuma = 15; cur->cbpChroma = 2;   /* inferred, 7.4.5 */
         for (k = 0; k < 16; k++) cur->luma[k] = cur->cb[k] = cur->cr[k] = 1;
         cur->cbDC = cur->crDC = 1;
         cur->cDC[0] = cur->cDC[1] = 1;
         /* two columns of chroma blocks, ch/4 rows of them */
         for (k = 0; k < (ch/4)*2; k++) cur->cAC[0][k] = cur->cAC[1][k] = 1;
         for (r = 0; r < 4; r++) for (c2 = 0; c2 < 4; c2++)
         {
            f->nzL[(gy0+r)*gw + gx0+c2] = 16;
            f->i4mode[(gy0+r)*gw + gx0+c2] = 0xff;
            if (f->c444)
            {
               f->nzC[0][(gy0+r)*gw + gx0+c2] = 16;
               f->nzC[1][(gy0+r)*gw + gx0+c2] = 16;
            }
         }
         if (!f->c444)
         for (r = 0; r < f->cmbh/4; r++) for (c2 = 0; c2 < 2; c2++)
         {
            f->nzC[0][(mby*(f->cmbh/4)+r)*cgw + mbx*2+c2] = 16;
            f->nzC[1][(mby*(f->cmbh/4)+r)*cgw + mbx*2+c2] = 16;
         }
         *prevQpDeltaNZ = 0;
         return 0;
      }
      is_i16 = 1;
      cbp_luma   = rh264_cabac_decode(cb, mbt[1]) ? 15 : 0;
      if (rh264_cabac_decode(cb, mbt[2]))
         cbp_chroma = rh264_cabac_decode(cb, mbt[3]) ? 2 : 1;
      i16mode  = rh264_cabac_decode(cb, mbt[4]) << 1;
      i16mode |= rh264_cabac_decode(cb, mbt[5]);
   }
   cur->is_i16 = is_i16;

   if (!is_i16) {
      /* I_NxN: the transform size decides between sixteen 4x4 predictions
       * and four 8x8 ones; the mode syntax is shared (7.3.5.1, 9.3.3.1.1.10
       * for the flag's neighbour-driven context). */
      if (pps->transform_8x8_mode)
      {
         int a = (have_left && L->avail) ? L->t8 : 0;
         int b = (have_up   && U->avail) ? U->t8 : 0;
         t8 = rh264_cabac_decode(cb, 399 + a + b);
      }
      for (i=0;i<(t8?4:16);i++) {
         int prev = rh264_cabac_decode(cb, CTX_PREV_I4);
         int rmode = -1;
         if (!prev) {
            rmode  = rh264_cabac_decode(cb, CTX_REM_I4);
            rmode |= rh264_cabac_decode(cb, CTX_REM_I4)<<1;
            rmode |= rh264_cabac_decode(cb, CTX_REM_I4)<<2;
         }
         modes[i] = rmode;   /* -1 = use predicted */
      }
   }
   cur->t8 = t8;
   f->mbt8[mby * f->mbw + mbx] = (uint8_t)t8;
   /* intra_chroma_pred_mode: TU cMax=3 (decoded for BOTH I_4x4 and
    * I_16x16); absent in 4:4:4, where the chroma planes take the luma
    * modes */
   if (f->c444)
      chroma_mode = 0;
   else
   {
   ctxInc = (have_left && L->avail && L->chroma_nz?1:0)
          + (have_up   && U->avail && U->chroma_nz?1:0);
   if (!rh264_cabac_decode(cb, CTX_CHROMA_PRED + ctxInc))
      chroma_mode = 0;
   else if (!rh264_cabac_decode(cb, CTX_CHROMA_PRED+3))
      chroma_mode = 1;
   else
      chroma_mode = rh264_cabac_decode(cb, CTX_CHROMA_PRED+3) ? 3 : 2;
   }
   cur->chroma_nz = (chroma_mode!=0);
   if (!is_i16) {
      /* CBP: luma 4 bins + chroma */
      { int cbp=0, k;
        for (k=0;k<4;k++) {
           int bx=(k&1)*2, by=(k>>1)*2; /* 8x8 index in 4x4 units */
           int a,b,inc;
           /* condTermN = neighbour 8x8 cbp bit == 0 ? 1 : 0 (avail intra) */
           if (bx>0) a = !((cbp>>(k-1))&1);
           else      a = L->avail ? !((L->cbpLuma>>(k+1))&1) : 0;
           if (by>0) b = !((cbp>>(k-2))&1);
           else      b = U->avail ? !((U->cbpLuma>>(k+2))&1) : 0;
           inc = a + 2*b;
           if (rh264_cabac_decode(cb, CTX_CBP_LUMA+inc)) cbp |= (1<<k);
        }
        cbp_luma = 0;
        { int m; for(m=0;m<4;m++) if(cbp&(1<<m)) cbp_luma |= (1<<m); }
        cur->cbpLuma = cbp_luma;
        /* chroma: bin0 (any) then bin1 (dc+ac); no chroma bins at
         * all in 4:4:4 (7.3.5, ChromaArrayType 3) */
        if (f->c444)
           cbp_chroma = 0;
        else
        { int a,b,inc;
          a = L->avail ? (L->cbpChroma!=0?1:0):0;
          b = U->avail ? (U->cbpChroma!=0?1:0):0;
          inc = a + 2*b;
          if (rh264_cabac_decode(cb, CTX_CBP_CHROMA+inc)) {
             a = L->avail ? (L->cbpChroma==2?1:0):0;
             b = U->avail ? (U->cbpChroma==2?1:0):0;
             inc = a + 2*b;
             cbp_chroma = rh264_cabac_decode(cb, CTX_CBP_CHROMA+4+inc) ? 2 : 1;
          } else cbp_chroma = 0;
        }
      }
      cur->cbpChroma = cbp_chroma;
   } else {
      cur->cbpLuma = cbp_luma; cur->cbpChroma = cbp_chroma;
   }

   /* mb_qp_delta (only if any residual) */
   { int has_res = is_i16 ? 1 : (cbp_luma||cbp_chroma);
     int dqp=0;
     if (has_res) {
        int inc = (*prevQpDeltaNZ)?1:0;
        if (rh264_cabac_decode(cb, CTX_QP_DELTA+inc)) {
           int k=1;
           if (rh264_cabac_decode(cb, CTX_QP_DELTA+2)) {
              k=2;
              /* the legal delta range bounds this prefix; without a
               * cap a corrupt stream spins here until k overflows */
              while (rh264_cabac_decode(cb, CTX_QP_DELTA+3) && k < 88) k++;
           }
           /* codeNum k -> signed: +1,-1,+2,... */
           dqp = (k&1) ? (k+1)/2 : -(k/2);
        }
        *prevQpDeltaNZ = (dqp!=0);
     } else *prevQpDeltaNZ = 0;
     if (rh264_qp_apply_delta(f, dqp)) return -1;
   }

   /* ============ reconstruction ============ */
   if (is_i16) {
      int32_t dc[16], tmp[16]; int bx,by,k;
      int tb = RH264_TB(f); int32_t tbres[256];
      /* I_16x16 blocks contribute DC (mode 2) to neighbour intra4x4 MPM */
      { int yy,xx; for(yy=0;yy<4;yy++) for(xx=0;xx<4;xx++)
           f->i4mode[(gy0+yy)*gw+(gx0+xx)] = 2; }
      rh264_intra16x16(f, Y,f->ystride,i16mode,pu,pl);
      /* luma DC block (cat0) */
      { int inc, a, b, ndc;
        for(k=0;k<16;k++) dc[k]=0;
        a = have_left ? (L->avail ? L->lumaDC : 1) : 1;
        b = have_up   ? (U->avail ? U->lumaDC : 1) : 1;
        inc = a + 2*b;
        ndc = rh264_cabac_residual(cb, 0, inc, 16, dc);
        cur->lumaDC = ndc ? 1 : 0;
      }
      { int qpp=RH264_QPP(f), per=qpp/6,rem=qpp%6;
              int LS=f->w4[0][0]*rh264_dequant4_v[rem][0];
        int32_t hin[16],hout[16];
        { const uint8_t *sc = RH264_SCAN4(f);
          for(k=0;k<16;k++) hin[sc[k]]=dc[k]; }
        if (tb) { for(k=0;k<16;k++) tmp[k]=hin[k]; }   /* dcY = c */
        else {
        rh264_ihadamard4x4(hin,hout);
        for(k=0;k<16;k++){ int32_t val=hout[k];
           if(per>=6) val=(int32_t)(((uint32_t)(val*LS))<<(per-6));
           else val=(val*LS+(1<<(5-per)))>>(6-per);
           tmp[k]=val; }
        }
      }
      { int bi;
      for (bi=0; bi<16; bi++){
         int32_t ac[16],r[16];
         int raster, inc, a, b, nz=0;
         bx=rh264_blk_x[bi]; by=rh264_blk_y[bi]; raster=by*4+bx;
         { uint8_t *d=Y + RH264_OFF(f, (by*4)*f->ystride+bx*4);
         for(k=0;k<16;k++) ac[k]=0;
         if (cbp_luma) {
            int32_t scan[16];
            a = (bx>0)? cur->luma[raster-1] : (L->avail? L->luma[by*4+3]:1);
            b = (by>0)? cur->luma[raster-4] : (U->avail? U->luma[12+bx]:1);
            inc = a + 2*b;
            nz = rh264_cabac_residual(cb, 1, inc, 15, scan); /* AC only, scan order */
            { const uint8_t *sc = RH264_SCAN4(f);
              for(k=0;k<15;k++) ac[sc[k+1]] = scan[k]; }
            cur->luma[raster] = nz?1:0;
         }
         ac[0]=tmp[raster]; /* DC from hadamard, raster order */
         if (tb) rh264_tb_put4(tbres,16,bx,by,ac);
         else {
         { int32_t q[16]; for(k=0;k<16;k++)q[k]=ac[k];
           rh264_dequant4x4(q, RH264_QPP(f),1,f->w4[0]); q[0]=ac[0];
           rh264_itransform4x4(q,r); }
         rh264_add_residual(f, d, f->ystride, r, 4);
         }
         f->nzL[(gy0+by)*gw+(gx0+bx)] = cur->luma[raster];
         }
      }
      if (tb) {
         rh264_tb_dpcm(tbres,16,16,rh264_tb_luma_dpcm(i16mode));
         rh264_add_bypass(f, Y,f->ystride,tbres,16,16,16);
      } }
   } else if (t8) {
      /* I_NxN with 8x8 transform: predict+residual per 8x8 in raster order */
      int b8;
      for (b8 = 0; b8 < 4; b8++) {
         int bx8 = (b8 & 1), by8 = (b8 >> 1);
         uint8_t *d = Y + RH264_OFF(f, (by8*8)*f->ystride + bx8*8);
         int cgx = gx0 + bx8*2, cgy = gy0 + by8*2;
         int predmode, mode = modes[b8];
         int hu = by8 || pu, hl = bx8 || pl;
         int hul = (bx8 && by8) ? 1 : (bx8 ? pu : (by8 ? pl : pul));
         int hur = (b8 == 0) ? pu
                 : (b8 == 1) ? pur
                 : (b8 == 2) ? 1 : 0;
         int k, coded, cy2, cx2;
         { int mA, mB;
           if (hl) mA = f->i4mode[cgy*gw + cgx - 1]; else mA = -1;
           if (hu) mB = f->i4mode[(cgy-1)*gw + cgx]; else mB = -1;
           if (mA == 0xff) mA = 2;
           if (mB == 0xff) mB = 2;
           if (mA < 0 || mB < 0) predmode = 2;
           else predmode = (mA < mB ? mA : mB);
         }
         if (mode < 0) mode = predmode;
         else if (mode >= predmode) mode++;
         for (cy2 = 0; cy2 < 2; cy2++) for (cx2 = 0; cx2 < 2; cx2++)
            f->i4mode[(cgy+cy2)*gw + cgx+cx2] = (uint8_t)mode;
         rh264_intra8x8(f, d, f->ystride, mode, hu, hl, hul, hur);
         coded = (cbp_luma >> b8) & 1;
         if (coded)
         {
            int32_t *scan = cb->r8_scan, *coef = cb->r8_coef;
            int32_t *r = cb->r8_r;
            int inc = f->c444 ? rh264_cbf8_ctx(b8, cbp_luma, 1, cur->luma,
                  L, L->luma, U, U->luma, have_left, have_up, 1) : 0;
            if (!rh264_cabac_residual(cb, 5, inc, 64, scan) && f->c444)
               coded = 0;   /* the 4:4:4 8x8 flag can say "nothing" */
            for (k = 0; k < 64; k++) coef[k] = 0;
            { const uint8_t *sc = RH264_SCAN8(f);
     for (k = 0; k < 64; k++) coef[sc[k]] = scan[k]; }
            if (!coded) {}
            else if (RH264_TB(f))
            {
               rh264_tb_dpcm(coef, 8, 8, rh264_tb_luma_dpcm(mode));
               rh264_add_bypass(f, d, f->ystride, coef, 8, 8, 8);
            }
            else
            {
            rh264_dequant8x8(coef, RH264_QPP(f), f->w8[0]);
            rh264_itransform8x8(coef, r);
            rh264_add_residual(f, d, f->ystride, r, 8);
            }
         }
         /* the four covered 4x4s inherit the 8x8's coded state (both for
          * neighbouring coded_block_flag contexts and for deblocking) */
         for (cy2 = 0; cy2 < 2; cy2++) for (cx2 = 0; cx2 < 2; cx2++)
         {
            cur->luma[(by8*2+cy2)*4 + bx8*2+cx2] = coded;
            f->nzL[(cgy+cy2)*gw + cgx+cx2] = (uint8_t)coded;
         }
      }
   } else {
      /* I_4x4: predict+residual per 4x4 in zig scan order */
      int bi;
      for (bi=0; bi<16; bi++) {
         int bx=rh264_blk_x[bi], by=rh264_blk_y[bi];
         int raster = by*4+bx;
         uint8_t *d=Y + RH264_OFF(f, (by*4)*f->ystride+bx*4);
         int predmode, mode=modes[bi];
         int hu=(by>0)||pu, hl=(bx>0)||pl;
         int hur; int32_t coef[16],r[16]; int k,nz=0;
         /* predicted mode = min(modeA,modeB) */
         { int mA,mB;
           if (bx>0||have_left) mA=f->i4mode[(gy0+by)*gw+(gx0+bx-1)];
           else mA=-1;
           if (by>0||have_up) mB=f->i4mode[(gy0+by-1)*gw+(gx0+bx)];
           else mB=-1;
           /* An inter neighbour (0xff) contributes Intra_4x4 DC to the
            * most probable mode, or counts as unavailable where the
            * picture forbids predicting from inter samples (8.3.1.1).
            * Only reachable from a P slice; in an I slice every
            * neighbour is intra. */
           if (mA==0xff) mA = f->constrained_intra ? -1 : 2;
           if (mB==0xff) mB = f->constrained_intra ? -1 : 2;
           if (mA<0||mB<0) predmode=2; /* DC */
           else predmode=(mA<mB?mA:mB);
         }
         if (mode<0) mode=predmode;
         else if (mode>=predmode) mode++;
         f->i4mode[(gy0+by)*gw+(gx0+bx)]=(uint8_t)mode;
         /* up-right availability (same rule as CAVLC path), keyed by scan bi */
         switch(bi){
            case 2: case 6: case 8: case 9: case 10: case 12: case 14:
               hur=1; break;
            case 3: case 11: case 13: case 15:
               hur=0; break;
            case 0: case 1: case 4:
               hur=pu; break;
            case 5:
               hur=pur; break;
            case 7:
               hur=0; break;
            default: hur=0; break;
         }
         {
            int hulb=(bx&&by)?1:(bx?hu:(by?hl:pul));
            rh264_intra4x4(f, d,f->ystride,mode,hu,hl,hur,hulb);
         }
         for(k=0;k<16;k++)coef[k]=0;
         if (cbp_luma & (1<<(((by>>1)*2)+(bx>>1)))) {
            int a = (bx>0)? cur->luma[raster-1] : (L->avail? L->luma[by*4+3]:1);
            int b = (by>0)? cur->luma[raster-4] : (U->avail? U->luma[12+bx]:1);
            int inc = a + 2*b;
            int32_t scan[16];
            nz = rh264_cabac_residual(cb, 2, inc, 16, scan);
            { const uint8_t *sc = RH264_SCAN4(f);
              for(k=0;k<16;k++)coef[sc[k]]=scan[k]; }
            cur->luma[raster]=nz?1:0;
            if (RH264_TB(f)) {
               rh264_tb_dpcm(coef,4,4,rh264_tb_luma_dpcm(mode));
               rh264_add_bypass(f, d,f->ystride,coef,4,4,4);
            } else {
            rh264_dequant4x4(coef, RH264_QPP(f),0,f->w4[0]);
            rh264_itransform4x4(coef,r);
            rh264_add_residual(f, d, f->ystride, r, 4);
            }
         }
         f->nzL[(gy0+by)*gw+(gx0+bx)]=cur->luma[raster];
      }
   }

   if (f->c444)
   {
      /* 4:4:4: Cb then Cr, each as luma (7.3.5.3 residual_luma) */
      if (rh264_cabac_intra_plane444(cb, f, 0, mbx, mby, is_i16, t8,
               i16mode, cbp_luma, cur, L, U, have_up, have_left,
               pu, pl, pur, pul) < 0
          || rh264_cabac_intra_plane444(cb, f, 1, mbx, mby, is_i16, t8,
               i16mode, cbp_luma, cur, L, U, have_up, have_left,
               pu, pl, pur, pul) < 0)
         return -1;
   }
   else
   {
   /* chroma prediction + residual (4:2:0). Bitstream order (7.3.5.3.1):
    * both chroma DC blocks first, then all chroma AC blocks. */
   rh264_intra_chroma_h(f, U8,f->cstride,chroma_mode,pu,pl,f->cmbh);
   rh264_intra_chroma_h(f, V8,f->cstride,chroma_mode,pu,pl,f->cmbh);
   { int comp, blk, k;
     int32_t (*dcs)[8] = cb->cr_dcs, (*cdc)[8] = cb->cr_cdc;
     int32_t (*cac)[8][16] = cb->cr_cac;
     int nblk = (f->cmbh == 16) ? 8 : 4;
     int qpcc[2];
     qpcc[0]=rh264_chroma_qp_bd(f,f->chroma_qp_offset);
     qpcc[1]=rh264_chroma_qp_bd(f,f->chroma_qp_offset2);
     /* chroma DC for both components */
     for (comp=0; comp<2; comp++) {
        for(k=0;k<nblk;k++) dcs[comp][k]=0;
        if (cbp_chroma) {
           int a = have_left ? (L->avail? L->cDC[comp]:1):1;
           int b = have_up   ? (U->avail? U->cDC[comp]:1):1;
           int inc = a + 2*b;
           int ndc = rh264_cabac_residual(cb, 3, inc, nblk, dcs[comp]);
           cur->cDC[comp] = ndc ? 1 : 0;
        }
        if (RH264_TB(f)) {
          /* bypass: dcC = c, in the 2x2 / 2x4 raster (8.5.11.1) */
          static const uint8_t s422b[8]={0,2,1,4,6,3,5,7};
          if (nblk==8) for(k=0;k<8;k++) cdc[comp][s422b[k]]=dcs[comp][k];
          else         for(k=0;k<4;k++) cdc[comp][k]=dcs[comp][k];
        } else if (nblk==8) {
          /* 4:2:2: the eight coefficients arrive in their own scan
           * order, transform as a 2x4 and quantise at qP+3 */
          static const uint8_t s422[8]={0,2,1,4,6,3,5,7};
          int32_t e[8]; int q=qpcc[comp]+3, per=q/6, rem=q%6;
          int LS=f->w4[1+comp][0]*rh264_dequant4_v[rem][0];
          for(k=0;k<8;k++) e[s422[k]]=dcs[comp][k];
          rh264_chroma_dc_idct422(e);
          for(k=0;k<8;k++){ int32_t v=e[k];
             v = (per >= 6) ? (int32_t)((uint32_t)(v*LS) << (per-6))
                            : (int32_t)((v*LS + (1 << (5-per))) >> (6-per));
             cdc[comp][k]=v; }
        } else { int per=qpcc[comp]/6,rem=qpcc[comp]%6;
          int LS=f->w4[1+comp][0]*rh264_dequant4_v[rem][0];
          int32_t e[4];
          e[0]=dcs[comp][0]+dcs[comp][1]+dcs[comp][2]+dcs[comp][3];
          e[1]=dcs[comp][0]-dcs[comp][1]+dcs[comp][2]-dcs[comp][3];
          e[2]=dcs[comp][0]+dcs[comp][1]-dcs[comp][2]-dcs[comp][3];
          e[3]=dcs[comp][0]-dcs[comp][1]-dcs[comp][2]+dcs[comp][3];
          for(k=0;k<4;k++){ int32_t v=e[k];
             v=(int32_t)(((uint32_t)(v*LS))<<per)>>5; cdc[comp][k]=v; }
        }
     }
     /* chroma AC for both components */
     for (comp=0; comp<2; comp++)
        for (blk=0; blk<nblk; blk++) {
           int bx=blk&1, by=blk>>1;
           for(k=0;k<16;k++) cac[comp][blk][k]=0;
           if (cbp_chroma==2) {
              int inc = rh264_cbf_cac_ctx_n(comp,blk,cur,L,U,nblk);
              int32_t scan[16]; int nz;
              nz = rh264_cabac_residual(cb, 4, inc, 15, scan);
              { const uint8_t *sc = RH264_SCAN4(f);
                for(k=0;k<15;k++) cac[comp][blk][sc[k+1]]=scan[k]; }
              cur->cAC[comp][blk]=nz?1:0;
              (void)bx;(void)by;
           }
        }
     /* reconstruct both components */
     for (comp=0; comp<2; comp++) {
        uint8_t *P = comp? V8:U8;
        int tb = RH264_TB(f); int32_t tbres[8*16];
        for (blk=0; blk<nblk; blk++) {
           int bx=blk&1, by=blk>>1;
           uint8_t *d=P + RH264_OFF(f, (by*4)*f->cstride+bx*4);
           int32_t q[16],r[16];
           for(k=0;k<16;k++) q[k]=cac[comp][blk][k];
           f->nzC[comp][(mby*(f->cmbh/4)+by)*cgw+(mbx*2+bx)]=cur->cAC[comp][blk];
           if (tb) { q[0]=cdc[comp][blk]; rh264_tb_put4(tbres,8,bx,by,q); continue; }
           rh264_dequant4x4(q,qpcc[comp],1,f->w4[1+comp]);
           q[0]=cdc[comp][blk];
           rh264_itransform4x4(q,r);
           rh264_add_residual(f, d, f->cstride, r, 4);
        }
        if (tb) {
           rh264_tb_dpcm(tbres,8,f->cmbh,rh264_tb_chroma_dpcm(chroma_mode));
           rh264_add_bypass(f, P,f->cstride,tbres,8,8,f->cmbh);
        }
     }
   }
   }
   f->mbqp[mby*f->mbw+mbx]=(uint8_t)RH264_QPP(f);
   (void)sps;
   return 0;
}


/* Full CABAC I-slice decode. b is positioned right after the slice header.
 * Returns 0 on success. */

/* I-slice macroblock: ctxIdxOffset 3, bin0 uses the neighbour-derived inc. */
static int rh264_cabac_decode_mb(rh264_cabac *cb, const rh264_sps *sps,
      const rh264_pps *pps, rh264_frame *f, int mbx, int mby,
      int *prevQpDeltaNZ, rh264_cbf *cur, rh264_cbf *L, rh264_cbf *U,
      int slice_first)
{
   static const int mbt_i[7] = { CTX_MB_TYPE_I, CTX_MB_TYPE_I+3,
      CTX_MB_TYPE_I+4, CTX_MB_TYPE_I+5, CTX_MB_TYPE_I+6, CTX_MB_TYPE_I+7, 1 };
   return rh264_cabac_decode_mb_ctx(cb, sps, pps, f, mbx, mby,
         prevQpDeltaNZ, cur, L, U, mbt_i, -1, slice_first);
}

static int rh264_cabac_decode_islice(rh264_bits *b, const rh264_sps *sps,
      const rh264_pps *pps, rh264_slice_hdr *sh, rh264_frame *f, int *end_mb,
      rh264_cabac *cb)
{
   int slice_end;
   rh264_cbf *row, *cur, *L, *U, dummy;
   int mbw=f->mbw, mbh=f->mbh, mbx, mby, prevQpNZ=0, rc=0;
   size_t bytepos;
   /* cabac_alignment_one_bits: consume 1-bits until byte aligned */
   while (b->bitpos & 7) { if (!rh264_u1(b)) break; }
   bytepos = (b->bitpos + 7) >> 3;
   rh264_cabac_init_engine(cb, b->buf + bytepos, b->buf + b->size);
   cb->field = f->field;
   cb->c422 = (f->cmbh == 16 && !f->c444);
   cb->c444 = f->c444;
   rh264_cabac_init_contexts(cb, sh->slice_qp, -1);
   f->qp = sh->slice_qp;
   f->chroma_qp_offset = pps->chroma_qp_index_offset;
   f->constrained_intra = pps->constrained_intra_pred_flag;
   f->chroma_qp_offset2 = pps->chroma_qp_index_offset2;
   f->tb = sps->tb;

   /* per-MB cbf caches: a full row for 'up', plus 'left' tracking */
   /* Scratch lives on the working frame; zero what this slice uses. */
   row = f->scr_row;
   if (!row) return -1;
   memset(row, 0, ((size_t)mbw+2) * sizeof(rh264_cbf));
   memset(&dummy,0,sizeof(dummy));

   slice_end = mbw * mbh;
   {
      /* Address order, which is raster only when the picture is not
       * scanned in macroblock pairs.  With pairs, a column's 'up'
       * context for the top macroblock is the bottom macroblock of the
       * pair above (row), and for the bottom macroblock it is the top
       * one just decoded (toprow); each has its own 'left'. */
      rh264_cbf *toprow = NULL;
      rh264_cbf leftT, leftB;
      int mba, bot;
      if (f->mbaff)
      {
         toprow = f->scr_toprow;
         memset(toprow, 0, ((size_t)mbw+2) * sizeof(rh264_cbf));
      }
      memset(&leftT,0,sizeof(leftT));
      memset(&leftB,0,sizeof(leftB));
      for (mba=sh->first_mb_in_slice; mba<mbw*mbh; mba++) {
         rh264_cbf tmp;
         int la;
         rh264_mb_pos(mba, mbw, f->mbaff, &mbx, &mby);
         bot = f->mbaff && (mba & 1);
         if (mbx==0 && !bot)
         { memset(&leftT,0,sizeof(leftT)); memset(&leftB,0,sizeof(leftB)); }
         /* mb_field_decoding_flag: one per pair, ahead of its top
          * macroblock.  Only frame pairs are handled, so the
          * neighbouring pairs are always frame coded and the context
          * increment is zero. */
         if (f->mbaff && !bot && rh264_cabac_decode(cb, 70))
         { return -1; }
         la = rh264_mb_addr(mbx-1, mby, mbw, f->mbaff);
         cur = &tmp;
         L = (mbx>0 && la >= sh->first_mb_in_slice)
             ? (bot ? &leftB : &leftT) : &dummy;
         U = bot ? &toprow[mbx] : &row[mbx];
         rc = rh264_cabac_decode_mb(cb, sps, pps, f, mbx, mby,
               &prevQpNZ, cur, L, U, sh->first_mb_in_slice);
         if (rc < 0) { return rc; }
         if (bot)      { leftB = tmp; row[mbx] = tmp; }
         else if (f->mbaff) { leftT = tmp; toprow[mbx] = tmp; }
         else          { leftT = tmp; row[mbx] = tmp; }
         if (f->row_done && mbx == f->mbw - 1)
            f->row_done(f->row_user, mby);
         /* end_of_slice_flag, but NOT after the top macroblock of a
          * pair: with pair scanning more data always follows it
          * (7.3.4), and reading a flag there consumes a bit the
          * encoder never wrote. */
         if (mba==mbw*mbh-1) break;
         if (!(f->mbaff && !bot) && rh264_cabac_terminate(cb))
         { slice_end = mba+1; break; }
      }
      }
   if (end_mb) *end_mb = slice_end;
   return 0;
}

/* ---- rh264_cabac_p.h ---- */
/* CABAC P-slice decoding (clause 9.3). Shares the arithmetic engine, residual
 * decoder and reconstruction path with the CABAC I-slice code, and the motion
 * compensation / MV prediction with the CAVLC P-slice code. */

/* mb_skip_flag ctxIdxInc (9.3.3.1.1.1): condTermFlagN is 0 when the neighbour
 * is unavailable or was itself skipped, 1 otherwise. */
static int rh264_cabac_pskip_ctx(int have_left, int have_up,
      const uint8_t *skiprow, int mbx, int leftskip)
{
   int a = (have_left && !leftskip) ? 1 : 0;
   int b = (have_up   && !skiprow[mbx]) ? 1 : 0;
   return a + b;          /* plain sum here, not a + 2*b */
}

/* mvd_lX, UEG3 with signedValFlag 1 and uCoff 9 (9.3.2.3). The prefix is a
 * truncated unary string whose first bin is contexted on the sum of the
 * neighbouring absolute mvd components (9.3.3.1.1.7). */
static int rh264_cabac_mvd(rh264_cabac *cb, int ctxbase, int abssum)
{
   int inc = (abssum < 3) ? 0 : ((abssum > 32) ? 2 : 1);
   int val, k;
   if (!rh264_cabac_decode(cb, ctxbase + inc))
      return 0;
   val = 1;
   for (k = 1; k < 9; k++)
   {
      int c = (k >= 4) ? 6 : (2 + k);   /* binIdx 1,2,3 -> 3,4,5; >=4 -> 6 */
      if (!rh264_cabac_decode(cb, ctxbase + c))
         break;
      val++;
   }
   if (val == 9)
   {
      int kexp = 3;                     /* UEG3 suffix, bypass coded */
      while (rh264_cabac_bypass(cb)) { val += 1 << kexp; kexp++; }
      while (kexp--) val += rh264_cabac_bypass(cb) << kexp;
   }
   return rh264_cabac_bypass(cb) ? -val : val;
}

/* Sum of the absolute mvd component of the partitions left of and above the
 * given 4x4 grid position, for the mvd context increment. */
static int rh264_cabac_mvd_absum(const int16_t *absmvd, int gw,
      int gx, int gy, int comp)
{
   int s = 0;
   if (gx > 0) s += absmvd[((gy)*gw + (gx-1))*2 + comp];
   if (gy > 0) s += absmvd[((gy-1)*gw + (gx))*2 + comp];
   return s;
}

/* ref_idx_lX (9.3.3.1.1.6): unary, first bin contexted on whether the left and
 * above partitions themselves use a reference index above zero. */
/* curref holds the reference indices of the partitions of the macroblock being
 * decoded, in 4x4 raster order, with -2 meaning "not decoded yet". Every
 * ref_idx of a macroblock is coded before any of its mvd values, so a later
 * partition's neighbour can sit in a partition whose index is already known
 * but whose motion vector grid cell has not been written yet. */
static int rh264_cabac_ref_idx(rh264_cabac *cb, const rh264_mv *mvg,
      int gwmax, int gx, int gy, int nrefs,
      const signed char *curref, int mbx, int mby, int list)
{
   int a = 0, b = 0, inc, v;
   int x0 = mbx*4, y0 = mby*4;
   if (nrefs <= 1) return 0;
   /* condTermFlagN (9.3.3.1.1.6): the neighbour contributes when its
    * reference index in this list exceeds zero, except intra blocks and
    * blocks whose motion came from direct derivation, which never do.
    * Inside the current macroblock the per-list scratch is consulted:
    * -2 not yet read (contributes nothing), -3 direct. */
   if (gx > 0)
   {
      int nx = gx-1, ny = gy, r;
      if (nx >= x0 && nx < x0+4 && ny >= y0 && ny < y0+4)
         r = curref[(ny-y0)*4 + (nx-x0)];
      else
      {
         const rh264_mv *m = &mvg[ny*gwmax + nx];
         r = (m->intra || m->dir) ? -3 : (list ? m->ref1 : m->ref);
      }
      a = (r > 0) ? 1 : 0;
   }
   if (gy > 0)
   {
      int nx = gx, ny = gy-1, r;
      if (nx >= x0 && nx < x0+4 && ny >= y0 && ny < y0+4)
         r = curref[(ny-y0)*4 + (nx-x0)];
      else
      {
         const rh264_mv *m = &mvg[ny*gwmax + nx];
         r = (m->intra || m->dir) ? -3 : (list ? m->ref1 : m->ref);
      }
      b = (r > 0) ? 1 : 0;
   }
   inc = a + 2*b;
   if (!rh264_cabac_decode(cb, RH264_CTX_REF_IDX + inc)) return 0;
   if (!rh264_cabac_decode(cb, RH264_CTX_REF_IDX + 4)) return 1;
   /* U binarization: unbounded unary, so keep reading until the
    * terminating zero rather than stopping at the largest legal index. */
   v = 2;
   while (rh264_cabac_decode(cb, RH264_CTX_REF_IDX + 5))
   {
      v++;
      if (v >= RH264_MAX_REFS) break;
   }
   if (v >= nrefs) v = nrefs - 1;   /* never index past the list */
   return v;
}

/* Decode one partition's mvd, predict, store and motion compensate. */
static void rh264_cabac_p_part(rh264_cabac *cb, rh264_frame *f,
      const rh264_frame *ref, const rh264_slice_hdr *sh, rh264_mv *mvg, int16_t *absmvd,
      int gwmax, int ghmax, int mbx, int mby,
      int bx, int by, int bw, int bh, int hint, int refidx,
      const signed char *picid, const int *l0poc)
{
   int gx = mbx*4 + (bx>>2), gy = mby*4 + (by>>2);
   int bw4 = bw>>2, bh4 = bh>>2;
   int pmvx, pmvy, mvx, mvy, mdx, mdy, ix, iy;
   rh264_pred_mv_dir(mvg, gwmax, gwmax, ghmax, gx, gy, bw4, refidx, hint,
         &pmvx, &pmvy);
   mdx = rh264_cabac_mvd(cb, RH264_CTX_MVD_X,
         rh264_cabac_mvd_absum(absmvd, gwmax, gx, gy, 0));
   mdy = rh264_cabac_mvd(cb, RH264_CTX_MVD_Y,
         rh264_cabac_mvd_absum(absmvd, gwmax, gx, gy, 1));
   mvx = pmvx + mdx; mvy = pmvy + mdy;
   rh264_mv_fill_pic(mvg, gwmax, gx, gy, bw4, bh4, mvx, mvy, refidx,
         picid ? picid[refidx] : refidx, l0poc ? l0poc[refidx] : 0);
   for (iy = 0; iy < bh4; iy++) for (ix = 0; ix < bw4; ix++)
   {
      int o = ((gy+iy)*gwmax + (gx+ix))*2;
      absmvd[o]   = (int16_t)(mdx < 0 ? -mdx : mdx);
      absmvd[o+1] = (int16_t)(mdy < 0 ? -mdy : mdy);
   }
   rh264_inter_pred_block(f, ref, mbx, mby, bx, by, bw, bh, mvx, mvy);
   rh264_weight_pred(f, sh, refidx, mbx, mby, bx, by, bw, bh);
}

/* coded_block_flag ctxIdxInc for the blocks of an inter macroblock. An
 * unavailable neighbour gives condTermFlagN 0 here, where an intra macroblock
 * would use 1 (9.3.3.1.1.9). */
/* The same over any plane's per-4x4 flags (4:4:4 Cb / Cr as luma). */
static int rh264_cabac_pcbf_plane_ctx(int raster, const uint8_t *cblk,
      const rh264_cbf *L, const uint8_t *lblk, const rh264_cbf *U,
      const uint8_t *ublk, int have_left, int have_up)
{
   int bx = raster & 3, by = raster >> 2, a, b;
   if (bx > 0) a = cblk[raster-1];
   else        a = (have_left && L->avail) ? lblk[by*4+3] : 0;
   if (by > 0) b = cblk[raster-4];
   else        b = (have_up && U->avail) ? ublk[12+bx] : 0;
   return a + 2*b;
}

static int rh264_cabac_pcbf_luma_ctx(int raster, rh264_cbf *cur,
      rh264_cbf *L, rh264_cbf *U, int have_left, int have_up)
{
   return rh264_cabac_pcbf_plane_ctx(raster, cur->luma, L, L->luma,
         U, U->luma, have_left, have_up);
}

/* 4:4:4: one chroma plane's residual of an inter macroblock, as luma
 * (ctxBlockCat 8/9 for Cb, 12/13 for Cr), with the inter chroma qP
 * and weight matrices.  cbp_luma covers all three planes. */
static void rh264_cabac_p_plane444(rh264_cabac *cb, rh264_frame *f,
      int comp, int mbx, int mby, int cbp_luma, rh264_cbf *cur,
      const rh264_cbf *L, const rh264_cbf *U, int have_left, int have_up,
      int t8)
{
   int gw = f->mbw*4, k, bi;
   int stride = f->cstride;
   uint8_t *P = (comp ? f->V : f->U) + RH264_OFF(f, (size_t)(mby*16)*stride + mbx*16);
   uint8_t *nz = f->nzC[comp];
   uint8_t *cblk = comp ? cur->cr : cur->cb;
   const uint8_t *lblk = comp ? L->cr : L->cb, *ublk = comp ? U->cr : U->cb;
   int cat_4 = comp ? 12 : 8, cat_8 = comp ? 13 : 9;
   int qpc = rh264_chroma_qp_bd(f, comp ? f->chroma_qp_offset2 : f->chroma_qp_offset);
   const uint8_t *w4 = f->w4[4 + comp];
   const uint8_t *w8 = f->w8[3 + 2*comp];
   if (t8)
   {
      int b8;
      for (b8 = 0; b8 < 4; b8++)
      {
         int bx8 = (b8 & 1), by8 = (b8 >> 1);
         int coded = (cbp_luma >> b8) & 1;
         int cy2, cx2;
         if (coded)
         {
            int32_t *scan = cb->r8_scan, *coef = cb->r8_coef, *r = cb->r8_r;
            uint8_t *d = P + RH264_OFF(f, (by8*8)*stride + bx8*8);
            int inc = rh264_cbf8_ctx(b8, cbp_luma, 1, cblk, L, lblk, U, ublk,
                  have_left, have_up, 0);
            if (!rh264_cabac_residual(cb, cat_8, inc, 64, scan))
               coded = 0;
            { const uint8_t *sc = RH264_SCAN8(f);
              for (k = 0; k < 64; k++) coef[sc[k]] = scan[k]; }
            if (!coded) {}
            else if (RH264_TB(f))
               rh264_add_bypass(f, d, stride, coef, 8, 8, 8);
            else
            {
               rh264_dequant8x8(coef, qpc, w8);
               rh264_itransform8x8(coef, r);
               rh264_add_residual(f, d, stride, r, 8);
            }
         }
         for (cy2 = 0; cy2 < 2; cy2++) for (cx2 = 0; cx2 < 2; cx2++)
         {
            cblk[(by8*2+cy2)*4 + bx8*2+cx2] = coded;
            nz[(mby*4+by8*2+cy2)*gw + mbx*4+bx8*2+cx2] = (uint8_t)coded;
         }
      }
      return;
   }
   for (bi = 0; bi < 16; bi++)
   {
      int bx = rh264_blk_x[bi], by = rh264_blk_y[bi], raster = by*4 + bx;
      int32_t coef[16], r[16];
      int nzf = 0;
      if (cbp_luma & (1 << (bi >> 2)))
      {
         int32_t scan[16];
         int inc = rh264_cabac_pcbf_plane_ctx(raster, cblk, L, lblk, U, ublk,
               have_left, have_up);
         nzf = rh264_cabac_residual(cb, cat_4, inc, 16, scan);
         if (nzf)
         {
            { const uint8_t *sc = RH264_SCAN4(f);
              for (k = 0; k < 16; k++) coef[sc[k]] = scan[k]; }
            if (RH264_TB(f))
               rh264_add_bypass(f, P + RH264_OFF(f, (by*4)*stride + bx*4), stride, coef, 4, 4, 4);
            else
            {
               rh264_dequant4x4(coef, qpc, 0, w4);
               rh264_itransform4x4(coef, r);
               rh264_add_residual(f, P + RH264_OFF(f, (by*4)*stride + bx*4), stride, r, 4);
            }
         }
      }
      cblk[raster] = nzf ? 1 : 0;
      nz[(mby*4+by)*gw + mbx*4+bx] = (uint8_t)(nzf ? 1 : 0);
   }
}

static int rh264_cabac_pcbf_cdc_ctx(int comp, rh264_cbf *L, rh264_cbf *U,
      int have_left, int have_up)
{
   int a = (have_left && L->avail) ? L->cDC[comp] : 0;
   int b = (have_up   && U->avail) ? U->cDC[comp] : 0;
   return a + 2*b;
}

static int rh264_cabac_pcbf_cac_ctx(int comp, int idx, rh264_cbf *cur,
      rh264_cbf *L, rh264_cbf *U, int have_left, int have_up, int nblk)
{
   int bx = idx & 1, by = idx >> 1, a, b;
   if (bx > 0) a = cur->cAC[comp][idx-1];
   else        a = (have_left && L->avail) ? L->cAC[comp][by*2+1] : 0;
   if (by > 0) b = cur->cAC[comp][idx-2];
   else        b = (have_up && U->avail) ? U->cAC[comp][nblk-2+bx] : 0;
   return a + 2*b;
}

/* Residual of one inter macroblock: luma 4x4 (ctxBlockCat 2) plus chroma DC
 * and AC (cat 3 and 4), added onto the motion-compensated prediction. */
static void rh264_cabac_p_residual(rh264_cabac *cb, rh264_frame *f,
      int mbx, int mby, int cbp_luma, int cbp_chroma,
      rh264_cbf *cur, rh264_cbf *L, rh264_cbf *U,
      int have_left, int have_up, int t8)
{
   int gw = f->mbw*4, cgw = f->mbw*2, k, bi, comp;
   uint8_t *Y = f->Y + RH264_OFF(f, (mby*16)*f->ystride + mbx*16);
   uint8_t *planes[2];
   int32_t cdc[2][8];
   int nblk = (f->cmbh == 16) ? 8 : 4;
   planes[0] = f->U + RH264_OFF(f, (mby*f->cmbh)*f->cstride + mbx*(f->c444 ? 16 : 8));
   planes[1] = f->V + RH264_OFF(f, (mby*f->cmbh)*f->cstride + mbx*(f->c444 ? 16 : 8));

   if (t8)
   {
      /* luma as four 8x8 blocks; the chroma below is untouched */
      int b8;
      for (b8 = 0; b8 < 4; b8++)
      {
         int bx8 = (b8 & 1), by8 = (b8 >> 1);
         int coded = (cbp_luma >> b8) & 1;
         int cy2, cx2;
         if (coded)
         {
            int32_t *scan = cb->r8_scan, *coef = cb->r8_coef;
            int32_t *r = cb->r8_r;
            uint8_t *d = Y + RH264_OFF(f, (by8*8)*f->ystride + bx8*8);
            int inc = f->c444 ? rh264_cbf8_ctx(b8, cbp_luma, 1, cur->luma,
                  L, L->luma, U, U->luma, have_left, have_up, 0) : 0;
            if (!rh264_cabac_residual(cb, 5, inc, 64, scan) && f->c444)
               coded = 0;
            { const uint8_t *sc = RH264_SCAN8(f);
     for (k = 0; k < 64; k++) coef[sc[k]] = scan[k]; }
            if (!coded) {}
            else if (RH264_TB(f))
               rh264_add_bypass(f, d, f->ystride, coef, 8, 8, 8);
            else
            {
            rh264_dequant8x8(coef, RH264_QPP(f), f->w8[1]);
            rh264_itransform8x8(coef, r);
            rh264_add_residual(f, d, f->ystride, r, 8);
            }
         }
         for (cy2 = 0; cy2 < 2; cy2++) for (cx2 = 0; cx2 < 2; cx2++)
         {
            cur->luma[(by8*2+cy2)*4 + bx8*2+cx2] = coded;
            f->nzL[(mby*4+by8*2+cy2)*gw + mbx*4+bx8*2+cx2] = (uint8_t)coded;
         }
      }
   }
   else
   {
      /* the residual decoder writes the blocks in raster order and, away
       * from transform bypass, dequantised, with one scale per
       * macroblock (8.5.12.1) */
      int32_t qm4[16];
      int tb = RH264_TB(f);
      const uint8_t *rs = RH264_SCAN4(f);
      if (cbp_luma && !tb)
         rh264_qmul4(qm4, RH264_QPP(f), f->w4[3]);
      for (bi = 0; bi < 16; bi++)
      {
         int bx = rh264_blk_x[bi], by = rh264_blk_y[bi], raster = by*4 + bx;
         int32_t coef[16], r[16];
         int nz = 0;
         if (cbp_luma & (1 << (bi >> 2)))
         {
            int inc = rh264_cabac_pcbf_luma_ctx(raster, cur, L, U,
                  have_left, have_up);
            nz = rh264_cabac_residual_q(cb, 2, inc, 16, coef, rs,
                  tb ? NULL : qm4);
            if (nz)
            {
               uint8_t *d = Y + RH264_OFF(f, (by*4)*f->ystride + bx*4);
               if (tb)
                  rh264_add_bypass(f, d, f->ystride, coef, 4, 4, 4);
               else
               {
                  rh264_itransform4x4(coef, r);
                  rh264_add_residual(f, d, f->ystride, r, 4);
               }
            }
         }
         cur->luma[raster] = nz ? 1 : 0;
         f->nzL[(mby*4+by)*gw + mbx*4+bx] = (uint8_t)(nz ? 1 : 0);
      }
   }

   if (f->c444)
   {
      rh264_cabac_p_plane444(cb, f, 0, mbx, mby, cbp_luma, cur, L, U,
            have_left, have_up, t8);
      rh264_cabac_p_plane444(cb, f, 1, mbx, mby, cbp_luma, cur, L, U,
            have_left, have_up, t8);
      return;
   }
   for (comp = 0; comp < 2; comp++) for (k = 0; k < nblk; k++) cdc[comp][k] = 0;
   if (cbp_chroma)
   {
      for (comp = 0; comp < 2; comp++)
      {
         static const uint8_t s422[8]={0,2,1,4,6,3,5,7};
         int32_t scan[8];
         int inc = rh264_cabac_pcbf_cdc_ctx(comp, L, U, have_left, have_up);
         int n = rh264_cabac_residual(cb, 3, inc, nblk, scan);
         if (nblk==8) for (k=0;k<8;k++) cdc[comp][s422[k]] = scan[k];
         else         for (k=0;k<4;k++) cdc[comp][k] = scan[k];
         cur->cDC[comp] = n ? 1 : 0;
         if (RH264_TB(f)) continue;   /* bypass: dcC = c */
         if (nblk==8) rh264_chroma_dc_idct422(cdc[comp]);
         else         rh264_chroma_dc_idct(cdc[comp]);
         { int qpc = rh264_chroma_qp_bd(f,
                 comp?f->chroma_qp_offset2:f->chroma_qp_offset);
           int per, rem, LS;
           if (nblk==8) qpc += 3;
           per = qpc/6; rem = qpc%6;
           LS = f->w4[4+comp][0]*rh264_dequant4_v[rem][0];
           for (k = 0; k < nblk; k++)
           {
              if (nblk==8)
                 cdc[comp][k] = (per >= 6)
                    ? (int32_t)((uint32_t)(cdc[comp][k]*LS) << (per-6))
                    : (int32_t)((cdc[comp][k]*LS + (1 << (5-per))) >> (6-per));
              else
                 cdc[comp][k] = (int32_t)(((uint32_t)(cdc[comp][k]*LS))
                       << per) >> 5;
           } }
      }
   }
   for (comp = 0; comp < 2; comp++)
   {
      uint8_t *p = planes[comp];
      int blk, tb = RH264_TB(f);
      int32_t qm4[16];
      /* AC levels come out in raster order (the scan from its second
       * entry) and, away from transform bypass, dequantised; the DC,
       * already scaled, goes in after */
      const uint8_t *rs = RH264_SCAN4(f) + 1;
      if (cbp_chroma == 2 && !tb)
         rh264_qmul4(qm4, rh264_chroma_qp_bd(f,
               comp?f->chroma_qp_offset2:f->chroma_qp_offset),
               f->w4[4+comp]);
      for (blk = 0; blk < nblk; blk++)
      {
         int bx = blk & 1, by = blk >> 1;
         int32_t ac[16], r[16];
         int nz = 0;
         uint8_t *d = p + RH264_OFF(f, by*4*f->cstride + bx*4);
         if (cbp_chroma == 2)
         {
            int inc = rh264_cabac_pcbf_cac_ctx(comp, blk, cur, L, U,
                  have_left, have_up, nblk);
            nz = rh264_cabac_residual_q(cb, 4, inc, 15, ac, rs,
                  tb ? NULL : qm4);
         }
         cur->cAC[comp][blk] = nz ? 1 : 0;
         f->nzC[comp][(mby*(f->cmbh/4)+by)*cgw + mbx*2+bx] = (uint8_t)(nz ? 1 : 0);
         if (!nz)
         {
            /* no AC: nothing at all to add, or (transform aside) the
             * DC alone, which the transform spreads unchanged over all
             * sixteen samples */
            if (!cdc[comp][blk])
               continue;
            for (k = 0; k < 16; k++) ac[k] = 0;
            ac[0] = cdc[comp][blk];
            if (tb)
               rh264_add_bypass(f, d, f->cstride, ac, 4, 4, 4);
            else
            {
               for (k = 0; k < 16; k++) r[k] = ac[0];
               rh264_add_residual(f, d, f->cstride, r, 4);
            }
            continue;
         }
         ac[0] = cdc[comp][blk];
         if (tb)
         {
            rh264_add_bypass(f, d, f->cstride, ac, 4, 4, 4);
            continue;
         }
         rh264_itransform4x4(ac, r);
         rh264_add_residual(f, d, f->cstride, r, 4);
      }
   }
}

static int rh264_cabac_decode_pslice(rh264_bits *b, const rh264_sps *sps,
      const rh264_pps *pps, rh264_slice_hdr *sh, rh264_frame *f,
      const rh264_frame *const *l0, int nrefs, const signed char *picid,
      const int *l0poc, rh264_mv *mvg, int *end_mb,
      rh264_cabac *cb)
{
   int slice_end;
   rh264_cbf *row, dummy;
   uint8_t *skiprow;
   int16_t *absmvd;
   int mbw = f->mbw, mbh = f->mbh, mbx, mby, prevQpNZ = 0;
   int gw = mbw*4, cgw = mbw*2;
   int gwmax = mbw*4, ghmax = mbh*4;
   size_t bytepos;

   while (b->bitpos & 7) { if (!rh264_u1(b)) break; }
   bytepos = (b->bitpos + 7) >> 3;
   rh264_cabac_init_engine(cb, b->buf + bytepos, b->buf + b->size);
   cb->field = f->field;
   cb->c422 = (f->cmbh == 16 && !f->c444);
   cb->c444 = f->c444;
   rh264_cabac_init_contexts(cb, sh->slice_qp, sh->cabac_init_idc);
   f->qp = sh->slice_qp;
   f->chroma_qp_offset = pps->chroma_qp_index_offset;
   f->constrained_intra = pps->constrained_intra_pred_flag;
   f->chroma_qp_offset2 = pps->chroma_qp_index_offset2;
   f->tb = sps->tb;
   if (nrefs < 1) return -1;

   /* the coefficient/mode context describes the picture being decoded; a
    * continuation slice must keep what earlier slices of it produced */
   if (sh->first_mb_in_slice == 0)
   {
      memset(f->nzL, 0, (size_t)gw * mbh * 4);
      memset(f->nzC[0], 0, (size_t)cgw * mbh * (f->cmbh/4));
      memset(f->nzC[1], 0, (size_t)cgw * mbh * (f->cmbh/4));
      memset(f->i4mode, 0xff, (size_t)gw * mbh * 4);
      memset(f->mbt8, 0, (size_t)mbw * mbh);
   }

   /* Scratch lives on the working frame; zero what this slice uses. */
   row     = f->scr_row;
   skiprow = f->scr_skiprow;
   absmvd  = f->scr_am0;
   if (!row) return -1;
   memset(row,     0, ((size_t)mbw+2) * sizeof(rh264_cbf));
   memset(skiprow, 0, (size_t)mbw+2);
   memset(absmvd,  0, (size_t)gwmax*ghmax*2 * sizeof(int16_t));
   memset(&dummy, 0, sizeof(dummy));

   slice_end = mbw * mbh;
   {
      /* address order, with the pair contexts the intra reader uses */
      rh264_cbf leftT, leftB;
      rh264_cbf *toprow = NULL;
      uint8_t *topskip = NULL;
      int leftskipT = 0, leftskipB = 0, mba, bot, prev_skipped = 0;
      if (f->mbaff)
      {
         toprow  = f->scr_toprow;
         topskip = f->scr_topskip;
         memset(toprow,  0, ((size_t)mbw+2) * sizeof(rh264_cbf));
         memset(topskip, 0, (size_t)mbw+2);
      }
      memset(&leftT, 0, sizeof(leftT));
      memset(&leftB, 0, sizeof(leftB));
      for (mba = sh->first_mb_in_slice; mba < mbw*mbh; mba++)
      {
         int have_up, have_left, skip, inc, cx, cy, la, ua;
         rh264_cbf tmp, *L, *U;
         uint8_t *upskip; int leftskip;
         rh264_mb_pos(mba, mbw, f->mbaff, &mbx, &mby);
         bot = f->mbaff && (mba & 1);
         if (mbx == 0 && !bot)
         {
            memset(&leftT, 0, sizeof(leftT));
            memset(&leftB, 0, sizeof(leftB));
            leftskipT = 0; leftskipB = 0;
         }
         la = rh264_mb_addr(mbx-1, mby, mbw, f->mbaff);
         ua = rh264_mb_addr(mbx, mby-1, mbw, f->mbaff);
         /* neighbours only exist within the current slice (6.4.8) and
          * must already be decoded, which pair scanning can break */
         have_up   = (mby > 0) && ua >= sh->first_mb_in_slice && ua < mba;
         have_left = (mbx > 0) && la >= sh->first_mb_in_slice && la < mba;
         memset(&tmp, 0, sizeof(tmp));
         L = have_left ? (bot ? &leftB : &leftT) : &dummy;
         U = have_up   ? (bot ? &toprow[mbx] : &row[mbx]) : &dummy;
         upskip   = bot ? topskip : skiprow;
         leftskip = bot ? leftskipB : leftskipT;
         inc  = rh264_cabac_pskip_ctx(have_left, have_up, upskip, mbx, leftskip);
         skip = rh264_cabac_decode(cb, RH264_CTX_MB_SKIP_P + inc);
         /* mb_field_decoding_flag follows mb_skip_flag and is only sent
          * for a macroblock that carries data: ahead of a pair's top
          * macroblock, or its bottom one when the top was skipped
          * (7.3.4).  Only frame pairs are handled. */
         if (f->mbaff && !skip && (!bot || prev_skipped)
               && rh264_cabac_decode(cb, 70))
         { return -1; }
         prev_skipped = skip;

         if (skip)
         {
            int gx = mbx*4, gy = mby*4, mvx, mvy;
            const rh264_mv *A = rh264_mv_at(mvg, gwmax, gx-1, gy, gwmax, ghmax);
            const rh264_mv *B = rh264_mv_at(mvg, gwmax, gx, gy-1, gwmax, ghmax);
            if (!A || A->ref == -2 || !B || B->ref == -2
                  || (A->ref == 0 && A->mvx == 0 && A->mvy == 0)
                  || (B->ref == 0 && B->mvx == 0 && B->mvy == 0))
            { mvx = 0; mvy = 0; }
            else
               rh264_pred_mv(mvg, gwmax, gwmax, ghmax, gx, gy, 4, 0, &mvx, &mvy);
            rh264_mv_fill_pic(mvg, gwmax, gx, gy, 4, 4, mvx, mvy, 0,
                  picid ? picid[0] : 0, l0poc ? l0poc[0] : 0);
            rh264_inter_pred_block(f, l0[0], mbx, mby, 0, 0, 16, 16, mvx, mvy);
            rh264_weight_pred(f, sh, 0, mbx, mby, 0, 0, 16, 16);
            rh264_inter_clear_i4mode(f, mbx, mby);
            for (cy = 0; cy < 4; cy++)
            {
               memset(f->nzL + (size_t)(mby*4+cy)*gw + mbx*4, 0, 4);
               memset(absmvd + ((size_t)(mby*4+cy)*gwmax + mbx*4)*2, 0,
                     8 * sizeof(*absmvd));
            }
            rh264_nzc_clear(f, mbx, mby);
            tmp.avail = 1;
            prevQpNZ = 0;
            f->mbqp[mby*mbw+mbx] = (uint8_t)RH264_QPP(f);
         }
         else
         {
            int mb_type, cbp_luma = 0, cbp_chroma = 0;
            int t8 = 0, t8ok = 1;
            signed char curref[16];
            memset(curref, -2, sizeof(curref));
            tmp.avail = 1;
            if (rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_P + 0))
            {
               /* Intra macroblock inside a P slice. The prefix bin above has
                * already selected intra; the suffix is the I-slice mb_type
                * binarization read with ctxIdxOffset 17 (Table 9-39). */
               static const int mbt_p[7] = { RH264_CTX_MB_TYPE_PI,
                  RH264_CTX_MB_TYPE_PI+1, RH264_CTX_MB_TYPE_PI+2,
                  RH264_CTX_MB_TYPE_PI+2, RH264_CTX_MB_TYPE_PI+3,
                  RH264_CTX_MB_TYPE_PI+3, 0 };
               int r2 = rh264_cabac_decode_mb_ctx(cb, sps, pps, f, mbx, mby,
                     &prevQpNZ, &tmp, L, U, mbt_p, -1, sh->first_mb_in_slice);
               if (r2 < 0)
               { return r2; }
               /* mark the macroblock intra in the MV grid and clear its mvd */
               for (cy = 0; cy < 4; cy++) for (cx = 0; cx < 4; cx++)
               {
                  int o = ((mby*4+cy)*gwmax + mbx*4+cx);
                  mvg[o].mvx = 0; mvg[o].mvy = 0; mvg[o].ref = -1;
                  mvg[o].pic = -1; mvg[o].refpoc0 = 0;
                  mvg[o].mvx1 = 0; mvg[o].mvy1 = 0; mvg[o].ref1 = -1;
                  mvg[o].pic1 = -1; mvg[o].refpoc1 = 0; mvg[o].intra = 1;
                  absmvd[o*2] = 0; absmvd[o*2+1] = 0;
               }
               f->mbqp[mby*mbw+mbx] = (uint8_t)RH264_QPP(f);
               if (bot) { leftB = tmp; row[mbx] = tmp;
                          leftskipB = 0; skiprow[mbx] = 0; }
               else if (f->mbaff) { leftT = tmp; toprow[mbx] = tmp;
                          leftskipT = 0; topskip[mbx] = 0; }
               else { leftT = tmp; row[mbx] = tmp;
                          leftskipT = 0; skiprow[mbx] = 0; }
               if (f->row_done && mbx == mbw - 1)
                  f->row_done(f->row_user, mby);
               if (mba == mbw*mbh-1) break;
               if (!(f->mbaff && !bot) && rh264_cabac_terminate(cb))
               { slice_end = mba+1; break; }
               continue;
            }
            {
               int b1 = rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_P + 1);
               int b2 = rh264_cabac_decode(cb,
                     RH264_CTX_MB_TYPE_P + (b1 ? 3 : 2));
               if (!b1) mb_type = b2 ? 3 : 0;     /* P_8x8 : P_L0_16x16 */
               else     mb_type = b2 ? 1 : 2;     /* P_16x8 : P_8x16    */
            }
            rh264_inter_clear_i4mode(f, mbx, mby);

            if (mb_type == 0)
            {
               int r0 = rh264_cabac_ref_idx(cb, mvg, gwmax, mbx*4, mby*4,
                     nrefs, curref, mbx, mby, 0);
               { int _y,_x; for(_y=0;_y<4;_y++) for(_x=0;_x<4;_x++) curref[_y*4+_x]=(signed char)r0; }
               rh264_cabac_p_part(cb, f, l0[r0], sh, mvg, absmvd, gwmax, ghmax,
                     mbx, mby, 0, 0, 16, 16, RH264_MVP_MEDIAN, r0, picid, l0poc);
            }
            else if (mb_type == 1)
            {
               int ra = rh264_cabac_ref_idx(cb, mvg, gwmax, mbx*4, mby*4,
                     nrefs, curref, mbx, mby, 0);
               int rb;
               { int _y,_x; for(_y=0;_y<2;_y++) for(_x=0;_x<4;_x++) curref[_y*4+_x]=(signed char)ra; }
               rb = rh264_cabac_ref_idx(cb, mvg, gwmax, mbx*4, mby*4+2,
                     nrefs, curref, mbx, mby, 0);
               { int _y,_x; for(_y=2;_y<4;_y++) for(_x=0;_x<4;_x++) curref[_y*4+_x]=(signed char)rb; }
               rh264_cabac_p_part(cb, f, l0[ra], sh, mvg, absmvd, gwmax, ghmax,
                     mbx, mby, 0, 0, 16, 8, RH264_MVP_B, ra, picid, l0poc);
               rh264_cabac_p_part(cb, f, l0[rb], sh, mvg, absmvd, gwmax, ghmax,
                     mbx, mby, 0, 8, 16, 8, RH264_MVP_A, rb, picid, l0poc);
            }
            else if (mb_type == 2)
            {
               int ra = rh264_cabac_ref_idx(cb, mvg, gwmax, mbx*4, mby*4,
                     nrefs, curref, mbx, mby, 0);
               int rb;
               { int _y,_x; for(_y=0;_y<4;_y++) for(_x=0;_x<2;_x++) curref[_y*4+_x]=(signed char)ra; }
               rb = rh264_cabac_ref_idx(cb, mvg, gwmax, mbx*4+2, mby*4,
                     nrefs, curref, mbx, mby, 0);
               { int _y,_x; for(_y=0;_y<4;_y++) for(_x=2;_x<4;_x++) curref[_y*4+_x]=(signed char)rb; }
               rh264_cabac_p_part(cb, f, l0[ra], sh, mvg, absmvd, gwmax, ghmax,
                     mbx, mby, 0, 0, 8, 16, RH264_MVP_A, ra, picid, l0poc);
               rh264_cabac_p_part(cb, f, l0[rb], sh, mvg, absmvd, gwmax, ghmax,
                     mbx, mby, 8, 0, 8, 16, RH264_MVP_C, rb, picid, l0poc);
            }
            else
            {
               int sub[4], rf[4], p;
               for (p = 0; p < 4; p++)
               {
                  if (rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_P + 0))
                     sub[p] = 0;
                  else if (!rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_P + 1))
                     sub[p] = 1;
                  else
                     sub[p] = rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_P + 2)
                            ? 2 : 3;
               }
               /* the 8x8 transform needs every partition at least 8x8 */
               t8ok = (sub[0] == 0 && sub[1] == 0 && sub[2] == 0
                     && sub[3] == 0);
               for (p = 0; p < 4; p++)
               {
                  int qx = (p&1)*2, qy = (p>>1)*2, _y, _x;
                  rf[p] = rh264_cabac_ref_idx(cb, mvg, gwmax,
                        mbx*4 + qx, mby*4 + qy, nrefs, curref, mbx, mby, 0);
                  for (_y = qy; _y < qy+2; _y++)
                     for (_x = qx; _x < qx+2; _x++)
                        curref[_y*4+_x] = (signed char)rf[p];
               }
               for (p = 0; p < 4; p++)
               {
                  int px = (p&1)*8, py = (p>>1)*8, st = sub[p], rr = rf[p];
                  const rh264_frame *rp = l0[rr];
                  if (st == 0)
                     rh264_cabac_p_part(cb, f, rp, sh, mvg, absmvd, gwmax, ghmax,
                           mbx, mby, px, py, 8, 8, RH264_MVP_MEDIAN, rr, picid, l0poc);
                  else if (st == 1)
                  {
                     rh264_cabac_p_part(cb, f, rp, sh, mvg, absmvd, gwmax, ghmax,
                           mbx, mby, px, py,   8, 4, RH264_MVP_MEDIAN, rr, picid, l0poc);
                     rh264_cabac_p_part(cb, f, rp, sh, mvg, absmvd, gwmax, ghmax,
                           mbx, mby, px, py+4, 8, 4, RH264_MVP_MEDIAN, rr, picid, l0poc);
                  }
                  else if (st == 2)
                  {
                     rh264_cabac_p_part(cb, f, rp, sh, mvg, absmvd, gwmax, ghmax,
                           mbx, mby, px,   py, 4, 8, RH264_MVP_MEDIAN, rr, picid, l0poc);
                     rh264_cabac_p_part(cb, f, rp, sh, mvg, absmvd, gwmax, ghmax,
                           mbx, mby, px+4, py, 4, 8, RH264_MVP_MEDIAN, rr, picid, l0poc);
                  }
                  else
                  {
                     rh264_cabac_p_part(cb, f, rp, sh, mvg, absmvd, gwmax, ghmax,
                           mbx, mby, px,   py,   4, 4, RH264_MVP_MEDIAN, rr, picid, l0poc);
                     rh264_cabac_p_part(cb, f, rp, sh, mvg, absmvd, gwmax, ghmax,
                           mbx, mby, px+4, py,   4, 4, RH264_MVP_MEDIAN, rr, picid, l0poc);
                     rh264_cabac_p_part(cb, f, rp, sh, mvg, absmvd, gwmax, ghmax,
                           mbx, mby, px,   py+4, 4, 4, RH264_MVP_MEDIAN, rr, picid, l0poc);
                     rh264_cabac_p_part(cb, f, rp, sh, mvg, absmvd, gwmax, ghmax,
                           mbx, mby, px+4, py+4, 4, 4, RH264_MVP_MEDIAN, rr, picid, l0poc);
                  }
               }
            }

            /* coded_block_pattern (9.3.3.1.1.4) */
            {
               int cbp = 0, k;
               for (k = 0; k < 4; k++)
               {
                  int bx = (k&1), by = (k>>1), a, bb;
                  if (bx > 0) a = !((cbp >> (k-1)) & 1);
                  else        a = (have_left && L->avail)
                                 ? !((L->cbpLuma >> (k+1)) & 1) : 0;
                  if (by > 0) bb = !((cbp >> (k-2)) & 1);
                  else        bb = (have_up && U->avail)
                                 ? !((U->cbpLuma >> (k+2)) & 1) : 0;
                  if (rh264_cabac_decode(cb, CTX_CBP_LUMA + a + 2*bb))
                     cbp |= (1 << k);
               }
               cbp_luma = cbp;
               if (!f->c444)   /* no chroma bins in 4:4:4 */
               {
                  int a  = (have_left && L->avail) ? (L->cbpChroma != 0) : 0;
                  int bb = (have_up   && U->avail) ? (U->cbpChroma != 0) : 0;
                  if (rh264_cabac_decode(cb, CTX_CBP_CHROMA + a + 2*bb))
                  {
                     a  = (have_left && L->avail) ? (L->cbpChroma == 2) : 0;
                     bb = (have_up   && U->avail) ? (U->cbpChroma == 2) : 0;
                     cbp_chroma = rh264_cabac_decode(cb,
                           CTX_CBP_CHROMA + 4 + a + 2*bb) ? 2 : 1;
                  }
               }
            }
            tmp.cbpLuma = cbp_luma; tmp.cbpChroma = cbp_chroma;

            /* transform_size_8x8_flag (after the cbp, 7.3.5) */
            if (cbp_luma && pps->transform_8x8_mode && t8ok)
            {
               int a  = (have_left && L->avail) ? L->t8 : 0;
               int bb = (have_up   && U->avail) ? U->t8 : 0;
               t8 = rh264_cabac_decode(cb, 399 + a + bb);
            }
            tmp.t8 = t8;
            if (t8) f->mbt8[mby*mbw + mbx] = 1;

            /* mb_qp_delta */
            {
               int dqp = 0;
               if (cbp_luma || cbp_chroma)
               {
                  if (rh264_cabac_decode(cb, CTX_QP_DELTA + (prevQpNZ?1:0)))
                  {
                     int k = 1;
                     if (rh264_cabac_decode(cb, CTX_QP_DELTA+2))
                     {
                        k = 2;
                        /* bounded: see the I-slice path */
                        while (rh264_cabac_decode(cb, CTX_QP_DELTA+3)
                              && k < 88) k++;
                     }
                     dqp = (k&1) ? (k+1)/2 : -(k/2);
                  }
                  prevQpNZ = (dqp != 0);
               }
               else prevQpNZ = 0;
               if (rh264_qp_apply_delta(f, dqp))
               { return -1; }
            }

            rh264_cabac_p_residual(cb, f, mbx, mby, cbp_luma, cbp_chroma,
                  &tmp, L, U, have_left, have_up, t8);
            f->mbqp[mby*mbw+mbx] = (uint8_t)RH264_QPP(f);
         }

         if (bot) { leftB = tmp; row[mbx] = tmp; }
         else if (f->mbaff) { leftT = tmp; toprow[mbx] = tmp; }
         else { leftT = tmp; row[mbx] = tmp; }
         if (bot) { leftskipB = skip; skiprow[mbx] = (uint8_t)skip; }
         else if (f->mbaff) { leftskipT = skip; topskip[mbx] = (uint8_t)skip; }
         else { leftskipT = skip; skiprow[mbx] = (uint8_t)skip; }

         if (f->row_done && mbx == mbw - 1)
            f->row_done(f->row_user, mby);
         if (mba == mbw*mbh-1) break;
         if (!(f->mbaff && !bot) && rh264_cabac_terminate(cb))
               { slice_end = mba+1; break; }
      }
      }
   if (end_mb) *end_mb = slice_end;
   return 0;
}


/* ---- rh264_cabac_b.h ---- */
/* mb_type of a B slice (9.3.2.5 Table 9-37, tree as in the reference
 * decoder): 0 = direct, 1..3 = 16x16, 4..21 = 16x8/8x16, 22 = B_8x8,
 * 23 = intra 4x4, 24 = start of the intra 16x16 family whose suffix the
 * caller decodes. */
static int rh264_cabac_b_mb_type(rh264_cabac *cb, int inc)
{
   int v;
   if (!rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + inc))
      return 0;
   if (!rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 3))
      return rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 5) ? 2 : 1;
   if (rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 4))
   {
      v = 12;
      if (rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 5)) v += 8;
      if (rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 5)) v += 4;
      if (rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 5)) v += 2;
      if (v == 24) return 11;
      if (v == 26) return 22;
      if (v == 22) v = 23;
      if (rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 5)) v += 1;
      return v;
   }
   v = 3;
   if (rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 5)) v += 4;
   if (rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 5)) v += 2;
   if (rh264_cabac_decode(cb, RH264_CTX_MB_TYPE_B + 5)) v += 1;
   return v;
}

/* sub_mb_type of a B slice (Table 9-38): 0 = direct, 1..12. */
static int rh264_cabac_b_sub(rh264_cabac *cb)
{
   int v;
   if (!rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 0))
      return 0;
   if (!rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 1))
      return rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 3) ? 2 : 1;
   if (rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 2))
   {
      if (rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 3))
      {
         v = 10;
         if (rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 3)) v++;
      }
      else
      {
         v = 6;
         if (rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 3)) v += 2;
         if (rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 3)) v++;
      }
   }
   else
   {
      v = 2;
      if (rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 3)) v += 2;
      if (rh264_cabac_decode(cb, RH264_CTX_SUB_TYPE_B + 3)) v++;
   }
   return v + 1;
}

/* Record a partition's absolute vector difference over its cells so later
 * reads pick the right mvd context (9.3.3.1.1.7). */
static void rh264_absmvd_store(int16_t *plane, int gwmax, int gx, int gy,
      int bw4, int bh4, int mdx, int mdy)
{
   int ax = mdx < 0 ? -mdx : mdx, ay = mdy < 0 ? -mdy : mdy, x, y;
   for (y = 0; y < bh4; y++)
      for (x = 0; x < bw4; x++)
      {
         int o = ((gy + y) * gwmax + gx + x) * 2;
         plane[o] = (int16_t)ax; plane[o + 1] = (int16_t)ay;
      }
}

/* Read one mvd pair for a partition at gx,gy and record it. */
static void rh264_cabac_b_mvd(rh264_cabac *cb, int16_t *plane, int gwmax,
      int gx, int gy, int bw4, int bh4, int *mdx, int *mdy)
{
   *mdx = rh264_cabac_mvd(cb, RH264_CTX_MVD_X,
         rh264_cabac_mvd_absum(plane, gwmax, gx, gy, 0));
   *mdy = rh264_cabac_mvd(cb, RH264_CTX_MVD_Y,
         rh264_cabac_mvd_absum(plane, gwmax, gx, gy, 1));
   rh264_absmvd_store(plane, gwmax, gx, gy, bw4, bh4, *mdx, *mdy);
}

/* Full B-slice decode, CABAC entropy. The macroblock structure follows the
 * CAVLC B decoder exactly -- direct derivation first, all list 0 syntax
 * then all list 1, motion resolved one partition at a time in decode
 * order -- with the binarizations of 9.3.2/9.3.3 in place of the Exp-Golomb
 * reads and the P slice's coded-block-flag and skip bookkeeping. */
static int rh264_cabac_decode_bslice(rh264_bits *b, const rh264_sps *sps,
      const rh264_pps *pps, rh264_slice_hdr *sh, rh264_frame *f,
      const rh264_bctx *bc, rh264_mv *mvg, int *end_mb,
      rh264_cabac *cb)
{
   int slice_end;
   rh264_cbf *row, dummy;
   uint8_t *skiprow, *typerow;
   int16_t *am0, *am1;
   int mbw = f->mbw, mbh = f->mbh, mbx, mby, prevQpNZ = 0;
   int gw = mbw*4, cgw = mbw*2;
   int gwmax = mbw*4, ghmax = mbh*4;
   size_t bytepos;

   while (b->bitpos & 7) { if (!rh264_u1(b)) break; }
   bytepos = (b->bitpos + 7) >> 3;
   rh264_cabac_init_engine(cb, b->buf + bytepos, b->buf + b->size);
   cb->field = f->field;
   cb->c422 = (f->cmbh == 16 && !f->c444);
   cb->c444 = f->c444;
   rh264_cabac_init_contexts(cb, sh->slice_qp, sh->cabac_init_idc);
   f->qp = sh->slice_qp;
   f->chroma_qp_offset = pps->chroma_qp_index_offset;
   f->constrained_intra = pps->constrained_intra_pred_flag;
   f->chroma_qp_offset2 = pps->chroma_qp_index_offset2;
   f->tb = sps->tb;
   if (bc->n0 < 1 || bc->n1 < 1) return -1;

   /* the coefficient/mode context describes the picture being decoded; a
    * continuation slice must keep what earlier slices of it produced */
   if (sh->first_mb_in_slice == 0)
   {
      memset(f->nzL, 0, (size_t)gw * mbh * 4);
      memset(f->nzC[0], 0, (size_t)cgw * mbh * (f->cmbh/4));
      memset(f->nzC[1], 0, (size_t)cgw * mbh * (f->cmbh/4));
      memset(f->i4mode, 0xff, (size_t)gw * mbh * 4);
      memset(f->mbt8, 0, (size_t)mbw * mbh);
   }

   /* Scratch lives on the working frame; zero what this slice uses. */
   row     = f->scr_row;
   skiprow = f->scr_skiprow;
   typerow = f->scr_typerow;
   am0     = f->scr_am0;
   am1     = f->scr_am1;
   if (!row) return -1;
   memset(row,     0, ((size_t)mbw+2) * sizeof(rh264_cbf));
   memset(skiprow, 0, (size_t)mbw+2);
   memset(typerow, 0, (size_t)mbw+2);
   memset(am0,     0, (size_t)gwmax*ghmax*2 * sizeof(int16_t));
   memset(am1,     0, (size_t)gwmax*ghmax*2 * sizeof(int16_t));
   memset(&dummy, 0, sizeof(dummy));

   slice_end = mbw * mbh;
   {
      /* address order with the pair contexts; see the intra reader */
      rh264_cbf leftT, leftB, *toprow = NULL;
      uint8_t *topskip = NULL, *toptype = NULL;
      int leftskipT = 0, leftskipB = 0, lefttypeT = 0, lefttypeB = 0;
      int mba, bot, prev_skipped = 0;
      if (f->mbaff)
      {
         toprow  = f->scr_toprow;
         topskip = f->scr_topskip;
         toptype = f->scr_toptype;
         memset(toprow,  0, ((size_t)mbw+2) * sizeof(rh264_cbf));
         memset(topskip, 0, (size_t)mbw+2);
         memset(toptype, 0, (size_t)mbw+2);
      }
      memset(&leftT, 0, sizeof(leftT));
      memset(&leftB, 0, sizeof(leftB));
      for (mba = sh->first_mb_in_slice; mba < mbw*mbh; mba++)
      {
         int have_up, have_left, skip, inc, cx, cy, mb_type = 0, la, ua;
         rh264_cbf tmp, *L, *U;
         uint8_t *upskip, *uptype;
         int leftskip, lefttype;
         rh264_mb_pos(mba, mbw, f->mbaff, &mbx, &mby);
         /* temporal direct reads RefPicList1[0]'s co-located cells of this
          * row, from a picture that may still be decoding: its motion is
          * final with the row's samples, so the same wait, for this row
          * and the pair row under it, once per macroblock (it returns at
          * once when the rows are there) */
         if (bc->n1 > 0 && bc->l1[0])
            rh264_ref_wait_rows(f, bc->l1[0], mby * 16, 32, 0);
         bot = f->mbaff && (mba & 1);
         if (mbx == 0 && !bot)
         {
            memset(&leftT, 0, sizeof(leftT));
            memset(&leftB, 0, sizeof(leftB));
            leftskipT = leftskipB = lefttypeT = lefttypeB = 0;
         }
         la = rh264_mb_addr(mbx-1, mby, mbw, f->mbaff);
         ua = rh264_mb_addr(mbx, mby-1, mbw, f->mbaff);
         /* in this slice (6.4.8) and already decoded */
         have_up   = (mby > 0) && ua >= sh->first_mb_in_slice && ua < mba;
         have_left = (mbx > 0) && la >= sh->first_mb_in_slice && la < mba;
         memset(&tmp, 0, sizeof(tmp));
         L = have_left ? (bot ? &leftB : &leftT) : &dummy;
         U = have_up   ? (bot ? &toprow[mbx] : &row[mbx]) : &dummy;
         upskip   = bot ? topskip : skiprow;
         uptype   = bot ? toptype : typerow;
         leftskip = bot ? leftskipB : leftskipT;
         lefttype = bot ? lefttypeB : lefttypeT;

         inc  = rh264_cabac_pskip_ctx(have_left, have_up, upskip, mbx,
               leftskip);
         skip = rh264_cabac_decode(cb, RH264_CTX_MB_SKIP_B + inc);
         /* mb_field_decoding_flag follows mb_skip_flag (7.3.4) */
         if (f->mbaff && !skip && (!bot || prev_skipped)
               && rh264_cabac_decode(cb, 70))
         { return -1; }
         prev_skipped = skip;

         if (skip)
         {
            if (rh264_b_direct_mb(f, bc, sh, mvg, gwmax, ghmax, mbx, mby)
                  != 0)
            { return -1; }
            for (cy = 0; cy < 4; cy++)
               memset(f->nzL + (size_t)(mby*4+cy)*gw + mbx*4, 0, 4);
            rh264_nzc_clear(f, mbx, mby);
            rh264_inter_clear_i4mode(f, mbx, mby);
            tmp.avail = 1;
            prevQpNZ = 0;
            f->mbqp[mby*mbw+mbx] = (uint8_t)RH264_QPP(f);
         }
         else
         {
            int cbp_luma = 0, cbp_chroma = 0;
            int t8 = 0, t8ok = 0;
            signed char curref0[16], curref1[16];
            memset(curref0, -2, sizeof(curref0));
            memset(curref1, -2, sizeof(curref1));
            tmp.avail = 1;
            inc = (have_left && lefttype ? 1 : 0)
                + (have_up && uptype[mbx] ? 1 : 0);
            mb_type = rh264_cabac_b_mb_type(cb, inc);
            /* transform_size_8x8_flag is only coded when no partition is
             * smaller than 8x8; direct prediction qualifies only under
             * direct_8x8_inference (7.3.5, noSubMbPartSizeLessThan8x8Flag) */
            t8ok = (mb_type >= 1 && mb_type <= 21) ? 1
                 : (mb_type == 0 ? bc->d8x8 : 0);

            if (mb_type >= 23)
            {
               static const int mbt_p[7] = { RH264_CTX_MB_TYPE_PI,
                  RH264_CTX_MB_TYPE_PI+1, RH264_CTX_MB_TYPE_PI+2,
                  RH264_CTX_MB_TYPE_PI+2, RH264_CTX_MB_TYPE_PI+3,
                  RH264_CTX_MB_TYPE_PI+3, 0 };
               int r2 = rh264_cabac_decode_mb_ctx(cb, sps, pps, f, mbx, mby,
                     &prevQpNZ, &tmp, L, U, mbt_p, mb_type > 23,
                     sh->first_mb_in_slice);
               if (r2 < 0)
               { return r2; }
               for (cy = 0; cy < 4; cy++) for (cx = 0; cx < 4; cx++)
               {
                  int o = ((mby*4+cy)*gwmax + mbx*4+cx);
                  memset(&mvg[o], 0, sizeof(mvg[o]));
                  mvg[o].ref = -1; mvg[o].pic = -1;
                  mvg[o].ref1 = -1; mvg[o].pic1 = -1; mvg[o].intra = 1;
               }
               f->mbqp[mby*mbw+mbx] = (uint8_t)RH264_QPP(f);
               if (bot) { leftB = tmp; row[mbx] = tmp; }
               else if (f->mbaff) { leftT = tmp; toprow[mbx] = tmp; }
               else { leftT = tmp; row[mbx] = tmp; }
               if (bot) { leftskipB = 0; skiprow[mbx] = 0; }
               else if (f->mbaff) { leftskipT = 0; topskip[mbx] = 0; }
               else { leftskipT = 0; skiprow[mbx] = 0; }
               if (bot) { lefttypeB = 1; typerow[mbx] = 1; }
               else if (f->mbaff) { lefttypeT = 1; toptype[mbx] = 1; }
               else { lefttypeT = 1; typerow[mbx] = 1; }
               if (f->row_done && mbx == mbw - 1)
                  f->row_done(f->row_user, mby);
               if (mba == mbw*mbh-1) break;
               if (!(f->mbaff && !bot) && rh264_cabac_terminate(cb))
               { slice_end = mba+1; break; }
               continue;
            }
            rh264_inter_clear_i4mode(f, mbx, mby);

            if (mb_type == 0)
            {
               if (rh264_b_direct_mb(f, bc, sh, mvg, gwmax, ghmax, mbx, mby)
                     != 0)
               { return -1; }
            }
            else if (mb_type <= 3)
            {
               int pd = rh264_b_pdir16[mb_type];
               int r0 = -1, r1 = -1, d0x = 0, d0y = 0, d1x = 0, d1y = 0;
               int m0x, m0y, m1x, m1y;
               if (pd == 0 || pd == 2)
               {
                  r0 = rh264_cabac_ref_idx(cb, mvg, gwmax, mbx*4, mby*4,
                        bc->n0, curref0, mbx, mby, 0);
                  for (cy = 0; cy < 16; cy++) curref0[cy] = (signed char)r0;
               }
               if (pd == 1 || pd == 2)
               {
                  r1 = rh264_cabac_ref_idx(cb, mvg, gwmax, mbx*4, mby*4,
                        bc->n1, curref1, mbx, mby, 1);
                  for (cy = 0; cy < 16; cy++) curref1[cy] = (signed char)r1;
               }
               if (r0 >= 0)
                  rh264_cabac_b_mvd(cb, am0, gwmax, mbx*4, mby*4, 4, 4,
                        &d0x, &d0y);
               if (r1 >= 0)
                  rh264_cabac_b_mvd(cb, am1, gwmax, mbx*4, mby*4, 4, 4,
                        &d1x, &d1y);
               rh264_b_part(f, bc, sh, mvg, gwmax, ghmax, mbx, mby,
                     0, 0, 16, 16, r0, d0x, d0y, r1, d1x, d1y,
                     RH264_MVP_MEDIAN, &m0x, &m0y, &m1x, &m1y);
               rh264_b_pred_block(f, bc, sh, mbx, mby, 0, 0, 16, 16,
                     r0, m0x, m0y, r1, m1x, m1y);
            }
            else if (mb_type <= 21)
            {
               int is16x8 = ((mb_type & 1) == 0);
               int pd0 = rh264_b_pdir2[(mb_type - 4) >> 1][0];
               int pd1 = rh264_b_pdir2[(mb_type - 4) >> 1][1];
               int r0a = -1, r0b = -1, r1a = -1, r1b = -1;
               int d0[4], d1[4];
               int pi;
               int hints[2];
               int m0[4], m1[4];
               int px1 = is16x8 ? 0 : 2, py1 = is16x8 ? 2 : 0;
               int pw4 = is16x8 ? 4 : 2, ph4 = is16x8 ? 2 : 4;
               hints[0] = is16x8 ? RH264_MVP_B : RH264_MVP_A;
               hints[1] = is16x8 ? RH264_MVP_A : RH264_MVP_C;
               d0[0] = d0[1] = d0[2] = d0[3] = 0;
               d1[0] = d1[1] = d1[2] = d1[3] = 0;
               if (pd0 == 0 || pd0 == 2)
               {
                  r0a = rh264_cabac_ref_idx(cb, mvg, gwmax, mbx*4, mby*4,
                        bc->n0, curref0, mbx, mby, 0);
                  for (cy = 0; cy < ph4; cy++) for (cx = 0; cx < pw4; cx++)
                     curref0[cy*4+cx] = (signed char)r0a;
               }
               if (pd1 == 0 || pd1 == 2)
               {
                  r0b = rh264_cabac_ref_idx(cb, mvg, gwmax,
                        mbx*4 + px1, mby*4 + py1, bc->n0, curref0, mbx, mby,
                        0);
                  for (cy = 0; cy < ph4; cy++) for (cx = 0; cx < pw4; cx++)
                     curref0[(py1+cy)*4 + px1+cx] = (signed char)r0b;
               }
               if (pd0 == 1 || pd0 == 2)
               {
                  r1a = rh264_cabac_ref_idx(cb, mvg, gwmax, mbx*4, mby*4,
                        bc->n1, curref1, mbx, mby, 1);
                  for (cy = 0; cy < ph4; cy++) for (cx = 0; cx < pw4; cx++)
                     curref1[cy*4+cx] = (signed char)r1a;
               }
               if (pd1 == 1 || pd1 == 2)
               {
                  r1b = rh264_cabac_ref_idx(cb, mvg, gwmax,
                        mbx*4 + px1, mby*4 + py1, bc->n1, curref1, mbx, mby,
                        1);
                  for (cy = 0; cy < ph4; cy++) for (cx = 0; cx < pw4; cx++)
                     curref1[(py1+cy)*4 + px1+cx] = (signed char)r1b;
               }
               if (r0a >= 0)
                  rh264_cabac_b_mvd(cb, am0, gwmax, mbx*4, mby*4,
                        pw4, ph4, &d0[0], &d0[1]);
               if (r0b >= 0)
                  rh264_cabac_b_mvd(cb, am0, gwmax, mbx*4 + px1,
                        mby*4 + py1, pw4, ph4, &d0[2], &d0[3]);
               if (r1a >= 0)
                  rh264_cabac_b_mvd(cb, am1, gwmax, mbx*4, mby*4,
                        pw4, ph4, &d1[0], &d1[1]);
               if (r1b >= 0)
                  rh264_cabac_b_mvd(cb, am1, gwmax, mbx*4 + px1,
                        mby*4 + py1, pw4, ph4, &d1[2], &d1[3]);
               for (pi = 0; pi < 2; pi++)
               {
                  int bx = is16x8 ? 0 : pi * 8, by = is16x8 ? pi * 8 : 0;
                  int bw = is16x8 ? 16 : 8,     bh = is16x8 ? 8 : 16;
                  int rr0 = pi ? r0b : r0a, rr1 = pi ? r1b : r1a;
                  rh264_b_part(f, bc, sh, mvg, gwmax, ghmax, mbx, mby,
                        bx, by, bw, bh, rr0, d0[pi*2], d0[pi*2+1],
                        rr1, d1[pi*2], d1[pi*2+1], hints[pi],
                        &m0[pi*2], &m0[pi*2+1], &m1[pi*2], &m1[pi*2+1]);
                  rh264_b_pred_block(f, bc, sh, mbx, mby, bx, by, bw, bh,
                        rr0, m0[pi*2], m0[pi*2+1], rr1, m1[pi*2],
                        m1[pi*2+1]);
               }
            }
            else
            {
               int sub[4], rf0[4], rf1[4], p;
               int l0r = -1, l1r = -1, pm0x = 0, pm0y = 0, pm1x = 0,
                   pm1y = 0;
               int need_prepare = 0;
               int16_t sd0[4][8], sd1[4][8];
               for (p = 0; p < 4; p++)
               {
                  sub[p] = rh264_cabac_b_sub(cb);
                  if (rh264_b_sub_pdir[sub[p]] == 3) need_prepare = 1;
               }
               t8ok = 1;
               for (p = 0; p < 4; p++)
               {
                  if (rh264_b_sub_pdir[sub[p]] == 3)
                  { if (!bc->d8x8) t8ok = 0; }
                  else if (rh264_b_sub_w[sub[p]] != 8
                        || rh264_b_sub_h[sub[p]] != 8)
                     t8ok = 0;
               }
               if (need_prepare)
               {
                  if (bc->direct_spatial)
                     rh264_b_direct_prepare(bc, mvg, gwmax, ghmax, mbx, mby,
                           &l0r, &l1r, &pm0x, &pm0y, &pm1x, &pm1y);
                  for (p = 0; p < 4; p++)
                     if (rh264_b_sub_pdir[sub[p]] == 3)
                     {
                        int sx, sy;
                        int step = bc->d8x8 ? 2 : 1;
                        for (sy = 0; sy < 2; sy += step)
                           for (sx = 0; sx < 2; sx += step)
                              if (rh264_b_direct_cells(bc, mvg, gwmax, mbx,
                                    mby, (p & 1) * 2 + sx, (p >> 1) * 2 + sy,
                                    step, l0r, l1r, pm0x, pm0y, pm1x, pm1y)
                                    != 0)
                              { return -1; }
                        for (sy = 0; sy < 2; sy++) for (sx = 0; sx < 2; sx++)
                        {
                           curref0[((p>>1)*2+sy)*4 + (p&1)*2+sx] = -3;
                           curref1[((p>>1)*2+sy)*4 + (p&1)*2+sx] = -3;
                        }
                     }
               }
               for (p = 0; p < 4; p++)
               {
                  int pd = rh264_b_sub_pdir[sub[p]];
                  int qx = (p&1)*2, qy = (p>>1)*2;
                  rf0[p] = -1;
                  if (pd == 0 || pd == 2)
                  {
                     rf0[p] = rh264_cabac_ref_idx(cb, mvg, gwmax,
                           mbx*4 + qx, mby*4 + qy, bc->n0, curref0, mbx, mby,
                           0);
                     for (cy = 0; cy < 2; cy++) for (cx = 0; cx < 2; cx++)
                        curref0[(qy+cy)*4 + qx+cx] = (signed char)rf0[p];
                  }
               }
               for (p = 0; p < 4; p++)
               {
                  int pd = rh264_b_sub_pdir[sub[p]];
                  int qx = (p&1)*2, qy = (p>>1)*2;
                  rf1[p] = -1;
                  if (pd == 1 || pd == 2)
                  {
                     rf1[p] = rh264_cabac_ref_idx(cb, mvg, gwmax,
                           mbx*4 + qx, mby*4 + qy, bc->n1, curref1, mbx, mby,
                           1);
                     for (cy = 0; cy < 2; cy++) for (cx = 0; cx < 2; cx++)
                        curref1[(qy+cy)*4 + qx+cx] = (signed char)rf1[p];
                  }
               }
               for (p = 0; p < 4; p++)
               {
                  int sw = rh264_b_sub_w[sub[p]], shh = rh264_b_sub_h[sub[p]];
                  int px = (p & 1) * 8, py = (p >> 1) * 8;
                  int q = 0, oy, ox, mdx, mdy;
                  if (rf0[p] < 0) continue;
                  for (oy = 0; oy < 8; oy += shh)
                     for (ox = 0; ox < 8; ox += sw)
                     {
                        rh264_cabac_b_mvd(cb, am0, gwmax,
                              mbx*4 + ((px+ox)>>2), mby*4 + ((py+oy)>>2),
                              sw>>2, shh>>2, &mdx, &mdy);
                        sd0[p][q*2] = (int16_t)mdx;
                        sd0[p][q*2+1] = (int16_t)mdy;
                        q++;
                     }
               }
               for (p = 0; p < 4; p++)
               {
                  int sw = rh264_b_sub_w[sub[p]], shh = rh264_b_sub_h[sub[p]];
                  int px = (p & 1) * 8, py = (p >> 1) * 8;
                  int q = 0, oy, ox, mdx, mdy;
                  if (rf1[p] < 0) continue;
                  for (oy = 0; oy < 8; oy += shh)
                     for (ox = 0; ox < 8; ox += sw)
                     {
                        rh264_cabac_b_mvd(cb, am1, gwmax,
                              mbx*4 + ((px+ox)>>2), mby*4 + ((py+oy)>>2),
                              sw>>2, shh>>2, &mdx, &mdy);
                        sd1[p][q*2] = (int16_t)mdx;
                        sd1[p][q*2+1] = (int16_t)mdy;
                        q++;
                     }
               }
               for (p = 0; p < 4; p++)
               {
                  int sw = rh264_b_sub_w[sub[p]], shh = rh264_b_sub_h[sub[p]];
                  int px = (p & 1) * 8, py = (p >> 1) * 8;
                  int list;
                  if (rh264_b_sub_pdir[sub[p]] == 3) continue;
                  for (list = 0; list < 2; list++)
                  {
                     int rf = list ? rf1[p] : rf0[p];
                     const int16_t *sd = list ? sd1[p] : sd0[p];
                     int q = 0, oy, ox;
                     for (oy = 0; oy < 8; oy += shh)
                        for (ox = 0; ox < 8; ox += sw)
                        {
                           int gx = mbx*4 + ((px+ox)>>2);
                           int gy = mby*4 + ((py+oy)>>2);
                           int pmx = 0, pmy = 0, mx = 0, my = 0;
                           if (rf >= 0)
                           {
                              rh264_pred_mv_ldir(mvg, gwmax, gwmax, ghmax,
                                    gx, gy, sw>>2, rf, RH264_MVP_MEDIAN,
                                    list, &pmx, &pmy);
                              mx = pmx + sd[q*2]; my = pmy + sd[q*2+1];
                           }
                           rh264_mv_fill_list(mvg, gwmax, gx, gy, sw>>2,
                                 shh>>2, list, mx, my, rf,
                                 rf < 0 ? -1
                                        : (list ? bc->pid1[rf] : bc->pid0[rf]),
                                 rf < 0 ? 0
                                        : (list ? bc->l1poc[rf] : bc->l0poc[rf]),
                                 0);
                           q++;
                        }
                  }
               }
               /* compensate off the cells, widest uniform block first
                * (direct subs included) */
               rh264_b_comp_mb(f, bc, sh, mvg, gwmax, mbx, mby);
            }

            /* coded_block_pattern (9.3.3.1.1.4) */
            {
               int cbp = 0, k;
               for (k = 0; k < 4; k++)
               {
                  int bx = (k&1), by = (k>>1), a, bb;
                  if (bx > 0) a = !((cbp >> (k-1)) & 1);
                  else        a = (have_left && L->avail)
                                 ? !((L->cbpLuma >> (k+1)) & 1) : 0;
                  if (by > 0) bb = !((cbp >> (k-2)) & 1);
                  else        bb = (have_up && U->avail)
                                 ? !((U->cbpLuma >> (k+2)) & 1) : 0;
                  if (rh264_cabac_decode(cb, CTX_CBP_LUMA + a + 2*bb))
                     cbp |= (1 << k);
               }
               cbp_luma = cbp;
               if (!f->c444)   /* no chroma bins in 4:4:4 */
               {
                  int a  = (have_left && L->avail) ? (L->cbpChroma != 0) : 0;
                  int bb = (have_up   && U->avail) ? (U->cbpChroma != 0) : 0;
                  if (rh264_cabac_decode(cb, CTX_CBP_CHROMA + a + 2*bb))
                  {
                     a  = (have_left && L->avail) ? (L->cbpChroma == 2) : 0;
                     bb = (have_up   && U->avail) ? (U->cbpChroma == 2) : 0;
                     cbp_chroma = rh264_cabac_decode(cb,
                           CTX_CBP_CHROMA + 4 + a + 2*bb) ? 2 : 1;
                  }
               }
            }
            tmp.cbpLuma = cbp_luma; tmp.cbpChroma = cbp_chroma;

            /* transform_size_8x8_flag (after the cbp, 7.3.5) */
            if (cbp_luma && pps->transform_8x8_mode && t8ok)
            {
               int a  = (have_left && L->avail) ? L->t8 : 0;
               int bb = (have_up   && U->avail) ? U->t8 : 0;
               t8 = rh264_cabac_decode(cb, 399 + a + bb);
            }
            tmp.t8 = t8;
            if (t8) f->mbt8[mby*mbw + mbx] = 1;

            /* mb_qp_delta */
            {
               int dqp = 0;
               if (cbp_luma || cbp_chroma)
               {
                  if (rh264_cabac_decode(cb, CTX_QP_DELTA + (prevQpNZ?1:0)))
                  {
                     int k = 1;
                     if (rh264_cabac_decode(cb, CTX_QP_DELTA+2))
                     {
                        k = 2;
                        /* bounded: see the I-slice path */
                        while (rh264_cabac_decode(cb, CTX_QP_DELTA+3)
                              && k < 88) k++;
                     }
                     dqp = (k&1) ? (k+1)/2 : -(k/2);
                  }
                  prevQpNZ = (dqp != 0);
               }
               else prevQpNZ = 0;
               if (rh264_qp_apply_delta(f, dqp))
               { return -1; }
            }

            rh264_cabac_p_residual(cb, f, mbx, mby, cbp_luma, cbp_chroma,
                  &tmp, L, U, have_left, have_up, t8);
            f->mbqp[mby*mbw+mbx] = (uint8_t)RH264_QPP(f);
         }

         if (bot) { leftB = tmp; row[mbx] = tmp; }
         else if (f->mbaff) { leftT = tmp; toprow[mbx] = tmp; }
         else { leftT = tmp; row[mbx] = tmp; }
         if (bot) { leftskipB = skip; skiprow[mbx] = (uint8_t)skip; }
         else if (f->mbaff) { leftskipT = skip; topskip[mbx] = (uint8_t)skip; }
         else { leftskipT = skip; skiprow[mbx] = (uint8_t)skip; }
         lefttype = (!skip && mb_type != 0);
         if (bot) { lefttypeB = lefttype; typerow[mbx] = (uint8_t)lefttype; }
         else if (f->mbaff)
         { lefttypeT = lefttype; toptype[mbx] = (uint8_t)lefttype; }
         else { lefttypeT = lefttype; typerow[mbx] = (uint8_t)lefttype; }

         if (f->row_done && mbx == mbw - 1)
            f->row_done(f->row_user, mby);
         if (mba == mbw*mbh-1) break;
         if (!(f->mbaff && !bot) && rh264_cabac_terminate(cb))
               { slice_end = mba+1; break; }
      }
      }
   if (end_mb) *end_mb = slice_end;
   return 0;
}

/* Copy the pixel planes of src into dst (both already allocated at the same
 * geometry). Used to place the just-decoded picture into the reference list. */
static void rh264_frame_copy_planes(rh264_frame *dst, const rh264_frame *src)
{
   /* The destination may be sharing its block with a picture waiting
    * to be shown or a reference still in use: a field written into it
    * must not land in those. */
   if (rh264_frame_own_planes(dst, dst->plane_len) < 0)
      return;
   memcpy(dst->Yb, src->Yb, RH264_OFF(src, (size_t)src->ysb * src->mbh_frame * 16));
   memcpy(dst->Ub, src->Ub, RH264_OFF(src, (size_t)src->csb * src->mbh_frame * src->cmbh));
   memcpy(dst->Vb, src->Vb, RH264_OFF(src, (size_t)src->csb * src->mbh_frame * src->cmbh));
   dst->qp = src->qp;
   dst->chroma_qp_offset = src->chroma_qp_offset;
   dst->chroma_qp_offset2 = src->chroma_qp_offset2;
}

/* Picture order count for the picture just parsed (8.2.1). Type 0 rebuilds
 * the count from its wrapping LSB against the previous reference picture;
 * type 2 makes it follow decode order, doubled, with non-reference pictures
 * sitting one below the next reference; type 1 builds the expected count
 * from the sequence's offset cycle, keyed by frame number, and adds the
 * slice's deltas. */
static int rh264_derive_poc(rh264_video *v, const rh264_slice_hdr *sh,
      int nal_ref_idc)
{
   const rh264_sps *sps = &v->sps;
   int poc;
   if (sh->is_idr)
   { v->prev_poc_msb = 0; v->prev_poc_lsb = 0; v->fn_offset = 0;
     v->prev_frame_num = 0; }
   if (sps->pic_order_cnt_type == 0)
   {
      int max_lsb = 1 << sps->log2_max_poc_lsb;
      int msb;
      if (sh->poc_lsb < v->prev_poc_lsb
            && (v->prev_poc_lsb - sh->poc_lsb) >= (max_lsb / 2))
         msb = v->prev_poc_msb + max_lsb;
      else if (sh->poc_lsb > v->prev_poc_lsb
            && (sh->poc_lsb - v->prev_poc_lsb) > (max_lsb / 2))
         msb = v->prev_poc_msb - max_lsb;
      else
         msb = v->prev_poc_msb;
      poc = msb + sh->poc_lsb;
      v->poc_top = poc;
      v->poc_bot = poc + sh->poc_bottom_delta;
      if (nal_ref_idc)
      { v->prev_poc_msb = msb; v->prev_poc_lsb = sh->poc_lsb; }
   }
   else if (sps->pic_order_cnt_type == 2)
   {
      int max_fn = 1 << sps->log2_max_frame_num;
      if (!sh->is_idr && sh->frame_num < v->prev_frame_num)
         v->fn_offset += max_fn;
      poc = 2 * (v->fn_offset + sh->frame_num);
      if (!nal_ref_idc && poc > 0) poc -= 1;
      v->poc_top = v->poc_bot = poc;
      v->prev_frame_num = sh->frame_num;
   }
   else
   {
      /* POC type 1 (8.2.1.2): the expected order comes from a per-frame
       * offset cycle keyed by frame number. */
      int max_fn = 1 << sps->log2_max_frame_num;
      int fno, absfn, expected = 0, top, bottom, i2;
      if (sh->is_idr)
         fno = 0;
      else if (sh->frame_num < v->prev_frame_num)
         fno = v->fn_offset + max_fn;
      else
         fno = v->fn_offset;
      absfn = sps->poc1_ncycle ? fno + sh->frame_num : 0;
      if (!nal_ref_idc && absfn > 0) absfn--;
      if (absfn > 0)
      {
         int cyc = (absfn - 1) / sps->poc1_ncycle;
         int inc = (absfn - 1) % sps->poc1_ncycle;
         int per = 0;
         for (i2 = 0; i2 < sps->poc1_ncycle; i2++)
            per += sps->poc1_offset_ref[i2];
         expected = cyc * per;
         for (i2 = 0; i2 <= inc; i2++)
            expected += sps->poc1_offset_ref[i2];
      }
      if (!nal_ref_idc) expected += sps->poc1_offset_non_ref;
      top = expected + sh->poc1_delta0;
      bottom = top + sps->poc1_offset_ttb + sh->poc1_delta1;
      poc = (top < bottom) ? top : bottom;
      v->poc_top = top;
      v->poc_bot = bottom;
      v->fn_offset = fno;
      v->prev_frame_num = sh->frame_num;
   }
   return poc;
}

/* Build a P field picture's reference list (8.2.4.2.2 with 8.2.4.2.5).
 * Stored frames are visited most-recent-first and their fields taken
 * alternately, starting with the parity of the picture being decoded;
 * a field holding no decoded data - the partner of the field being
 * decoded, most obviously - is skipped.  Each entry is a view aliasing
 * the stored frame with field strides, so the slice decoders read it
 * exactly as they read a frame. */
/* Convert an ordered list of reference FRAMES (given as DPB slots) into
 * a list of reference FIELDS per 8.2.4.2.5: alternate between the
 * parities, taking the next available field of each in turn, starting
 * with the parity of the picture being decoded.  Views are written into
 * v->fieldview from base so two lists can coexist. */
static int rh264_fields_from_slots(rh264_video *v, const int *slots,
      int ns, const rh264_frame **out, int *opn, int base, int max)
{
   int n = 0, want = v->cur_field, other = (want == 1) ? 2 : 1;
   int isame = 0, iopp = 0, parity = want;
   while (n < max)
   {
      int *ip = (parity == want) ? &isame : &iopp;
      int found = 0;
      while (*ip < ns)
      {
         int s = slots[*ip];
         (*ip)++;
         if (!(v->dpb_fields[s] & (1 << (parity - 1)))) continue;
         v->fieldview[base + n] = v->dpb[s];
         rh264_frame_set_field(&v->fieldview[base + n], parity);
         v->fieldview[base + n].poc = v->dpb_poc_fld[s][parity - 1];
         v->fieldview_id[base + n] =
               (signed char)(32 + s * 2 + (parity - 1));
         out[n] = &v->fieldview[base + n];
         if (opn)
            opn[n] = v->dpb_pn[s] * 2 + (parity == want ? 1 : 0);
         n++;
         found = 1;
         break;
      }
      if (!found && isame >= ns && iopp >= ns)
         break;
      parity = (parity == want) ? other : want;
   }
   return n;
}

static int rh264_build_field_list(rh264_video *v, const rh264_frame **l0,
      int *lpn, int max)
{
   int n = 0, want = v->cur_field, other = (want == 1) ? 2 : 1;
   int isame = 0, iopp = 0, parity = want, i;
   /* Long-term fields belong at the end of the list with their own
    * numbering (8.2.4.2.2 with 8.2.4.2.5).  That is not built here, and
    * silently leaving them out would hand the slice a list that names
    * the wrong pictures, so refuse the picture instead. */
   for (i = 0; i < v->dpb_len; i++)
      if (v->dpb_lt[v->dpb_slot[i]] >= 0)
         return 0;
   /* 8.2.4.2.5 alternates between the parities, taking the next
    * available field of each in turn - not every field of one parity
    * followed by every field of the other. */
   while (n < max)
   {
      int *ip = (parity == want) ? &isame : &iopp;
      int found = 0;
      while (*ip < v->dpb_len)
      {
         int s = v->dpb_slot[*ip];
         (*ip)++;
         if (v->dpb_lt[s] >= 0) continue;
         if (!(v->dpb_fields[s] & (1 << (parity - 1)))) continue;
         v->fieldview[n] = v->dpb[s];
         rh264_frame_set_field(&v->fieldview[n], parity);
         v->fieldview[n].poc = v->dpb_poc_fld[s][parity - 1];
         v->fieldview_id[n] = (signed char)(32 + s * 2 + (parity - 1));
         l0[n]  = &v->fieldview[n];
         /* PicNum is 2*FrameNumWrap + 1 for a field of the same parity
          * as the current picture, 2*FrameNumWrap otherwise (8.2.4.1) */
         lpn[n] = v->dpb_pn[s] * 2 + (parity == want ? 1 : 0);
         n++;
         found = 1;
         break;
      }
      if (!found && isame >= v->dpb_len && iopp >= v->dpb_len)
         break;
      parity = (parity == want) ? other : want;
   }
   return n;
}

/* Queue the just-decoded picture for display. Returns the slot to show now
 * (lowest generation, then POC) once more than reorder_delay pictures wait,
 * or when an IDR arrives with nothing pending; -1 while buffering. */
static int rh264_out_push(rh264_video *v, int poc, int is_idr)
{
   int i, slot = -1, bi;
   for (i = 0; i < RH264_OUT_SLOTS; i++)
      if (!v->out_used[i] && i != v->out_show) { slot = i; break; }
   if (slot < 0) return -1;            /* cannot happen: delay < slot count */
   /* A slot that has been shown and is not the one on show holds its
    * block for nothing: let it go now, so the block is back on the
    * free list for the next picture rather than pinned in a slot that
    * may not come round for a dozen pictures. */
   for (i = 0; i < RH264_OUT_SLOTS; i++)
      if (!v->out_used[i] && i != v->out_show && v->out[i].planes
            && !v->out[i].field)
      {
         rh264_planes_release(v->out[i].planes);
         v->out[i].planes = NULL;
         v->out[i].Yb = v->out[i].Ub = v->out[i].Vb = NULL;
         v->out[i].Y  = v->out[i].U  = v->out[i].V  = NULL;
      }
   /* A frame picture shares its block into the slot: the slot needs
    * its bookkeeping, not a block of its own, which would be made,
    * zeroed and let go again on the spot. Only a field pair, which
    * copies, needs the slot to hold samples. */
   if (!v->out[slot].meta || (v->cur_field && !v->out[slot].Yb))
      if (rh264_frame_alloc(&v->out[slot], &v->sps,
               v->cur_field ? RH264_FRAME_ALLOC_PLAIN : RH264_FRAME_ALLOC_SHELL) != 0)
         return -1;
   if (!v->cur_field)
   {
      /* Frame pictures swap their planes into the output slot instead
       * of copying them: the reference copy (when the picture is one)
       * already sits in the DPB, every macroblock of the next picture
       * rewrites the working planes, and the slot was allocated from
       * the same SPS so the geometry is identical.  Field pairs keep
       * the copy - their two pictures fill the working planes
       * incrementally, which a swapped-in stale buffer would break. */
      rh264_frame_share_planes(&v->out[slot], &v->cur->f);
   }
   else
      rh264_frame_copy_planes(&v->out[slot], &v->cur->f);
   v->out[slot].w = v->cur->f.w; v->out[slot].h = v->cur->f.h;
   v->out_poc[slot] = poc; v->out_gen[slot] = v->idr_gen;
   v->out_used[slot] = 1;  v->out_len++;
   /* An IDR arriving with nothing pending can leave at once: no picture
    * after it in decode order may precede it in output order. Otherwise
    * hold pictures until more than reorder_delay wait. */
   if (v->out_len <= v->reorder_delay && !(is_idr && v->out_len == 1))
      return -1;
   bi = -1;
   for (i = 0; i < RH264_OUT_SLOTS; i++)
   {
      if (!v->out_used[i]) continue;
      if (bi < 0 || v->out_gen[i] < v->out_gen[bi]
            || (v->out_gen[i] == v->out_gen[bi] && v->out_poc[i] < v->out_poc[bi]))
         bi = i;
   }
#ifdef HAVE_THREADS
   /* Pictures decoding on the pool: the one due leaves only once it
    * is complete. Held otherwise, and the next push asks again - the
    * reorder depth is what gives the pool its head start. When the
    * queue is full it must leave, and the wait is on its last row. */
   if (v->threaded && v->out[bi].planes && v->out[bi].mbh > 0
         && rh264_block_rows_final(v->out[bi].planes) < v->out[bi].mbh)
   {
      if (v->out_len < RH264_OUT_SLOTS - 1)
      {
         v->st_pop_held++;
         return -1;
      }
      v->st_pop_waits++;
      while (rh264_block_rows_final(v->out[bi].planes) < v->out[bi].mbh)
      {
         int key = retro_eventcount_prepare_wait(&rh264_rows_ec);
         if (rh264_block_rows_final(v->out[bi].planes) >= v->out[bi].mbh)
         {
            retro_eventcount_cancel_wait(&rh264_rows_ec);
            break;
         }
         retro_eventcount_commit_wait(&rh264_rows_ec, key);
      }
   }
#endif
   v->out_used[bi] = 0; v->out_len--;
   v->out_show = bi;
   return bi;
}

/* Mark every cell of the motion grid intra, the state an I picture leaves
 * behind: when it later serves as the co-located picture of a direct block,
 * 8.4.1.2 treats intra cells as static. */
static void rh264_mvg_set_intra(rh264_mv *mvg, int gwmax, int ghmax)
{
   int i;
   for (i = 0; i < gwmax * ghmax; i++)
   {
      rh264_mv z; memset(&z, 0, sizeof(z));
      z.ref = -1; z.ref1 = -1; z.pic = -1; z.pic1 = -1; z.intra = 1;
      mvg[i] = z;
   }
}

/* Record one decoded slice on the picture being assembled: its first
 * macroblock (slice extents come from consecutive slices' first addresses)
 * and its deblocking parameters, which clause 8.7 applies per macroblock
 * according to the slice that macroblock belongs to. */
static int rh264_video_note_slice(rh264_video *v, const rh264_slice_hdr *sh)
{
   int n = v->cur->pic_nslices;
   if (n >= RH264_MAX_SLICES) return -1;
   v->cur->pic_first[n] = sh->first_mb_in_slice;
   v->cur->pic_idc[n]   = (signed char)sh->disable_deblocking_filter_idc;
   v->cur->pic_oA[n]    = (signed char)sh->slice_alpha_c0_offset;
   v->cur->pic_oB[n]    = (signed char)sh->slice_beta_offset;
   v->cur->pic_nslices  = n + 1;
   return 0;
}

/* Fold the macroblock range a slice decoded into the picture's motion grid.
 * The grid the slice decoders work on (v->cur->mvg) restarts at every slice so
 * cells of other slices read as undecoded, exactly the neighbour
 * availability 6.4.8 asks for; the accumulated copy is what deblocking,
 * reference storage and temporal direct prediction see. */
static void rh264_ctx_fold_mvg(rh264_pic_ctx *c, int first, int end)
{
   int mbw = c->f.mbw, gw = mbw * 4, mb = first;
   if (!c->mvg || !c->pic_mvg) return;
   while (mb < end)
   {
      int mx = mb % mbw, gy = (mb / mbw) * 4, r;
      int n  = mbw - mx;                /* the rest of this row */
      if (n > end - mb)
         n = end - mb;
      if (n == mbw)
      {
         /* whole rows: their four grid rows are one contiguous run */
         int rows = (end - mb) / mbw;
         memcpy(c->pic_mvg + gy * gw, c->mvg + gy * gw,
               (size_t)rows * 4 * gw * sizeof(rh264_mv));
         mb += rows * mbw;
         continue;
      }
      for (r = 0; r < 4; r++)
         memcpy(c->pic_mvg + (gy + r) * gw + mx * 4,
                c->mvg     + (gy + r) * gw + mx * 4,
                (size_t)n * 4 * sizeof(rh264_mv));
      mb += n;
   }
}


static void rh264_video_row_done(void *user, int mby);


static void rh264_video_post_picture(rh264_video *v);
static void rh264_video_finish_picture(rh264_video *v, int *got_pic);


/* A slice that is not the first of its picture continues the picture
 * open on the context, at the macroblock where the last slice ended.
 * On one thread that end is known; with the slices queued for the
 * pool it is not yet, and the check is that the slices arrive in
 * raster order - the job checks each against the last one's end when
 * it runs them. */
static int rh264_video_slice_continues(const rh264_video *v, int first_mb)
{
   const rh264_pic_ctx *c = v->cur;
   if (!c->pic_open)
      return 0;
   if (v->threaded)
      return c->pic_nslices > 0
          && first_mb > c->pic_first[c->pic_nslices - 1];
   return first_mb == c->pic_end;
}
/* A new picture takes the next context round and readies it for the
 * sequence's geometry. The one it leaves keeps its frame: that is the
 * last picture's, shared into the DPB and the output queue, and the
 * context will take a block of its own when it comes round again. */
static int rh264_video_next_ctx(rh264_video *v, int *got_pic)
{
   if (v->threaded)
   {
      /* The picture leaving decodes on the pool from here; its place in
       * the sequence is settled now, with its rows still counting up. */
      rh264_video_post_picture(v);
      rh264_video_finish_picture(v, got_pic);
   }
   if (v->nctx > 1)
   {
      int i = (int)(v->cur - v->ctx);
      v->cur = &v->ctx[(i + 1) % v->nctx];
   }
#ifdef HAVE_THREADS
   if (retro_atomic_load_acquire_int(&v->cur->busy))
      v->st_join_waits++;
   rh264_ctx_join(v, v->cur);
#endif
   v->cur->posted = 0;
   return rh264_ctx_prepare(v, v->cur);
}

static rh264_slice_job *rh264_ctx_queue_slice(struct rh264_video *v, rh264_pic_ctx *c,
      const rh264_bits *b, const rh264_slice_hdr *sh, int kind, int cabac);
static void rh264_ctx_run_slices(struct rh264_video *v, rh264_pic_ctx *c, const struct rh264_vlc_set *vlc);

static int rh264_video_decode_idr(rh264_video *v, const uint8_t *nal, size_t len, int *got_pic)
{
   int nut, nri, rc;
   size_t rl;
   uint8_t *rbsp;
   rh264_bits b;
   rh264_slice_hdr sh;
   if (len < 1) return -1;
   nut = nal[0] & 0x1f;
   nri = (nal[0] >> 5) & 3;
   rbsp = rh264_unescape_scratch(v, nal + 1, len - 1, &rl);
   if (!rbsp) return -1;
   rh264_bits_init(&b, rbsp, rl);
   if (v->vlc_ready) b.vlc = &v->vlc;
   if (!rh264_parse_slice_header_adv(&b, nut, nri, &v->sps, &v->pps, &sh))
   { if (sh.switching) v->cur->saw_switching = 1; return -1; }
   if (sh.first_mb_in_slice == 0)
   {
      /* first slice: a new picture begins (a picture left unfinished by a
       * truncated stream is discarded rather than shown half-decoded) */
      int fld = sh.field_pic_flag ? (sh.bottom_field_flag ? 2 : 1) : 0;
      int second = fld && v->pair_open && v->pair_frame_num == sh.frame_num
            && v->cur_field && v->cur_field != fld;
      /* The picture leaving is sequenced by the rotation, which needs
       * it still open; it closes once the new one has its context. */
      if (!second && rh264_video_next_ctx(v, got_pic) != 0) return -1;
      v->cur->pic_open = 0;
      v->cur->sps = v->sps;
      v->cur->pps = v->pps;
      if (!second) v->idr_gen++;
      v->last_poc = rh264_derive_poc(v, &sh, nri);
      v->cur_field = fld;
      rh264_frame_set_field(&v->cur->f, fld);
      v->cur->f.mbaff = (!fld && v->sps.mb_adaptive_frame_field_flag) ? 1 : 0;
      rh264_frame_reset_ex(&v->cur->f, second);
      if (fld && second) { if(v->last_poc<v->pair_poc) v->pair_poc=v->last_poc;
                           v->pair_open=0; }
      else if (fld) { v->pair_open=1; v->pair_frame_num=sh.frame_num;
                      v->pair_poc=v->last_poc; }
      else v->pair_open=0;
      v->cur->pic_open = 1; v->cur->pic_kind = 1; v->cur->pic_ref = nri;
      v->cur->pic_end = 0;  v->cur->pic_nslices = 0;
      v->cur->f.rows_deblocked = 0;
      v->cur->f.row_user       = v->cur;
      v->cur->f.row_done       = (v->cur->f.mbaff || v->cur->f.field) ? NULL : rh264_video_row_done;
      if (v->cur->f.planes)
         rh264_block_publish_rows(v->cur->f.planes, 0);
      v->cur->job_rc    = 0;
      v->cur->completed = 0;
      v->pend_idr_ltr = sh.idr_ltr;
      rh264_resolve_scaling(&v->sps, &v->pps, v->cur->f.w4, v->cur->f.w8);
   }
   else if (!v->cur->pic_open || v->cur->pic_kind != 1
         || !rh264_video_slice_continues(v, sh.first_mb_in_slice))
   { return -1; }   /* continuation without its picture */
   if (rh264_video_note_slice(v, &sh) != 0)
   { v->cur->pic_open = 0; return -1; }
   /* Main/High-profile streams use CABAC entropy coding; baseline uses CAVLC.
    * Dispatch on the PPS entropy_coding_mode_flag. Both paths are intra-only. */
   if (!rh264_ctx_queue_slice(v, v->cur, &b, &sh, 0, v->pps.entropy_coding_mode_flag))
   { v->cur->pic_open = 0; return -1; }
   v->cur->vlc = v->vlc_ready ? &v->vlc : NULL;
   if (!v->threaded)
      rh264_ctx_run_slices(v, v->cur, v->cur->vlc);
   rc = v->cur->job_rc;
   if (rc != 0)
      v->cur->pic_open = 0;
   return rc;
}

/* Insert the just-decoded picture into the short-term reference list, which is
 * kept most-recent-first so reference index 0 names the newest picture -- the
 * order the default P list initialisation of 8.2.4.2.1 produces when there is
 * no list reordering. The oldest entry is evicted once the buffer is full
 * (the sliding window of 8.2.5.3). */
/* Adaptive reference marking, op 1 only (8.2.5.4.1): unmark the short-term
 * picture whose PicNum is CurrPicNum minus the signalled difference. Runs
 * before the current picture is stored. */
/* Remove list position j from the reference set. */
static void rh264_dpb_unmark_at(rh264_video *v, int j)
{
   memmove(&v->dpb_slot[j], &v->dpb_slot[j + 1],
         (size_t)(v->dpb_len - 1 - j) * sizeof(v->dpb_slot[0]));
   v->dpb_len--;
}

/* Find the held short-term picture with PicNum pn (FrameNumWrap of
 * 8.2.4.1); -1 when absent. */
static int rh264_dpb_find_short(rh264_video *v, int currfn, int pn)
{
   int j, maxpn = 1 << v->sps.log2_max_frame_num;
   for (j = 0; j < v->dpb_len; j++)
   {
      int s = v->dpb_slot[j];
      int fn, fnw;
      if (v->dpb_lt[s] >= 0) continue;
      fn = v->dpb_pn[s];
      fnw = (fn > currfn) ? (fn - maxpn) : fn;
      if (fnw == pn) return j;
   }
   return -1;
}

/* Adaptive reference marking (8.2.5.4). Returns the long_term_frame_idx the
 * current picture is to be stored under (op 6), or -1 to store it as a
 * short-term reference. On op 5 every held reference is dropped and the
 * POC/frame-number state restarts, so *epoch is set to tell the caller the
 * current picture opens a new output epoch with POC zero. */
static int rh264_dpb_marking(rh264_video *v, int currfn, int *epoch)
{
   int k, cur_lt = -1;
   int maxpn = 1 << v->sps.log2_max_frame_num;
   for (k = 0; k < v->pend_n_mmco; k++)
   {
      int op = v->pend_mmco_op[k];
      int a  = v->pend_mmco_a[k];
      int j;
      if (op == 1 || op == 3)
      {
         int pn = currfn - (a + 1);
         if (pn < 0) pn += maxpn;
         if (pn > currfn) pn -= maxpn;
         j = rh264_dpb_find_short(v, currfn, pn);
         if (j < 0) continue;
         if (op == 1)
            rh264_dpb_unmark_at(v, j);
         else
         {
            /* short-term -> long-term with the given index; a held
             * long-term under that index is dropped first (8.2.5.4.3) */
            int idx = v->pend_mmco_b[k], m;
            for (m = 0; m < v->dpb_len; m++)
               if (v->dpb_lt[v->dpb_slot[m]] == idx)
               { rh264_dpb_unmark_at(v, m); break; }
            j = rh264_dpb_find_short(v, currfn, pn);
            if (j >= 0) v->dpb_lt[v->dpb_slot[j]] = (signed char)idx;
         }
      }
      else if (op == 2)
      {
         /* unmark the long-term picture with LongTermPicNum a (equal to
          * long_term_frame_idx for frame coding) */
         for (j = 0; j < v->dpb_len; j++)
            if (v->dpb_lt[v->dpb_slot[j]] == a)
            { rh264_dpb_unmark_at(v, j); break; }
      }
      else if (op == 4)
      {
         v->max_lt_idx = a - 1;
         for (j = 0; j < v->dpb_len; )
         {
            if (v->dpb_lt[v->dpb_slot[j]] > v->max_lt_idx)
               rh264_dpb_unmark_at(v, j);
            else j++;
         }
      }
      else if (op == 5)
      {
         v->dpb_len = 0;
         v->max_lt_idx = -1;
         *epoch = 1;
      }
      else if (op == 6)
      {
         int idx = a, m;
         for (m = 0; m < v->dpb_len; m++)
            if (v->dpb_lt[v->dpb_slot[m]] == idx)
            { rh264_dpb_unmark_at(v, m); break; }
         cur_lt = idx;
      }
   }
   return cur_lt;
}

static void rh264_dpb_insert(rh264_video *v, int picnum, int lt)
{
   int slot, i;
   if (v->dpb_size <= 0) return;
   /* The two fields of a complementary pair are one reference frame:
    * the second field fills in the entry its partner opened rather
    * than taking a slot of its own. */
   if (v->cur_field && !v->pair_open && v->pair_slot >= 0)
   {
      slot = v->pair_slot;
      rh264_frame_copy_planes(&v->dpb[slot], &v->cur->f);
      v->dpb_fields[slot] |= (uint8_t)(1 << (v->cur_field - 1));
      v->dpb_poc_fld[slot][v->cur_field - 1] = v->last_poc;
      if (v->last_poc < v->dpb_poc[slot])
      { v->dpb_poc[slot] = v->last_poc; v->dpb[slot].poc = v->last_poc; }
      {
         rh264_mv *g;
         rh264_frame_own_mvg(&v->dpb[slot]);
         g = (v->cur_field == 2) ? v->dpb[slot].mvg2 : v->dpb[slot].mvg;
         if (g && v->cur->pic_mvg)
            memcpy(g, v->cur->pic_mvg,
                  (size_t)(v->cur->f.mbw * 4) * (v->cur->f.mbh * 4) * sizeof(rh264_mv));
      }
      v->pair_slot = -1;
      return;
   }
   if (v->dpb_len >= v->dpb_size)
   {
      /* sliding window (8.2.5.3): the oldest short-term reference leaves;
       * long-term pictures stay until unmarked by an explicit operation */
      for (i = v->dpb_len - 1; i >= 0; i--)
         if (v->dpb_lt[v->dpb_slot[i]] < 0)
         { rh264_dpb_unmark_at(v, i); break; }
      if (i < 0)   /* every held picture long-term: drop the tail */
         rh264_dpb_unmark_at(v, v->dpb_len - 1);
   }
   {
      /* any slot not currently listed; removals can leave holes */
      int used, j;
      slot = 0;
      for (i = 0; i < v->dpb_size; i++)
      {
         used = 0;
         for (j = 0; j < v->dpb_len; j++)
            if (v->dpb_slot[j] == i) { used = 1; break; }
         if (!used) { slot = i; break; }
      }
   }
   /* A frame picture is one block: the reference is the working
    * frame's samples, shared, and the working frame takes a fresh
    * block for the next picture (see rh264_video_finish_picture). A
    * field pair fills its block a field at a time and is copied. */
   if (v->cur_field)
      rh264_frame_copy_planes(&v->dpb[slot], &v->cur->f);
   else
      rh264_frame_share_planes(&v->dpb[slot], &v->cur->f);
   v->dpb_fields[slot] = (uint8_t)(v->cur_field ? (1 << (v->cur_field - 1))
                                                : 3);
   v->pair_slot = (v->cur_field && v->pair_open) ? slot : -1;
   if (v->cur_field)
      v->dpb_poc_fld[slot][v->cur_field - 1] = v->last_poc;
   else
      v->dpb_poc_fld[slot][0] = v->dpb_poc_fld[slot][1] = v->last_poc;
   v->dpb_pn[slot] = picnum;
   v->dpb_poc[slot] = v->last_poc;
   v->dpb[slot].poc = v->last_poc;
   if (!v->cur_field && v->cur->pic_mvg)
   {
      /* a frame picture's motion stands for both parities: the slot
       * takes the working grid itself, for both */
      rh264_frame_share_mvg(&v->dpb[slot], v->cur->pic_mvg);
   }
   else
   {
      /* a first field goes in the slot's own grid for its parity */
      rh264_mv *g;
      rh264_frame_own_mvg(&v->dpb[slot]);
      g = (v->cur_field == 2) ? v->dpb[slot].mvg2 : v->dpb[slot].mvg;
      if (g && v->cur->pic_mvg)
         memcpy(g, v->cur->pic_mvg,
               (size_t)(v->cur->f.mbw * 4) * (v->cur->f.mbh * 4) * sizeof(rh264_mv));
   }
   v->dpb_lt[slot] = (signed char)lt;
   for (i = v->dpb_len; i > 0; i--)
      v->dpb_slot[i] = v->dpb_slot[i-1];
   v->dpb_slot[0] = slot;
   v->dpb_len++;
   v->have_ref = 1;
}

/* Decode one non-IDR (type-1) slice. Only P-slices with CAVLC entropy coding
 * are currently supported; the previous decoded frame in v->ref is the single
 * reference. Returns 0 on success, negative otherwise (e.g. B-slice, CABAC P,
 * or no reference yet). */
/* Apply ref_pic_list_modification (8.2.4.3.1) over a built list. PicNums
 * compare through FrameNumWrap: frame_num wraps at MaxFrameNum, so a
 * buffered picture's PicNum is relative to the current one (8.2.4.1). The
 * list may end up naming one picture at several indices, which is how
 * weighted prediction offers a weighted and an unweighted version of it.
 * The list is one entry longer than nref while this runs. */
static void rh264_apply_list_mods(rh264_video *v, const rh264_frame **l,
      int *lpn, int nref, int currpn, int maxpn,
      const int8_t *mod_op, const int32_t *mod_val, int nmod)
{
   int pred = currpn, k, ridx = 0;
   for (k = 0; k < nmod && ridx < nref; k++)
   {
      int pn, is_lt = (mod_op[k] == 2);
      if (is_lt)
         pn = 0x10000 + mod_val[k];    /* LongTermPicNum (8.2.4.3.1) */
      else
      {
         if (mod_op[k] == 0)
         { pn = pred - (mod_val[k] + 1); if (pn < 0) pn += maxpn; }
         else
         { pn = pred + (mod_val[k] + 1); if (pn >= maxpn) pn -= maxpn; }
         pred = pn;
         if (pn > currpn) pn -= maxpn;
      }
      {
         const rh264_frame *pic = NULL;
         int j, nidx;
         for (j = 0; j < v->dpb_len; j++)
         {
            int s = v->dpb_slot[j];
            if (is_lt)
            {
               if (v->dpb_lt[s] == mod_val[k]) { pic = &v->dpb[s]; break; }
            }
            else if (v->dpb_lt[s] < 0)
            {
               int fn = v->dpb_pn[s];
               int fnw = (fn > currpn) ? (fn - maxpn) : fn;
               if (fnw == pn) { pic = &v->dpb[s]; break; }
            }
         }
         if (!pic) continue;             /* names a picture we lack */
         for (j = nref; j > ridx; j--)
         { l[j] = l[j-1]; lpn[j] = lpn[j-1]; }
         l[ridx] = pic; lpn[ridx] = pn;
         ridx++;
         nidx = ridx;
         for (j = ridx; j <= nref; j++)
            if (l[j] != pic)
            { l[nidx] = l[j]; lpn[nidx] = lpn[j]; nidx++; }
      }
   }
}

static int rh264_video_decode_inter(rh264_video *v, const uint8_t *nal, size_t len, int *got_pic)
{
   int nut, nri, rc, kind, end = 0;
   size_t rl;
   uint8_t *rbsp;
   rh264_bits b;
   rh264_slice_hdr *sh = &v->cur->sscr.sh;
   if (len < 1) return -1;
   /* A reference must exist before a P or B slice builds its lists -
    * checked there rather than here: with pictures in flight the
    * IDR before this picture enters the DPB when this picture opens,
    * which is after the header below, not before it. */
   nut = nal[0] & 0x1f;
   nri = (nal[0] >> 5) & 3;
   rbsp = rh264_unescape_scratch(v, nal + 1, len - 1, &rl);
   if (!rbsp) return -1;
   rh264_bits_init(&b, rbsp, rl);
   if (v->vlc_ready) b.vlc = &v->vlc;
   if (!rh264_parse_slice_header_adv(&b, nut, nri, &v->sps, &v->pps, sh))
   { if (sh->switching) v->cur->saw_switching = 1; return -1; }   /* unsupported slice type (e.g. B) */
   if (sh->slice_type != RH264_SLICE_P && sh->slice_type != RH264_SLICE_SP
         && sh->slice_type != RH264_SLICE_B
         && sh->slice_type != RH264_SLICE_I)
   { return -1; }
   kind = (sh->slice_type == RH264_SLICE_I) ? 2
        : (sh->slice_type == RH264_SLICE_B) ? 4 : 3;
   /* A droppable picture while catching up: nothing references a
    * picture with nal_ref_idc 0, frame_num and the POC state advance on
    * reference pictures only, so passing it over - every slice of it,
    * before any of its state is taken on - leaves the decoder exactly
    * as it was, whether its pictures decode one at a time or
    * concurrently; with the pool busy, the work not done is time
    * caught up. */
   if (sh->first_mb_in_slice == 0)
      v->skip_pic = 0;
   if (nri == 0 && (v->skip_pic || (sh->first_mb_in_slice == 0
            && retro_atomic_load_acquire_int(&v->skip_nonref))))
   {
      v->skip_pic = 1;
      v->dropped  = 1;
      return 0;
   }
   if (sh->first_mb_in_slice == 0)
   {
      int fld = sh->field_pic_flag ? (sh->bottom_field_flag ? 2 : 1) : 0;
      int second = fld && v->pair_open && v->pair_frame_num == sh->frame_num
            && v->cur_field && v->cur_field != fld;
      if (!second && rh264_video_next_ctx(v, got_pic) != 0) return -1;
      v->cur->pic_open = 0;
      v->cur->sps = v->sps;
      v->cur->pps = v->pps;
      v->last_poc = rh264_derive_poc(v, sh, nri);
      v->cur_field = fld;
      rh264_frame_set_field(&v->cur->f, fld);
      v->cur->f.mbaff = (!fld && v->sps.mb_adaptive_frame_field_flag) ? 1 : 0;
      if (fld && second) { if(v->last_poc<v->pair_poc) v->pair_poc=v->last_poc;
                           v->pair_open=0; }
      else if (fld) { v->pair_open=1; v->pair_frame_num=sh->frame_num;
                      v->pair_poc=v->last_poc; }
      else v->pair_open=0;
      v->cur->pic_open = 1; v->cur->pic_kind = kind; v->cur->pic_ref = nri;
      v->cur->pic_end = 0;  v->cur->pic_nslices = 0;
      v->cur->f.rows_deblocked = 0;
      v->cur->f.row_user       = v->cur;
      v->cur->f.row_done       = (v->cur->f.mbaff || v->cur->f.field) ? NULL : rh264_video_row_done;
      if (v->cur->f.planes)
         rh264_block_publish_rows(v->cur->f.planes, 0);
      v->cur->job_rc    = 0;
      v->cur->completed = 0;
      rh264_resolve_scaling(&v->sps, &v->pps, v->cur->f.w4, v->cur->f.w8);
   }
   else if (!v->cur->pic_open || v->cur->pic_kind != kind
         || !rh264_video_slice_continues(v, sh->first_mb_in_slice))
   { return -1; }   /* continuation without its picture, or a
                                 * mixed-type picture (unsupported) */
   if (rh264_video_note_slice(v, sh) != 0)
   { v->cur->pic_open = 0; return -1; }
   if (sh->slice_type == RH264_SLICE_I)
   {
      /* A non-IDR I picture, e.g. a recovery point: decoded like an IDR but
       * with normal inter-picture bookkeeping -- the reference buffer stays,
       * the counters keep running, and the picture is stored like any other
       * reference. */
      if (!rh264_ctx_queue_slice(v, v->cur, &b, sh, 0, v->pps.entropy_coding_mode_flag))
      { v->cur->pic_open = 0; return -1; }
      v->cur->vlc = v->vlc_ready ? &v->vlc : NULL;
      if (!v->threaded)
         rh264_ctx_run_slices(v, v->cur, v->cur->vlc);
      rc = v->cur->job_rc;
      if (rc != 0) v->cur->pic_open = 0;
      v->last_picnum = sh->frame_num_val;
      v->pend_n_mmco = sh->n_mmco;
      memcpy(v->pend_mmco_op, sh->mmco_op, sizeof(v->pend_mmco_op));
      memcpy(v->pend_mmco_a, sh->mmco_a, sizeof(v->pend_mmco_a));
      memcpy(v->pend_mmco_b, sh->mmco_b, sizeof(v->pend_mmco_b));
      return rc;
   }
   if (!v->have_ref && kind != 2) { v->cur->pic_open = 0; return -1; }
   if (v->dpb_len < 1)
   { return -1; }
   if (sh->slice_type == RH264_SLICE_B)
   {
      /* B slice: both lists ordered by POC around the current picture
       * (8.2.4.2.3), then per-list modification. list 1 starts as list 0
       * mirrored; when that leaves the two identical, its first two entries
       * swap. The first B slice of the stream raises the display delay to
       * what the VUI promises, or the reference count when it is silent. */
      rh264_bctx *bc = &v->cur->sscr.bc;
      int i, n = 0, maxpn = 1 << v->sps.log2_max_frame_num;
      int currpn = sh->frame_num_val;
      const rh264_frame **ordered = v->cur->sscr.ordered;
      int *opoc = v->cur->sscr.opoc, *opn = v->cur->sscr.opn;
      int *oslot = v->cur->sscr.oslot;      /* DPB slot behind ordered[]     */
      int *l0slot = v->cur->sscr.l0slot;    /* the two lists as slots        */
      int *l1slot = v->cur->sscr.l1slot;
      int *lpn0 = v->cur->sscr.lpn0, *lpn1 = v->cur->sscr.lpn1;
      int nref0 = sh->num_ref_idx_l0, nref1 = sh->num_ref_idx_l1;
      memset(bc, 0, sizeof(*bc));
      if (nref0 < 1)  nref0 = 1;
      if (nref0 > 32) nref0 = 32;
      if (nref1 < 1)  nref1 = 1;
      if (nref1 > 32) nref1 = 32;
      for (i = 0; i < v->dpb_len; i++)
      {
         int s = v->dpb_slot[i];
         if (v->dpb_lt[s] >= 0) continue;
         ordered[n] = &v->dpb[s];
         opoc[n] = v->dpb_poc[s];
         oslot[n] = s;
         opn[n] = v->dpb_pn[s]; n++;
      }
      /* selection-sort the held pictures by POC, ascending */
      for (i = 0; i < n; i++)
      {
         int j, m = i;
         for (j = i + 1; j < n; j++) if (opoc[j] < opoc[m]) m = j;
         if (m != i)
         {
            const rh264_frame *tf = ordered[i]; int tp = opoc[i], tn = opn[i];
            int ts = oslot[i];
            ordered[i] = ordered[m]; opoc[i] = opoc[m]; opn[i] = opn[m];
            oslot[i] = oslot[m];
            ordered[m] = tf; opoc[m] = tp; opn[m] = tn; oslot[m] = ts;
         }
      }
      /* list 0: below the current POC descending, then above ascending;
       * list 1 the reverse */
      bc->n0 = 0; bc->n1 = 0;
      for (i = n - 1; i >= 0; i--) if (opoc[i] < v->last_poc)
      { bc->l0[bc->n0] = ordered[i]; bc->l0poc[bc->n0] = opoc[i];
        l0slot[bc->n0] = oslot[i]; lpn0[bc->n0] = opn[i]; bc->n0++; }
      for (i = 0; i < n; i++) if (opoc[i] > v->last_poc)
      { bc->l0[bc->n0] = ordered[i]; bc->l0poc[bc->n0] = opoc[i];
        l0slot[bc->n0] = oslot[i]; lpn0[bc->n0] = opn[i]; bc->n0++; }
      for (i = 0; i < n; i++) if (opoc[i] > v->last_poc)
      { bc->l1[bc->n1] = ordered[i]; bc->l1poc[bc->n1] = opoc[i];
        l1slot[bc->n1] = oslot[i]; lpn1[bc->n1] = opn[i]; bc->n1++; }
      for (i = n - 1; i >= 0; i--) if (opoc[i] < v->last_poc)
      { bc->l1[bc->n1] = ordered[i]; bc->l1poc[bc->n1] = opoc[i];
        l1slot[bc->n1] = oslot[i]; lpn1[bc->n1] = opn[i]; bc->n1++; }
      /* long-term pictures close both lists, ascending index (8.2.4.2.4);
       * they take part in the identical-lists swap check below */
      { int idx;
        for (idx = 0; idx <= v->max_lt_idx; idx++)
           for (i = 0; i < v->dpb_len; i++)
           {
              int s = v->dpb_slot[i];
              if (v->dpb_lt[s] != idx) continue;
              bc->l0[bc->n0] = &v->dpb[s]; bc->l0poc[bc->n0] = v->dpb_poc[s];
              lpn0[bc->n0] = 0x10000 + idx; bc->n0++;
              bc->l1[bc->n1] = &v->dpb[s]; bc->l1poc[bc->n1] = v->dpb_poc[s];
              lpn1[bc->n1] = 0x10000 + idx; bc->n1++;
           }
      }
      if (sh->field_pic_flag)
      {
         /* the frame lists just built are refFrameList0/1 (8.2.4.2.4);
          * each becomes a field list by the parity alternation */
         int f0 = rh264_fields_from_slots(v, l0slot, bc->n0, bc->l0, lpn0,
               0, 32);
         int f1 = rh264_fields_from_slots(v, l1slot, bc->n1, bc->l1, lpn1,
               34, 32);
         for (i = 0; i < f0; i++) bc->l0poc[i] = bc->l0[i]->poc;
         for (i = 0; i < f1; i++) bc->l1poc[i] = bc->l1[i]->poc;
         bc->n0 = f0; bc->n1 = f1;
      }
      if (bc->n0 < 1 || bc->n1 < 1) { return -1; }
      if (bc->n0 == bc->n1 && bc->n1 > 1)
      {
         for (i = 0; i < bc->n0; i++) if (bc->l0[i] != bc->l1[i]) break;
         if (i == bc->n0)
         {
            const rh264_frame *tf = bc->l1[0]; int tp = bc->l1poc[0];
            int tn = lpn1[0];
            bc->l1[0] = bc->l1[1]; bc->l1poc[0] = bc->l1poc[1]; lpn1[0] = lpn1[1];
            bc->l1[1] = tf; bc->l1poc[1] = tp; lpn1[1] = tn;
         }
      }
      /* pad to the active counts, then modify */
      while (bc->n0 < nref0)
      { bc->l0[bc->n0] = bc->l0[bc->n0-1]; bc->l0poc[bc->n0] = bc->l0poc[bc->n0-1];
        lpn0[bc->n0] = lpn0[bc->n0-1]; bc->n0++; }
      while (bc->n1 < nref1)
      { bc->l1[bc->n1] = bc->l1[bc->n1-1]; bc->l1poc[bc->n1] = bc->l1poc[bc->n1-1];
        lpn1[bc->n1] = lpn1[bc->n1-1]; bc->n1++; }
      bc->n0 = nref0; bc->n1 = nref1;
      rh264_apply_list_mods(v, bc->l0, lpn0, bc->n0, currpn, maxpn,
            sh->mod_op, sh->mod_val, sh->nmod);
      rh264_apply_list_mods(v, bc->l1, lpn1, bc->n1, currpn, maxpn,
            sh->mod_op1, sh->mod_val1, sh->nmod1);
      /* picture ids and POCs follow whatever the modification placed */
      for (i = 0; i < bc->n0; i++)
      {
         int j; bc->pid0[i] = 0; bc->l0poc[i] = 0;
         for (j = 0; j < v->dpb_size; j++)
            if (bc->l0[i] == &v->dpb[j])
            { bc->pid0[i] = (signed char)j; bc->l0poc[i] = v->dpb_poc[j]; break; }
         for (j = 0; j < 68; j++)
            if (bc->l0[i] == &v->fieldview[j])
            { bc->pid0[i] = v->fieldview_id[j];
              bc->l0poc[i] = v->fieldview[j].poc; break; }
      }
      for (i = 0; i < bc->n1; i++)
      {
         int j; bc->pid1[i] = 0; bc->l1poc[i] = 0;
         for (j = 0; j < v->dpb_size; j++)
            if (bc->l1[i] == &v->dpb[j])
            { bc->pid1[i] = (signed char)j; bc->l1poc[i] = v->dpb_poc[j]; break; }
         for (j = 0; j < 68; j++)
            if (bc->l1[i] == &v->fieldview[j])
            { bc->pid1[i] = v->fieldview_id[j];
              bc->l1poc[i] = v->fieldview[j].poc; break; }
      }
      bc->currpoc = v->last_poc;
      bc->direct_spatial = sh->direct_spatial_mv_pred_flag;
      bc->d8x8 = v->sps.direct_8x8_inference_flag;
      bc->wbidc = v->pps.weighted_bipred_idc;
      rh264_ctx_snapshot_list(v->cur, bc->l0, bc->n0);
      rh264_ctx_snapshot_list(v->cur, bc->l1, bc->n1);
      bc->colg = bc->l1[0]->mvg;
      if (!bc->colg) { return -1; }
      rh264_b_setup_scales(bc);
      {
         rh264_slice_job *j = rh264_ctx_queue_slice(v, v->cur, &b, sh, 2,
               v->pps.entropy_coding_mode_flag);
         if (!j) { return -1; }
         j->bc = *bc;
      }
      v->cur->vlc = v->vlc_ready ? &v->vlc : NULL;
      if (!v->threaded)
         rh264_ctx_run_slices(v, v->cur, v->cur->vlc);
      rc = v->cur->job_rc;
      (void)end;
      if (rc != 0) v->cur->pic_open = 0;
      v->last_picnum = sh->frame_num_val;
      v->pend_n_mmco = sh->n_mmco;
      memcpy(v->pend_mmco_op, sh->mmco_op, sizeof(v->pend_mmco_op));
      memcpy(v->pend_mmco_a, sh->mmco_a, sizeof(v->pend_mmco_a));
      memcpy(v->pend_mmco_b, sh->mmco_b, sizeof(v->pend_mmco_b));
      return rc;
   }
   {
      /* Build reference picture list 0. The default order is by descending
       * PicNum (8.2.4.2.1), which is the order the buffer already keeps, then
       * ref_pic_list_modification moves entries to the front (8.2.4.3.1). The
       * list may name the same picture more than once, which is how weighted
       * prediction offers a weighted and an unweighted version of it, so it
       * can be longer than the number of pictures held. */
      const rh264_frame **l0 = v->cur->sscr.l0;
      int *lpn = v->cur->sscr.lpn;
      int *l0poc = v->cur->sscr.l0poc;
      signed char *picid = v->cur->sscr.picid;
      int i, n = 0, nref = sh->num_ref_idx_l0;
      int maxpn = 1 << v->sps.log2_max_frame_num;
      int currpn = sh->frame_num_val;
      if (nref < 1) nref = 1;
      if (nref > 32) nref = 32;
      if (sh->field_pic_flag)
         n = rh264_build_field_list(v, l0, lpn, 32);
      else
      for (i = 0; i < v->dpb_len; i++)
      {
         int s = v->dpb_slot[i];
         if (v->dpb_lt[s] >= 0) continue;
         l0[n] = &v->dpb[s]; lpn[n] = v->dpb_pn[s]; n++;
      }
      /* long-term pictures follow, ascending LongTermPicNum (8.2.4.2.1) */
      { int idx;
        for (idx = 0; idx <= v->max_lt_idx; idx++)
           for (i = 0; i < v->dpb_len; i++)
           {
              int s = v->dpb_slot[i];
              if (v->dpb_lt[s] == idx)
              { l0[n] = &v->dpb[s]; lpn[n] = 0x10000 + idx; n++; }
           }
      }
      if (n == 0) { return -1; }
      while (n < nref) { l0[n] = l0[n-1]; lpn[n] = lpn[n-1]; n++; }
      rh264_apply_list_mods(v, l0, lpn, nref, currpn, maxpn,
            sh->mod_op, sh->mod_val, sh->nmod);
      /* Map each list position onto the picture it names, so deblocking can
       * ask whether two blocks used the same picture rather than the same
       * index; list modification can place one picture at several indices. */
      for (i = 0; i < nref && i < 34; i++)
      {
         int j; picid[i] = 0; l0poc[i] = 0;
         for (j = 0; j < v->dpb_size; j++)
            if (l0[i] == &v->dpb[j])
            { picid[i] = (signed char)j; l0poc[i] = v->dpb_poc[j]; break; }
         /* a field view aliases a stored frame, so it never matches a
          * buffer slot above; take the identity AND the order count
          * recorded with it.  The count is what a later B picture's
          * temporal direct prediction matches its colocated block's
          * reference against, so leaving it zero makes that lookup
          * fail. */
         for (j = 0; j < 68; j++)
            if (l0[i] == &v->fieldview[j])
            { picid[i] = v->fieldview_id[j];
              l0poc[i] = v->fieldview[j].poc; break; }
      }
      (void)lpn;
      rh264_ctx_snapshot_list(v->cur, l0, nref);
      {
         rh264_slice_job *j = rh264_ctx_queue_slice(v, v->cur, &b, sh, 1,
               v->pps.entropy_coding_mode_flag);
         if (!j) { v->cur->pic_open = 0; return -1; }
         j->nref = nref;
         memcpy(j->l0, l0, sizeof(j->l0[0]) * nref);
         memcpy(j->picid, picid, sizeof(j->picid[0]) * nref);
         memcpy(j->l0poc, l0poc, sizeof(j->l0poc[0]) * nref);
      }
      v->cur->vlc = v->vlc_ready ? &v->vlc : NULL;
      if (!v->threaded)
         rh264_ctx_run_slices(v, v->cur, v->cur->vlc);
      rc = v->cur->job_rc;
   }
   (void)end;
   if (rc != 0) v->cur->pic_open = 0;
   v->last_picnum = sh->frame_num_val;
   v->pend_n_mmco = sh->n_mmco;
   memcpy(v->pend_mmco_op, sh->mmco_op, sizeof(v->pend_mmco_op));
   memcpy(v->pend_mmco_a, sh->mmco_a, sizeof(v->pend_mmco_a));
   memcpy(v->pend_mmco_b, sh->mmco_b, sizeof(v->pend_mmco_b));
   return rc;
}

/* Walk NAL units in either length-prefixed (AVCC) or Annex-B form and act on
 * SPS/PPS + the first coded slice (IDR or non-IDR). */
/* Complete the assembled picture: mark each macroblock with its slice, run
 * the loop filter with per-slice parameters, and do the reference/output
 * bookkeeping that depends on the whole picture existing. */

/* The slice decoders call this as each macroblock row completes (see
 * rh264_frame::row_done). Filters the row above the one just finished,
 * with the picture's first slice's parameters: the hook is only armed
 * while the picture has a single slice, and a row that completed
 * inside slice 0 has slice 0 above it, so the parameters are the ones
 * the whole-picture pass would use. A second slice disarms it and the
 * finish takes the rest. */
static void rh264_video_row_done(void *user, int mby)
{
   /* The context, not the decoder: on a pool thread the decoder's
    * current picture is a later one. */
   rh264_pic_ctx *c = (rh264_pic_ctx*)user;
   rh264_mv *g;
   /* pic_open is the sequence's flag and the sequence may have closed
    * this picture already - it closes it when the next one opens,
    * which on a pool thread is while this one still decodes. */
   if (!c || c->pic_nslices != 1 || mby < 1)
      return;
   if (c->f.rows_deblocked != mby - 1)
      return;                         /* a gap: leave it to the finish */
   g = c->slice_mvg ? c->slice_mvg : c->mvg;
   if (c->pic_kind <= 2)
      rh264_deblock(&c->f, c->pic_idc, c->pic_oA, c->pic_oB,
            mby - 1, mby);
   else
   {
      /* The boundary strengths read the motion vectors of the rows
       * being filtered and the row above them, in the grid the slice
       * writes; the finish reads pic_mvg, by then the same. */
      if (!g)
         return;
      rh264_deblock_pslice(&c->f, c->pic_idc, c->pic_oA, c->pic_oB,
            g, mby - 1, mby);
   }
   c->f.rows_deblocked = mby;
   /* The row's motion goes into the picture grid with it, so a later
    * picture's temporal direct finds the co-located cells final along
    * with the samples: an I picture's row all intra, as the finish
    * leaves the whole grid, and a P or B row folded from the working
    * grid unless the slice wrote the picture grid itself. */
   if (c->pic_kind <= 2)
   {
      if (c->pic_mvg)
         rh264_mvg_set_intra(c->pic_mvg + (size_t)(mby - 1) * 4 * c->f.mbw * 4,
               c->f.mbw * 4, 4);
   }
   else if (g != c->pic_mvg)
      rh264_ctx_fold_mvg(c, (mby - 1) * c->f.mbw, mby * c->f.mbw);
   /* Row mby-1 is filtered; filtering row mby will still touch its
    * last lines through their shared edge, so what is final is
    * everything above it. */
   if (c->f.planes)
      rh264_block_publish_rows(c->f.planes, mby - 1);
}

/* The picture's own completion: the slice map for the filter, the rows
 * the row hook did not filter, the intra marks in the motion grid, the
 * block's row count set to the whole picture, the references let go.
 * Nothing here touches the sequence, and nothing after the last slice
 * of the picture reads the sequence to do it: this is the part a pool
 * thread runs, on the picture's own context. */
static void rh264_ctx_complete_picture(rh264_pic_ctx *c)
{
   int total = c->f.mbw * c->f.mbh, s;
   for (s = 0; s < c->pic_nslices; s++)
   {
      int lo = c->pic_first[s];
      int hi = (s + 1 < c->pic_nslices) ? c->pic_first[s + 1] : c->pic_end;
      int mb;
      if (hi > total) hi = total;
      /* mbslice is read by raster position (deblocking walks the
       * picture, not the bitstream), while a slice covers a range of
       * ADDRESSES - and under pair scanning those differ. */
      for (mb = lo; mb < hi; mb++)
      {
         int mx, my;
         rh264_mb_pos(mb, c->f.mbw, c->f.mbaff, &mx, &my);
         c->f.mbslice[my * c->f.mbw + mx] = (uint8_t)s;
      }
   }
   /* The rows the slice decoders already filtered on the way through
    * stay filtered; the rest is done here, which is all of it for a
    * picture the row hook could not take (pair-scanned, or more than
    * one slice by the time a row completed). */
   if (c->pic_kind <= 2)
   {
      rh264_deblock(&c->f, c->pic_idc, c->pic_oA, c->pic_oB,
            c->f.rows_deblocked, c->f.mbh);
      if (c->pic_mvg)
         rh264_mvg_set_intra(c->pic_mvg, c->f.mbw * 4, c->f.mbh * 4);
   }
   else
      rh264_deblock_pslice(&c->f, c->pic_idc, c->pic_oA, c->pic_oB,
            c->pic_mvg, c->f.rows_deblocked, c->f.mbh);
   c->f.rows_deblocked = 0;
   c->f.row_done       = NULL;
   if (c->f.planes)
      rh264_block_publish_rows(c->f.planes, c->f.mbh);
   rh264_ctx_release_refs(c);
}

/* Queue one slice's data decode on the picture's context. Takes its
 * own copy of the RBSP; the lists point at the context's snapshots,
 * which outlive the queue. */
static rh264_slice_job *rh264_ctx_queue_slice(rh264_video *v, rh264_pic_ctx *c,
      const rh264_bits *b, const rh264_slice_hdr *sh, int kind, int cabac)
{
   uint8_t *spare     = NULL;
   size_t spare_cap   = 0;
   rh264_slice_job *j = rh264_job_take(v);
   if (j)
   {
      spare     = j->rbsp;
      spare_cap = j->rbsp_cap;
      memset(j, 0, sizeof(*j));
   }
   else if (!(j = (rh264_slice_job*)calloc(1, sizeof(*j))))
      return NULL;
   /* The slice was unescaped into the buffer of whichever context was
    * current at the time - the one before the rotation, when this
    * slice opened a picture - and the job takes that very buffer;
    * its owner takes the one the pooled job came back with. */
   {
      rh264_pic_ctx *o = NULL;
      int k;
      for (k = 0; k < RH264_MAX_CTX; k++)
         if (v->ctx[k].rbsp_scratch == b->buf)
         {
            o = &v->ctx[k];
            break;
         }
      if (!o)
      {
         /* not a context's buffer: copy, as a stranger's must be */
         size_t need = b->size ? b->size : 1;
         if (spare_cap < need)
         {
            free(spare);
            spare     = (uint8_t*)malloc(need);
            spare_cap = need;
         }
         if (!spare)
         {
            free(j);
            return NULL;
         }
         memcpy(spare, b->buf, b->size);
         j->rbsp     = spare;
         j->rbsp_cap = spare_cap;
      }
      else
      {
         j->rbsp     = o->rbsp_scratch;
         j->rbsp_cap = o->rbsp_scratch_cap;
         o->rbsp_scratch     = spare;
         o->rbsp_scratch_cap = spare_cap;
      }
   }
   j->rbsp_len = b->size;
   (void)c;
   j->bitpos   = b->bitpos;
   j->sh       = *sh;
   j->kind     = kind;
   j->cabac    = cabac;
   if (c->jobs_tail)
      c->jobs_tail->next = j;
   else
      c->jobs = j;
   c->jobs_tail = j;
   return j;
}

/* Drain the context's slice queue in order: each slice's data decode,
 * the fold of its motion into the picture grid, the end of the coded
 * area; and, once the slices cover the picture, its completion. This
 * is the picture's work and reads nothing of the sequence beyond the
 * inverse VLC tables, which are read-only for the decoder's life.
 * Runs on the submitting thread today; on a pool thread once pictures
 * decode concurrently. */
static void rh264_ctx_run_slices(rh264_video *v, rh264_pic_ctx *c, const struct rh264_vlc_set *vlc)
{
   int total = c->f.mbw * c->f.mbh;
   while (c->jobs)
   {
      rh264_slice_job *j = c->jobs;
      rh264_bits b;
      int end = 0, rc = -1;
      c->jobs = j->next;
      if (!c->jobs)
         c->jobs_tail = NULL;
      if (c->job_rc == 0 && j->sh.first_mb_in_slice != c->pic_end)
         c->job_rc = -1;               /* a gap between slices */
      if (c->job_rc == 0)
      {
         rh264_mv *g = c->mvg;
         /* a picture's first slice has no other slice's cells to hide,
          * so it decodes straight into the picture grid: nothing to
          * fold afterwards */
         if (j->kind != 0 && j->sh.first_mb_in_slice == 0 && c->pic_mvg)
            g = c->pic_mvg;
         c->slice_mvg = g;
         /* every cell "not decoded" (ref -2) before the slice: the
          * other slices' macroblocks then read as unavailable (6.4.8),
          * the decoded ones fill in in raster order, and a list a
          * partition does not use stays as the reset leaves it */
         if (j->kind != 0)
            rh264_mvg_reset(g, (size_t)(c->f.mbw * 4) * (c->f.mbh * 4));
         rh264_bits_init(&b, j->rbsp, j->rbsp_len);
         b.bitpos = j->bitpos;
         if (vlc) b.vlc = vlc;
         switch (j->kind)
         {
            case 0:
               rc = j->cabac
                  ? rh264_cabac_decode_islice(&b, &c->sps, &c->pps, &j->sh, &c->f, &end, &c->sscr.cb)
                  : rh264_decode_islice(&b, &c->sps, &c->pps, &j->sh, &c->f, &end);
               break;
            case 1:
               rc = j->cabac
                  ? rh264_cabac_decode_pslice(&b, &c->sps, &c->pps, &j->sh, &c->f,
                        j->l0, j->nref, j->picid, j->l0poc, g, &end, &c->sscr.cb)
                  : rh264_decode_pslice(&b, &c->sps, &c->pps, &j->sh, &c->f,
                        j->l0, j->nref, j->picid, j->l0poc, g, &end);
               break;
            default:
               rc = j->cabac
                  ? rh264_cabac_decode_bslice(&b, &c->sps, &c->pps, &j->sh, &c->f,
                        &j->bc, g, &end, &c->sscr.cb)
                  : rh264_decode_bslice(&b, &c->sps, &c->pps, &j->sh, &c->f, &j->bc,
                        g, &end);
               break;
         }
         if (rc == 0)
         {
            if (j->kind != 0 && g != c->pic_mvg)
            {
               /* the rows the row hook filtered went into the picture
                * grid with their filtering; the rest of the slice goes
                * in now */
               int from = j->sh.first_mb_in_slice;
               int done = c->f.rows_deblocked * c->f.mbw;
               rh264_ctx_fold_mvg(c, from > done ? from : done, end);
            }
            c->pic_end = end;
         }
         else
            c->job_rc = rc;
      }
      rh264_job_park(v, j);
   }
   /* Completion is the picture's own affair, not the sequence's: it
    * does not ask pic_open, which the sequence clears when the next
    * picture opens, while this one may still be on a pool thread. */
   if (c->job_rc == 0 && c->pic_end >= total && !c->completed)
   {
      rh264_ctx_complete_picture(c);
      c->completed = 1;
   }
   else if (!c->completed && c->f.planes)
   {
      /* A picture that refused, or came up short of its last slice:
       * nobody may wait on its rows, whichever thread ran it - a
       * picture predicting from it would wait for a row that never
       * comes. It is published whole; what it holds is what was
       * decoded, and the caller learns of the refusal. */
      rh264_block_publish_rows(c->f.planes, c->f.mbh);
   }
}


#ifdef HAVE_THREADS
static void rh264_ctx_job(void *arg)
{
   rh264_pic_ctx *c = (rh264_pic_ctx*)arg;
   {
      int now = retro_atomic_fetch_add_int(&rh264_jobs_running, 1) + 1, m;
      do
      {
         m = retro_atomic_load_acquire_int(&rh264_jobs_running_max);
         if (now <= m)
            break;
      } while (!retro_atomic_cas_int(&rh264_jobs_running_max, m, now));
   }
   rh264_ctx_run_slices(c->owner, c, c->vlc);
   retro_atomic_fetch_sub_int(&rh264_jobs_running, 1);
   retro_atomic_store_release_int(&c->busy, 0);
   retro_eventcount_notify(&rh264_rows_ec);
}

/* Wait for the context's picture, if a pool thread still has it. The
 * submitting thread would sit idle for it; it takes queued pictures
 * from the pool instead while there are any - the one it waits for,
 * or one before it that the workers have not reached - and sleeps
 * only once the queue is empty and the picture is in other hands. */
static void rh264_ctx_join(rh264_video *v, rh264_pic_ctx *c)
{
   if (!rh264_rows_ec_ok)
      return;
   while (retro_atomic_load_acquire_int(&c->busy))
   {
      int key;
      if (v->pool && tpool_help((tpool_t*)v->pool))
         continue;
      key = retro_eventcount_prepare_wait(&rh264_rows_ec);
      if (retro_atomic_load_acquire_int(&c->busy))
         retro_eventcount_commit_wait(&rh264_rows_ec, key);
      else
         retro_eventcount_cancel_wait(&rh264_rows_ec);
   }
}
#endif

/* Hand the current picture's queued slices to the pool, or run them
 * here: a field picture stays on this thread for its pair, and so does
 * an intra picture, whose grid is marked at completion. Nothing is
 * queued for the picture after this. */
static void rh264_video_post_picture(rh264_video *v)
{
   rh264_pic_ctx *c = v->cur;
   if (!c->pic_open || c->posted)
      return;
   c->posted = 1;
#ifdef HAVE_THREADS
   if (v->threaded && !v->cur_field && c->pic_kind > 2)
   {
      retro_atomic_store_release_int(&c->busy, 1);
      if (tpool_add_work((tpool_t*)v->pool, rh264_ctx_job, c))
      {
         int k, busy = 0;
         for (k = 0; k < v->nctx; k++)
            busy += retro_atomic_load_acquire_int(&v->ctx[k].busy);
         v->st_posted++;
         v->st_inflight_sum += busy;
         if (busy >= v->nctx - 1)
            v->st_inflight_max++;
         return;
      }
      retro_atomic_store_release_int(&c->busy, 0);
   }
#endif
   rh264_ctx_run_slices(c->owner, c, c->vlc);
}

/* The sequence's side of a finished picture: an IDR empties the DPB,
 * reference marking runs, the picture enters the DPB as a reference
 * and the output queue as a picture to show. Runs in decode order on
 * the thread that submits, after the picture's own completion for now
 * and before it once pictures decode concurrently - at which point the
 * picture enters both with its rows still counting up. */
static void rh264_video_finish_picture(rh264_video *v, int *got_pic)
{
   if (!v->cur->pic_open) return;
   if (!v->threaded && !v->cur->completed)
   {
      rh264_ctx_complete_picture(v->cur);
      v->cur->completed = 1;
   }
   if (v->cur->pic_kind == 1)
   {
      v->dpb_len = 0;       /* an IDR empties the reference list (8.2.5.1) */
      /* long_term_reference_flag: the IDR itself becomes the long-term
       * picture with index 0 and bounds the index space (8.2.5.1) */
      v->max_lt_idx = v->pend_idr_ltr ? 0 : -1;
      rh264_dpb_insert(v, 0, v->pend_idr_ltr ? 0 : -1);
      if (!(v->cur_field && v->pair_open)
            && rh264_out_push(v, v->cur_field?v->pair_poc:v->last_poc, 1) >= 0)
         *got_pic = 1;
   }
   else
   {
      int epoch = 0;
      if (v->cur->pic_ref)   /* nal_ref_idc: only reference pictures stored */
      {
         int cur_lt = rh264_dpb_marking(v, v->last_picnum, &epoch);
         if (epoch)
         {
            /* op 5 restarts the sequence numbering: the current picture
             * behaves as frame 0 with POC 0 in a fresh output epoch
             * (8.2.1, 8.2.5.4.5) */
            v->idr_gen++;
            v->last_poc = 0;
            v->prev_poc_msb = 0; v->prev_poc_lsb = 0;
            v->prev_frame_num = 0; v->fn_offset = 0;
            v->last_picnum = 0;
            v->cur->f.poc = 0;
         }
         rh264_dpb_insert(v, epoch ? 0 : v->last_picnum, cur_lt);
      }
      if (!(v->cur_field && v->pair_open)
            && rh264_out_push(v, v->cur_field?v->pair_poc:v->last_poc, epoch) >= 0)
         *got_pic = 1;
   }
   /* The picture's samples now belong to the DPB and the output queue
    * as well; the next picture must not write over them. A frame
    * picture leaves the working frame holding a shared block, and the
    * write into the next one takes a fresh block on its way in, in
    * rh264_frame_alloc_if_needed. */
   v->cur->pic_open = 0;
}

/* ==================== hardware path ==================== */

/* The pictures decode on a backend (a GPU video API); this side keeps
 * what the stream says about them - which are held for reference,
 * under which numbers, and the order they are shown in - in the same
 * DPB lists and with the same marking the software path uses, so the
 * two put out the same pictures in the same order. Frame pictures
 * only: the backend is opened for progressive streams alone. A
 * picture is complete at the end of the access unit that carries it. */

#define RH264_HW_MAX_SLOTS 32
/* A slice header ends long before this; the rest of the slice is the
 * backend's to read. */
#define RH264_HW_HDR_BYTES 4096

typedef struct rh264_hw
{
   const rh264_hw_ops_t *ops;
   void *dec;
   rh264_hw_sps_t sps;
   rh264_hw_pps_t pps;
   unsigned params_gen;
   unsigned nslots;
   int broken;                 /* a decode or the stream failed     */
   /* the picture being gathered */
   uint8_t *bs;
   size_t bs_len, bs_cap;
   uint32_t offs[RH264_MAX_SLICES];
   unsigned nslices;
   int pic_open, skip, slot;
   int frame_num, idr_pic_id, idr, ref, intra;
   int poc[2];
   /* the backend slot and field order counts of each DPB entry */
   int dpb_hw[RH264_MAX_REFS];
   int dpb_poc2[RH264_MAX_REFS][2];
   /* pictures waiting to be shown */
   int out_slot[RH264_OUT_SLOTS];
   int out_poc[RH264_OUT_SLOTS];
   int out_gen[RH264_OUT_SLOTS];
   uint8_t out_used[RH264_OUT_SLOTS];
   int out_len;
   rh264_hw_ref_t refs[RH264_MAX_REFS];
   video_hw_frame_t shown;
   int have_shown;
   int fresh;                  /* shown during the present call     */
} rh264_hw;

/* The backend decoders try; one pointer, published whole. */
#ifdef RETRO_ATOMIC_HAS_PTR
static retro_atomic_ptr_t rh264_hw_backend;
#define RH264_HW_BACKEND_GET() \
   ((const rh264_hw_ops_t*)retro_atomic_load_acquire_ptr(&rh264_hw_backend))
#else
static const rh264_hw_ops_t *rh264_hw_backend;
#define RH264_HW_BACKEND_GET() (rh264_hw_backend)
#endif

void rh264_set_hw_backend(const rh264_hw_ops_t *ops)
{
#ifdef RETRO_ATOMIC_HAS_PTR
   retro_atomic_store_release_ptr(&rh264_hw_backend, (void*)ops);
#else
   rh264_hw_backend = ops;
#endif
}

/* Coded order of the lists, from the raster order they are kept in. */
static void rh264_hw_scaling_from(rh264_hw_scaling_t *o, int nlists,
      const uint8_t *present, const uint8_t *usedef,
      const uint8_t l4[6][16], const uint8_t l8[6][64])
{
   int i, j;
   memset(o, 0, sizeof(*o));
   for (i = 0; i < nlists && i < 12; i++)
   {
      if (present[i])
         o->present_mask |= (uint16_t)(1u << i);
      if (usedef[i])
         o->default_mask |= (uint16_t)(1u << i);
   }
   for (i = 0; i < 6; i++)
      for (j = 0; j < 16; j++)
         o->list4x4[i][j] = l4[i][rh264_zigzag4[j]];
   for (i = 0; i < 6; i++)
      for (j = 0; j < 64; j++)
         o->list8x8[i][j] = l8[i][rh264_zigzag8[j]];
}

static void rh264_hw_fill_sps(rh264_hw_sps_t *o, const rh264_sps *s)
{
   memset(o, 0, sizeof(*o));
   o->sps_id                         = s->sps_id;
   o->profile_idc                    = s->profile_idc;
   o->constraint_flags               = s->constraint_flags;
   o->level_idc                      = s->level_idc;
   o->chroma_format_idc              = s->chroma_format_idc;
   o->bit_depth                      = s->bit_depth_luma;
   o->log2_max_frame_num             = s->log2_max_frame_num;
   o->poc_type                       = s->pic_order_cnt_type;
   o->log2_max_poc_lsb               = s->log2_max_poc_lsb;
   o->delta_pic_order_always_zero    = s->poc_type1_always_zero;
   o->offset_for_non_ref_pic         = s->poc1_offset_non_ref;
   o->offset_for_top_to_bottom_field = s->poc1_offset_ttb;
   o->num_ref_frames_in_poc_cycle    = s->poc1_ncycle;
   o->offset_for_ref_frame           = s->poc1_offset_ref;
   o->max_num_ref_frames             = s->max_num_ref_frames;
   o->gaps_allowed                   = s->gaps_allowed;
   o->width_in_mbs                   = s->pic_width_in_mbs;
   o->height_in_mbs                  = s->frame_mbs;
   o->direct_8x8_inference           = s->direct_8x8_inference_flag;
   o->transform_bypass               = s->tb;
   o->frame_cropping                 = s->frame_cropping;
   o->crop_left                      = s->crop_l;
   o->crop_right                     = s->crop_r;
   o->crop_top                       = s->crop_t;
   o->crop_bottom                    = s->crop_b;
   o->width                          = s->frame_width;
   o->height                         = s->frame_height;
   o->crop_x                         = s->crop_x;
   o->crop_y                         = s->crop_y;
   o->scaling_present                = s->scaling_present;
   rh264_hw_scaling_from(&o->scaling, s->chroma_format_idc == 3 ? 12 : 8,
         s->sl_present, s->sl_usedef, s->sl4, s->sl8);
}

static void rh264_hw_fill_pps(rh264_hw_pps_t *o, const rh264_pps *p)
{
   memset(o, 0, sizeof(*o));
   o->pps_id                        = p->pps_id;
   o->sps_id                        = p->sps_id;
   o->entropy_coding_mode           = p->entropy_coding_mode_flag;
   o->bottom_field_pic_order_in_frame_present = p->pic_order_present_flag;
   o->num_ref_idx_l0_default        = p->num_ref_idx_l0_default;
   o->num_ref_idx_l1_default        = p->num_ref_idx_l1_default;
   o->weighted_pred                 = p->weighted_pred_flag;
   o->weighted_bipred_idc           = p->weighted_bipred_idc;
   o->pic_init_qp                   = p->pic_init_qp;
   o->pic_init_qs                   = p->pic_init_qs;
   o->chroma_qp_index_offset        = p->chroma_qp_index_offset;
   o->second_chroma_qp_index_offset = p->chroma_qp_index_offset2;
   o->deblocking_filter_control_present = p->deblocking_filter_control_present;
   o->constrained_intra_pred        = p->constrained_intra_pred_flag;
   o->transform_8x8_mode            = p->transform_8x8_mode;
   o->scaling_present               = p->scaling_present;
   rh264_hw_scaling_from(&o->scaling, 12, p->sl_present, p->sl_usedef,
         p->sl4, p->sl8);
}

/* Whether the backend could take a stream of these parameter sets:
 * the profiles GPUs decode, as frames, 8-bit 4:2:0. */
static int rh264_hw_stream_ok(const rh264_sps *s)
{
   if (     s->profile_idc != 66 && s->profile_idc != 77
         && s->profile_idc != 100)
      return 0;
   return s->frame_mbs_only_flag && s->chroma_format_idc == 1
       && s->bit_depth_luma == 8 && s->bit_depth_chroma == 8 && !s->tb;
}

/* A slot no picture holds: neither a reference nor waiting to be
 * shown. -1 when every one is held. */
static int rh264_hw_slot_take(const rh264_video *v)
{
   const rh264_hw *hw = v->hw;
   unsigned s;
   int j;
   for (s = 0; s < hw->nslots; s++)
   {
      int held = 0;
      for (j = 0; j < v->dpb_len && !held; j++)
         if (hw->dpb_hw[v->dpb_slot[j]] == (int)s)
            held = 1;
      for (j = 0; j < RH264_OUT_SLOTS && !held; j++)
         if (hw->out_used[j] && hw->out_slot[j] == (int)s)
            held = 1;
      if (!held)
         return (int)s;
   }
   return -1;
}

/* rh264_dpb_insert for a frame on the backend. */
static void rh264_hw_dpb_insert(rh264_video *v, int picnum, int lt)
{
   rh264_hw *hw = v->hw;
   int i, slot = 0;
   if (v->dpb_size <= 0)
      return;
   if (v->dpb_len >= v->dpb_size)
   {
      for (i = v->dpb_len - 1; i >= 0; i--)
         if (v->dpb_lt[v->dpb_slot[i]] < 0)
         { rh264_dpb_unmark_at(v, i); break; }
      if (i < 0)
         rh264_dpb_unmark_at(v, v->dpb_len - 1);
   }
   for (i = 0; i < v->dpb_size; i++)
   {
      int j, used = 0;
      for (j = 0; j < v->dpb_len; j++)
         if (v->dpb_slot[j] == i) { used = 1; break; }
      if (!used) { slot = i; break; }
   }
   hw->dpb_hw[slot]      = hw->slot;
   hw->dpb_poc2[slot][0] = hw->poc[0];
   hw->dpb_poc2[slot][1] = hw->poc[1];
   v->dpb_fields[slot]   = 3;
   v->dpb_pn[slot]       = picnum;
   v->dpb_poc[slot]      = v->last_poc;
   v->dpb_lt[slot]       = (signed char)lt;
   for (i = v->dpb_len; i > 0; i--)
      v->dpb_slot[i] = v->dpb_slot[i - 1];
   v->dpb_slot[0] = slot;
   v->dpb_len++;
   v->have_ref = 1;
}

/* The next picture out, lowest (generation, POC) first; -1 none. */
static int rh264_hw_out_pick(const rh264_hw *hw)
{
   int i, bi = -1;
   for (i = 0; i < RH264_OUT_SLOTS; i++)
   {
      if (!hw->out_used[i])
         continue;
      if (     bi < 0 || hw->out_gen[i] < hw->out_gen[bi]
            || (     hw->out_gen[i] == hw->out_gen[bi]
                  && hw->out_poc[i] < hw->out_poc[bi]))
         bi = i;
   }
   return bi;
}

/* Hands the picture at queue entry @i to the caller. */
static int rh264_hw_show(rh264_video *v, int i)
{
   rh264_hw *hw     = v->hw;
   hw->out_used[i]  = 0;
   hw->out_len--;
   /* two pictures in one call: the first is passed over */
   if (hw->fresh)
      video_hw_frame_abandon(&hw->shown);
   hw->have_shown   = 0;
   if (hw->ops->output(hw->dec, (unsigned)hw->out_slot[i], &hw->shown) != 0)
   {
      hw->broken = 1;
      return -1;
   }
   hw->have_shown = 1;
   hw->fresh      = 1;
   return 0;
}

/* rh264_out_push for a picture on the backend: 1 when one was shown. */
static int rh264_hw_out_push(rh264_video *v, int poc, int is_idr)
{
   rh264_hw *hw = v->hw;
   int i, slot = -1;
   for (i = 0; i < RH264_OUT_SLOTS; i++)
      if (!hw->out_used[i]) { slot = i; break; }
   if (slot < 0)
      return -1;
   hw->out_slot[slot] = hw->slot;
   hw->out_poc[slot]  = poc;
   hw->out_gen[slot]  = v->idr_gen;
   hw->out_used[slot] = 1;
   hw->out_len++;
   if (hw->out_len <= v->reorder_delay && !(is_idr && hw->out_len == 1))
      return 0;
   if (rh264_hw_show(v, rh264_hw_out_pick(hw)) != 0)
      return -1;
   return 1;
}

/* The gathered picture to the backend, then into the DPB and the
 * display queue as rh264_video_finish_picture puts a frame. */
static int rh264_hw_finish(rh264_video *v, int *got_pic)
{
   rh264_hw *hw = v->hw;
   rh264_hw_pic_t pic;
   int j, r;
   unsigned n = 0;

   if (!hw->pic_open)
      return 0;
   hw->pic_open = 0;
   if (hw->skip)
      return 0;

   /* an IDR predicts from nothing: the DPB empties under it */
   for (j = 0; j < v->dpb_len && !hw->idr; j++)
   {
      int s                = v->dpb_slot[j];
      hw->refs[n].slot      = hw->dpb_hw[s];
      hw->refs[n].long_term = v->dpb_lt[s] >= 0;
      hw->refs[n].frame_num = v->dpb_lt[s] >= 0 ? v->dpb_lt[s] : v->dpb_pn[s];
      hw->refs[n].poc[0]    = hw->dpb_poc2[s][0];
      hw->refs[n].poc[1]    = hw->dpb_poc2[s][1];
      n++;
   }
   pic.sps           = &hw->sps;
   pic.pps           = &hw->pps;
   pic.params_gen    = hw->params_gen;
   pic.data          = hw->bs;
   pic.size          = hw->bs_len;
   pic.slice_offsets = hw->offs;
   pic.num_slices    = hw->nslices;
   pic.slot          = hw->slot;
   pic.frame_num     = hw->frame_num;
   pic.idr_pic_id    = hw->idr_pic_id;
   pic.poc[0]        = hw->poc[0];
   pic.poc[1]        = hw->poc[1];
   pic.idr           = hw->idr;
   pic.reference     = hw->ref;
   pic.intra         = hw->intra;
   pic.refs          = hw->refs;
   pic.num_refs      = n;
   pic.long_term     = 0;
   pic.long_term_idx = 0;
   if (hw->idr)
      pic.long_term  = v->pend_idr_ltr ? 1 : 0;
   else if (hw->ref)
      for (j = 0; j < v->pend_n_mmco; j++)
         if (v->pend_mmco_op[j] == 6)
         {
            pic.long_term     = 1;
            pic.long_term_idx = v->pend_mmco_a[j];
         }
   if (hw->ops->decode(hw->dec, &pic) != 0)
   {
      hw->broken = 1;
      return -1;
   }

   if (hw->idr)
   {
      v->dpb_len    = 0;
      v->max_lt_idx = v->pend_idr_ltr ? 0 : -1;
      rh264_hw_dpb_insert(v, 0, v->pend_idr_ltr ? 0 : -1);
      r = rh264_hw_out_push(v, v->last_poc, 1);
   }
   else
   {
      int epoch = 0;
      if (hw->ref)
      {
         int cur_lt = rh264_dpb_marking(v, v->last_picnum, &epoch);
         if (epoch)
         {
            /* memory_management_control_operation 5: the picture is
             * frame 0 of a fresh epoch, its order counts made relative
             * to the smaller (8.2.1) */
            int m = hw->poc[0] < hw->poc[1] ? hw->poc[0] : hw->poc[1];
            hw->poc[0]       -= m;
            hw->poc[1]       -= m;
            v->idr_gen++;
            v->last_poc       = 0;
            v->prev_poc_msb   = 0;
            v->prev_poc_lsb   = 0;
            v->prev_frame_num = 0;
            v->fn_offset      = 0;
            v->last_picnum    = 0;
         }
         rh264_hw_dpb_insert(v, epoch ? 0 : v->last_picnum, cur_lt);
      }
      r = rh264_hw_out_push(v, v->last_poc, epoch);
   }
   if (r < 0)
      return -1;
   if (r > 0)
      *got_pic = 1;
   return 0;
}

/* One slice NAL of a stream on the backend: its header read for the
 * sequencing, its bytes gathered for the backend. */
static int rh264_hw_slice(rh264_video *v, const uint8_t *nal, size_t len,
      int *got_pic)
{
   rh264_hw *hw = v->hw;
   rh264_slice_hdr *sh = &v->cur->sscr.sh;
   rh264_bits b;
   uint8_t *rbsp;
   size_t rl, need;
   int nut, nri;

   if (len < 2)
      return -1;
   nut  = nal[0] & 0x1f;
   nri  = (nal[0] >> 5) & 3;
   rbsp = rh264_unescape_scratch(v, nal + 1,
         len - 1 > RH264_HW_HDR_BYTES ? RH264_HW_HDR_BYTES : len - 1, &rl);
   if (!rbsp)
      return -1;
   rh264_bits_init(&b, rbsp, rl);
   if (!rh264_parse_slice_header_adv(&b, nut, nri, &v->sps, &v->pps, sh))
      return -1;
   if (sh->field_pic_flag)
      return -1;

   if (sh->first_mb_in_slice == 0)
   {
      /* a picture the last access unit left open ends here */
      if (rh264_hw_finish(v, got_pic) != 0)
         return -1;
      hw->skip = 0;
      if (nri == 0 && retro_atomic_load_acquire_int(&v->skip_nonref))
      {
         /* as rh264_video_decode_inter passes such a picture over */
         hw->skip     = 1;
         hw->pic_open = 1;
         v->dropped   = 1;
         return 0;
      }
      if (!sh->is_idr && !v->have_ref && sh->slice_type != RH264_SLICE_I)
         return -1;
      if (sh->is_idr)
         v->idr_gen++;
      v->last_poc     = rh264_derive_poc(v, sh, nri);
      hw->poc[0]      = v->poc_top;
      hw->poc[1]      = v->poc_bot;
      hw->idr         = sh->is_idr;
      hw->ref         = nri != 0;
      hw->intra       = sh->slice_type == RH264_SLICE_I;
      hw->frame_num   = sh->frame_num;
      hw->idr_pic_id  = sh->idr_pic_id;
      if (sh->is_idr)
         v->pend_idr_ltr = sh->idr_ltr;
      else
      {
         v->last_picnum = sh->frame_num_val;
         v->pend_n_mmco = sh->n_mmco;
         memcpy(v->pend_mmco_op, sh->mmco_op, sizeof(v->pend_mmco_op));
         memcpy(v->pend_mmco_a, sh->mmco_a, sizeof(v->pend_mmco_a));
         memcpy(v->pend_mmco_b, sh->mmco_b, sizeof(v->pend_mmco_b));
      }
      if ((hw->slot = rh264_hw_slot_take(v)) < 0)
         return -1;
      hw->bs_len   = 0;
      hw->nslices  = 0;
      hw->pic_open = 1;
   }
   else if (!hw->pic_open)
      return -1;
   else if (hw->skip)
      return 0;
   else if (sh->slice_type != RH264_SLICE_I)
      hw->intra = 0;

   if (hw->nslices >= RH264_MAX_SLICES)
      return -1;
   need = hw->bs_len + 3 + len;
   if (need > hw->bs_cap)
   {
      size_t cap = hw->bs_cap ? hw->bs_cap : 65536;
      uint8_t *nb;
      while (cap < need)
         cap *= 2;
      if (!(nb = (uint8_t*)realloc(hw->bs, cap)))
         return -1;
      hw->bs     = nb;
      hw->bs_cap = cap;
   }
   hw->offs[hw->nslices++] = (uint32_t)hw->bs_len;
   hw->bs[hw->bs_len++]    = 0;
   hw->bs[hw->bs_len++]    = 0;
   hw->bs[hw->bs_len++]    = 1;
   memcpy(hw->bs + hw->bs_len, nal, len);
   hw->bs_len += len;
   return 0;
}

/* A parameter set on a stream on the backend: the same geometry or
 * the stream is given up. */
static int rh264_hw_take_ps(rh264_video *v, const uint8_t *nal, size_t len)
{
   rh264_hw *hw = v->hw;
   rh264_hw_sps_t sps;
   rh264_hw_pps_t pps;
   rh264_video_take_ps(v, nal, len);
   if (!v->have_sps || !v->have_pps)
      return 0;
   if (!rh264_hw_stream_ok(&v->sps))
      return -1;
   rh264_hw_fill_sps(&sps, &v->sps);
   rh264_hw_fill_pps(&pps, &v->pps);
   if (     sps.width_in_mbs       != hw->sps.width_in_mbs
         || sps.height_in_mbs      != hw->sps.height_in_mbs
         || sps.max_num_ref_frames != hw->sps.max_num_ref_frames
         || sps.width  != hw->sps.width  || sps.height != hw->sps.height
         || sps.crop_x != hw->sps.crop_x || sps.crop_y != hw->sps.crop_y)
      return -1;
   if (     memcmp(&sps, &hw->sps, sizeof(sps))
         || memcmp(&pps, &hw->pps, sizeof(pps)))
   {
      hw->sps = sps;
      hw->pps = pps;
      hw->params_gen++;
   }
   return 0;
}

static void rh264_hw_free(rh264_video *v)
{
   rh264_hw *hw = v->hw;
   if (!hw)
      return;
   if (hw->dec)
      hw->ops->close(hw->dec);
   free(hw->bs);
   free(hw);
   v->hw = NULL;
}

int rh264_video_set_hw(rh264_video *v, int enable)
{
   const rh264_hw_ops_t *ops;
   rh264_hw *hw;
   int d, n, slots;

   if (!v)
      return 0;
   if (!enable)
      return v->hw ? 1 : 0;
   if (v->hw)
      return 1;
   ops = RH264_HW_BACKEND_GET();
   /* before the first picture, of a stream the backend could take */
   if (     !ops || !v->have_sps || !v->have_pps || v->alloc_w
         || v->cur->pic_open || !rh264_hw_stream_ok(&v->sps))
      return 0;
   if (!(hw = (rh264_hw*)calloc(1, sizeof(*hw))))
      return 0;
   hw->ops = ops;
   rh264_hw_fill_sps(&hw->sps, &v->sps);
   rh264_hw_fill_pps(&hw->pps, &v->pps);

   /* the sequencing rh264_frame_alloc_if_needed would set up */
   n = v->sps.max_num_ref_frames;
   if (n < 1) n = 1;
   if (n > RH264_MAX_REFS) n = RH264_MAX_REFS;
   if (v->sps.vui_num_reorder >= 0) d = v->sps.vui_num_reorder;
   else if (v->sps.profile_idc == 66) d = 0;
   else d = v->sps.max_num_ref_frames;
   if (d > RH264_MAX_REFS) d = RH264_MAX_REFS;
   if (d < 0) d = 0;
   /* every reference, every picture waiting to be shown and the one
    * decoding */
   slots = n + d + 1;
   if (slots > RH264_HW_MAX_SLOTS)
   {
      free(hw);
      return 0;
   }
   hw->nslots = (unsigned)slots;
   if (!(hw->dec = ops->open(ops->user, &hw->sps, hw->nslots)))
   {
      free(hw);
      return 0;
   }
   v->dpb_size      = n;
   v->dpb_len       = 0;
   v->reorder_delay = d;
   v->max_lt_idx    = -1;
   v->have_ref      = 0;
   v->out_len       = 0;
   v->out_show      = -1;
   /* the pictures are the backend's: nothing decodes on a pool */
#ifdef HAVE_THREADS
   v->pool          = NULL;
#endif
   v->threaded      = 0;
   v->hw            = hw;
   return 1;
}

int rh264_video_is_hw(const rh264_video *v)
{
   return (v && v->hw) ? 1 : 0;
}

int rh264_video_hw_frame(const rh264_video *v, video_hw_frame_t *out)
{
   if (!v || !v->hw || !v->hw->have_shown || !out)
      return -1;
   *out = v->hw->shown;
   return 0;
}

static int rh264_video_handle_slice_nal(rh264_video *v, const uint8_t *nal,
      size_t nl, int *got_pic)
{
   int type = nal[0] & 0x1f;
   if (v->hw)
   {
      if (v->hw->broken)
         return -1;
      if (type == 7 || type == 8)
         return rh264_hw_take_ps(v, nal, nl) != 0 ? -1 : 0;
      if (type == 5 || type == 1)
         return rh264_hw_slice(v, nal, nl, got_pic) != 0 ? -1 : 0;
      return 0;
   }
   /* a switching slice was refused earlier: everything after it would
    * be predicted from a picture that was never reconstructed */
   if (v->cur->saw_switching) return -1;
   if (type == 7 || type == 8) { rh264_video_take_ps(v, nal, nl); return 0; }
   if (type == 5 || type == 1)
   {
      if (!v->have_sps || !v->have_pps) return -1;
      if (rh264_frame_alloc_if_needed(v) != 0) return -1;
      if (type == 5)
      { if (rh264_video_decode_idr(v, nal, nl, got_pic) != 0) return -1; }
      else
      { if (rh264_video_decode_inter(v, nal, nl, got_pic) != 0) return -1; }
      /* a picture completes when its slices cover every macroblock; until
       * then further slice NAL units of the same picture are expected */
      if (v->threaded)
         return *got_pic ? 1 : 0;   /* the open of this picture may have shown one */
      if (v->cur->pic_open && v->cur->pic_end >= v->cur->f.mbw * v->cur->f.mbh)
      {
         rh264_video_finish_picture(v, got_pic);
         return 1;   /* one coded picture per call */
      }
      return 0;
   }
   return 0;
}

int rh264_video_decode(rh264_video *v, const uint8_t *data, size_t len)
{
   size_t p = 0;
   int got_pic = 0;
   if (!v || !data) return -1;
   v->dropped = 0;
   if (v->hw)
      v->hw->fresh = 0;

   if (v->nal_length_size > 0 && len >= (size_t)v->nal_length_size)
   {
      /* Length-prefixed AVCC: extradata was provided, so this is the
       * sample format by definition. Never sniff for start codes here: a
       * NAL of 256..511 bytes has the 4-byte length 00 00 01 xx, which is
       * indistinguishable from an Annex-B start code, and misreading such
       * a sample as Annex-B lets arbitrary payload bytes parse as
       * parameter sets. */
      int ls = v->nal_length_size, i;
      while (p + ls <= len)
      {
         size_t nl = 0;
         for (i = 0; i < ls; i++) nl = (nl << 8) | data[p + i];
         p += ls;
         if (nl == 0 || p + nl > len) break;
         {
            int r = rh264_video_handle_slice_nal(v, data + p, nl, &got_pic);
            if (r < 0) return -1;
            /* One coded picture per call: sequentially the picture
             * shown is this sample's, complete at its last slice, and
             * the rest of the sample is nothing. With pictures in
             * flight the one shown at a slice's open is an earlier
             * picture's, and this sample's remaining slices still
             * have to be queued: the call ends with the sample. */
            if (r == 1 && !v->threaded) break;
         }
         p += nl;
      }
   }
   else
   {
      /* Annex-B: scan for 00 00 01 start codes */
      while (p + 3 <= len)
      {
         size_t s, e;
         if (!(data[p] == 0 && data[p+1] == 0 && data[p+2] == 1))
         { p++; continue; }
         s = p + 3;
         e = s;
         while (e + 3 <= len
                && !(data[e] == 0 && data[e+1] == 0 && data[e+2] == 1)) e++;
         if (e + 3 > len) e = len;
         if (s < e)
         {
            int r = rh264_video_handle_slice_nal(v, data + s, e - s, &got_pic);
            if (r < 0) return -1;
            /* One coded picture per call: sequentially the picture
             * shown is this sample's, complete at its last slice, and
             * the rest of the sample is nothing. With pictures in
             * flight the one shown at a slice's open is an earlier
             * picture's, and this sample's remaining slices still
             * have to be queued: the call ends with the sample. */
            if (r == 1 && !v->threaded) break;
         }
         p = e;
      }
   }
   /* on the backend a picture is whole at the end of its access unit */
   if (v->hw && rh264_hw_finish(v, &got_pic) != 0)
      return -1;
   return got_pic ? 1 : 0;
}

void rh264_video_set_skip_nonref(rh264_video *v, int skip)
{
   if (v)
      retro_atomic_store_release_int(&v->skip_nonref, skip ? 1 : 0);
}

int rh264_video_dropped(const rh264_video *v)
{
   return v ? v->dropped : 0;
}

void rh264_video_set_contexts(rh264_video *v, int n)
{
   if (!v)
      return;
   if (n < 1) n = 1;
   if (n > RH264_MAX_CTX) n = RH264_MAX_CTX;
   v->nctx = n;
}

void rh264_video_stats(const rh264_video *v, int *posted, int *inflight_x100,
      int *at_max, int *join_waits, int *pop_held, int *pop_waits)
{
   if (!v)
      return;
   if (posted)        *posted        = v->st_posted;
   if (inflight_x100) *inflight_x100 = v->st_posted ? v->st_inflight_sum * 100 / v->st_posted : 0;
   if (at_max)        *at_max        = v->st_inflight_max;
   if (join_waits)    *join_waits    = v->st_join_waits;
   if (pop_held)      *pop_held      = v->st_pop_held;
   if (pop_waits)     *pop_waits     = v->st_pop_waits;
}

int rh264_video_jobs_at_once(void)
{
#ifdef HAVE_THREADS
   int m = retro_atomic_load_acquire_int(&rh264_jobs_running_max);
   retro_atomic_store_release_int(&rh264_jobs_running_max, 0);
   return m;
#else
   return 0;
#endif
}

void rh264_video_row_wait_stats(int *waits, int *rows_short_x100)
{
   int w = retro_atomic_load_acquire_int(&rh264_row_waits);
   int r = retro_atomic_load_acquire_int(&rh264_row_short_sum);
   if (waits)           *waits = w;
   if (rows_short_x100) *rows_short_x100 = w ? r * 100 / w : 0;
   retro_atomic_store_release_int(&rh264_row_waits, 0);
   retro_atomic_store_release_int(&rh264_row_short_sum, 0);
}

void rh264_video_set_publish_delay(int max_us)
{
#ifdef HAVE_THREADS
   rh264_publish_delay_us = max_us > 0 ? max_us : 0;
#else
   (void)max_us;
#endif
}

void rh264_video_set_thread_pool(rh264_video *v, void *pool, int threads)
{
#ifdef HAVE_THREADS
   /* a stream on the backend decodes nothing here */
   if (!v || v->hw)
      return;
#ifndef RETRO_ATOMIC_HAS_PTR
   /* Pictures in flight on the pool hand their buffers over through
    * the slots: no pool without pointer atomics */
   pool = NULL;
#endif
   if (!pool || threads < 2)
   {
      v->pool     = NULL;
      v->threaded = 0;
      return;
   }
   if (!rh264_rows_ec_ok)
   {
      if (!retro_eventcount_init(&rh264_rows_ec))
      {
         retro_eventcount_free(&rh264_rows_ec);
         return;
      }
      rh264_rows_ec_ok = 1;
   }
   /* One context per thread that may be decoding - the workers and
    * the submitter, which helps - and one for the picture being
    * built; the join is the throttle when they are all taken. */
   rh264_video_set_contexts(v, threads + 1 > RH264_MAX_CTX ? RH264_MAX_CTX : threads + 1);
   v->pool     = pool;
   v->threaded = v->nctx > 1;
#else
   (void)v; (void)pool; (void)threads;
#endif
}

int rh264_video_bit_depth(const rh264_video *v)
{
   const rh264_frame *f;
   if (!v) return 0;
   f = (v->out_show >= 0) ? &v->out[v->out_show] : &v->cur->f;
   return f->bd ? f->bd : 8;
}

const uint8_t *rh264_video_plane(const rh264_video *v, int plane,
      int *stride, int *width, int *height)
{
   const uint8_t *p;
   const rh264_frame *f;
   int st, w, h;
   if (!v || v->hw) return NULL;
   f = (v->out_show >= 0) ? &v->out[v->out_show] : &v->cur->f;
   /* Cropping is display-only (reference reads use the full coded
    * picture), so it is applied here and nowhere else: the returned
    * pointer starts at the visible window's origin.  crop_x is always
    * even, and crop_y is even whenever chroma subsamples vertically,
    * so the chroma origin divides exactly. */
   if (plane == 0)      { st = f->ysb;     w = f->w;         h = f->h;
                          p = f->Yb + RH264_OFF(f, (size_t)f->cropy * st + f->cropx); }
   else                 { st = f->csb;     w = f->c444 ? f->w : (f->w+1)/2;
                          h = (f->cmbh == 16) ? f->h : (f->h+1)/2;
                          p = ((plane == 1) ? f->Ub : f->Vb)
                            + RH264_OFF(f, (size_t)((f->cmbh == 16) ? f->cropy
                                                       : (f->cropy >> 1)) * st
                            + (f->c444 ? f->cropx : (f->cropx >> 1))); }
   if (stride) *stride = st;
   if (width)  *width  = w;
   if (height) *height = h;
   return p;
}

int rh264_video_drain(rh264_video *v)
{
   int i, bi = -1;
   if (!v) return -1;
   if (v->hw)
   {
      int got = 0;
      v->hw->fresh = 0;
      if (v->hw->broken || rh264_hw_finish(v, &got) != 0)
         return -1;
      if (got)
         return 0;
      if ((i = rh264_hw_out_pick(v->hw)) < 0)
         return -1;
      return rh264_hw_show(v, i);
   }
   if (v->threaded)
   {
      /* The last picture never saw a next one open: it goes to the
       * pool now and takes its place, and every picture in flight
       * lands before the queue is read. */
      int got = 0;
      if (v->cur->pic_open)
      {
         rh264_video_post_picture(v);
         rh264_video_finish_picture(v, &got);
         if (got)
            return 0;
      }
#ifdef HAVE_THREADS
      for (i = 0; i < v->nctx; i++)
         rh264_ctx_join(v, &v->ctx[i]);
#endif
   }
   for (i = 0; i < RH264_OUT_SLOTS; i++)
   {
      if (!v->out_used[i]) continue;
      if (bi < 0 || v->out_gen[i] < v->out_gen[bi]
            || (v->out_gen[i] == v->out_gen[bi] && v->out_poc[i] < v->out_poc[bi]))
         bi = i;
   }
   if (bi < 0) return -1;
   v->out_used[bi] = 0; v->out_len--;
   v->out_show = bi;
   return 0;
}
