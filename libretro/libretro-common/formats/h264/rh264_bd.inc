/* rh264_bd.inc - the sample-touching kernels of rh264, instantiated per
 * bit depth from rh264.c: RH264_PEL is the sample type (uint8_t at 8
 * bits, uint16_t above), RH264_BD the instantiation (8, or 16 for
 * every higher depth - the actual depth arrives as 'bd' and sets the
 * clip ceiling RH264_PMAX), RH264_FN(name) the per-depth symbol.  The
 * SIMD paths are 8-bit only; the 16-bit instantiation is the C code.
 * Everything above these kernels - parsing, prediction control,
 * transforms on coefficients, the deblocking strengths - lives once in
 * rh264.c and hands sample pointers (in bytes) down here.
 * Not a standalone translation unit. */

#if RH264_BD == 8
#define RH264_PMAX  255
#define RH264_PHALF 128
#else
#define RH264_PMAX  ((1 << bd) - 1)
#define RH264_PHALF (1 << (bd - 1))
#endif
#define RH264_CLIP(v) ((v)<0?0:((v)>RH264_PMAX?RH264_PMAX:(v)))
/* the unshifted 6-tap sums of the luma interpolation: up to 52 * 255
 * fits sixteen bits; 52 * 16383 does not (it overflowed at 10 bits,
 * one quarter-sample position of one block off by 32) */
#if RH264_BD == 8
#define RH264_HB int16_t
#else
#define RH264_HB int32_t
#endif

static void RH264_FN(add_bypass)(RH264_PEL *dst, int stride,
      const int32_t *r, int rstride, int w, int h, int bd)
{
   int x, y;
   for (y = 0; y < h; y++)
   {
      RH264_PEL *d = dst + y*stride; const int32_t *rr = r + y*rstride;
      for (x = 0; x < w; x++)
      {
         int32_t v = (int32_t)d[x] + rr[x];
         d[x] = (RH264_PEL)(v < 0 ? 0 : (v > RH264_PMAX ? RH264_PMAX : v));
      }
   }
}

static void RH264_FN(intra16x16)(RH264_PEL *dst,int stride,int mode,int have_up,int have_left, int bd){
   int x,y; const RH264_PEL *up=dst-stride;
   /* Vertical(0)/Horizontal(1)/Plane(3) require the corresponding neighbours;
    * fall back to DC(2) if unavailable rather than read out of bounds. */
   if((mode==0&&!have_up)||(mode==1&&!have_left)||(mode==3&&!(have_up&&have_left)))
      mode=2;
   switch(mode){
   case 0: for(y=0;y<16;y++)for(x=0;x<16;x++)dst[y*stride+x]=up[x]; break;
   case 1: for(y=0;y<16;y++)for(x=0;x<16;x++)dst[y*stride+x]=dst[y*stride-1]; break;
   case 2:{int sum=0,cnt=0,dc;
      if(have_up){for(x=0;x<16;x++)sum+=up[x];cnt+=16;}
      if(have_left){for(y=0;y<16;y++)sum+=dst[y*stride-1];cnt+=16;}
      dc=(cnt==32)?(sum+16)>>5:(cnt==16)?(sum+8)>>4:RH264_PHALF;
      for(y=0;y<16;y++)for(x=0;x<16;x++)dst[y*stride+x]=(RH264_PEL)dc;
      break;}
   default:{int H=0,V=0,a,bb,c,i,val; const RH264_PEL *tl=dst-stride-1;
      for(i=0;i<7;i++)H+=(i+1)*(up[8+i]-up[6-i]);
      H+=8*(up[15]-tl[0]);
      for(i=0;i<7;i++)V+=(i+1)*(dst[(8+i)*stride-1]-dst[(6-i)*stride-1]);
      V+=8*(dst[15*stride-1]-tl[0]);
      a=16*(up[15]+dst[15*stride-1]); bb=(5*H+32)>>6; c=(5*V+32)>>6;
      for(y=0;y<16;y++)for(x=0;x<16;x++){val=(a+bb*(x-7)+c*(y-7)+16)>>5;dst[y*stride+x]=(RH264_PEL)RH264_CLIP(val);}
      break;}
   }
}
/* Intra chroma prediction (8.3.4).  ch is the block height: 8 for
 * 4:2:0, 16 for 4:2:2, where chroma keeps the luma height.  DC works
 * per 4x4 block, so 4:2:2 simply has twice as many of them down the
 * macroblock; the plane mode's vertical term is built from sixteen
 * neighbours rather than eight and scaled to match. */
static void RH264_FN(intra_chroma_h)(RH264_PEL *dst,int stride,int mode,
      int have_up,int have_left,int ch, int bd){
   int x,y; const RH264_PEL *up=dst-stride;
   /* Horizontal(1)/Vertical(2)/Plane(3) require the corresponding neighbours.
    * A conformant stream never signals them when unavailable, but arbitrary
    * (e.g. thumbnail) input might; fall back to DC rather than read out of
    * bounds. */
   if((mode==1&&!have_left)||(mode==2&&!have_up)||(mode==3&&!(have_up&&have_left)))
      mode=0;
   switch(mode){
   case 1: for(y=0;y<ch;y++)for(x=0;x<8;x++)dst[y*stride+x]=dst[y*stride-1]; break;
   case 2: for(y=0;y<ch;y++)for(x=0;x<8;x++)dst[y*stride+x]=up[x]; break;
   case 3:{int H=0,V=0,a,bb,c,i,val; const RH264_PEL *tl=dst-stride-1;
      int yh=ch/2, cshift=(ch==16)?6:5, cmul=(ch==16)?5:17;
      for(i=0;i<3;i++)H+=(i+1)*(up[4+i]-up[2-i]);
      H+=4*(up[7]-tl[0]);
      for(i=0;i<yh-1;i++)V+=(i+1)*(dst[(yh+i)*stride-1]-dst[(yh-2-i)*stride-1]);
      V+=yh*(dst[(ch-1)*stride-1]-tl[0]);
      a=16*(up[7]+dst[(ch-1)*stride-1]); bb=(17*H+16)>>5;
      c=(cmul*V+(1<<(cshift-1)))>>cshift;
      for(y=0;y<ch;y++)for(x=0;x<8;x++){
         val=(a+bb*(x-3)+c*(y-(yh-1))+16)>>5;
         dst[y*stride+x]=(RH264_PEL)RH264_CLIP(val);}
      break;}
   default:{int bx,by;
      for(by=0;by<ch/4;by++)for(bx=0;bx<2;bx++){
         int sum=0,cnt=0,dc,i,ux=bx*4,uy=by*4,use_up=have_up,use_left=have_left;
         /* 8.3.4.1-3: a block on the top row away from the left edge
          * prefers the row above; one on the left column below the top
          * prefers the left column. */
         if(bx==1&&by==0){use_left=use_up?0:have_left;}
         else if(bx==0&&by>0){use_up=have_left?0:have_up;}
         if(use_up){for(i=0;i<4;i++)sum+=up[ux+i];cnt+=4;}
         if(use_left){for(i=0;i<4;i++)sum+=dst[(uy+i)*stride-1];cnt+=4;}
         dc=(cnt==8)?(sum+4)>>3:(cnt==4)?(sum+2)>>2:RH264_PHALF;
         for(y=0;y<4;y++)for(x=0;x<4;x++)dst[(uy+y)*stride+ux+x]=(RH264_PEL)dc;
      }
      break;
   }
   }
}
/* Intra_4x4 (8.3.1.2). p[] samples: p[-1,-1]=C, p[x,-1]=T[0..7] (top+topright),
 * p[-1,y]=L[0..3]. avail flags gate which samples are valid. */
static void RH264_FN(intra4x4)(RH264_PEL *dst,int stride,int mode,int have_up,int have_left,int have_up_right,int have_ul, int bd){
   const RH264_PEL *up=dst-stride; RH264_PEL L[4],C,T[8]; int x,y;
   for(y=0;y<4;y++)L[y]=have_left?dst[y*stride-1]:0;
   C=have_ul?dst[-stride-1]:0;
   for(x=0;x<4;x++)T[x]=have_up?up[x]:0;
   for(x=4;x<8;x++)T[x]=have_up_right?up[x]:(have_up?T[3]:0);
   #define PT(k) ((k)==-1?C:T[k])
   #define PL(k) ((k)==-1?C:L[k])
   #define P(xx,yy) dst[(yy)*stride+(xx)]
   for(y=0;y<4;y++)for(x=0;x<4;x++){
      int v=0;
      switch(mode){
      case 0: v=PT(x); break;
      case 1: v=PL(y); break;
      case 2: { int s=0,cnt=0; if(have_up){s+=T[0]+T[1]+T[2]+T[3];cnt+=4;}
               if(have_left){s+=L[0]+L[1]+L[2]+L[3];cnt+=4;}
               v=(cnt==8)?(s+4)>>3:(cnt==4)?(s+2)>>2:RH264_PHALF; } break;
      case 3: { int i=x+y; v=(i==6)?((T[6]+3*T[7]+2)>>2):((T[i]+2*T[i+1]+T[i+2]+2)>>2); } break;
      case 4:
         if(x>y) v=(PT(x-y-2)+2*PT(x-y-1)+PT(x-y)+2)>>2;
         else if(x<y) v=(PL(y-x-2)+2*PL(y-x-1)+PL(y-x)+2)>>2;
         else v=(PT(0)+2*C+PL(0)+2)>>2;
         break;
      case 5: { int z=2*x-y;
         if(z>=0){ if((z&1)==0) v=(PT(x-(y>>1)-1)+PT(x-(y>>1))+1)>>1;
                   else v=(PT(x-(y>>1)-2)+2*PT(x-(y>>1)-1)+PT(x-(y>>1))+2)>>2; }
         else if(z==-1) v=(PL(0)+2*C+PT(0)+2)>>2;
         else v=(PL(y-1)+2*PL(y-2)+PL(y-3)+2)>>2; } break;
      case 6: { int z=2*y-x;
         if(z>=0){ if((z&1)==0) v=(PL(y-(x>>1)-1)+PL(y-(x>>1))+1)>>1;
                   else v=(PL(y-(x>>1)-2)+2*PL(y-(x>>1)-1)+PL(y-(x>>1))+2)>>2; }
         else if(z==-1) v=(PT(0)+2*C+PL(0)+2)>>2;
         else v=(PT(x-1)+2*PT(x-2)+PT(x-3)+2)>>2; } break;
      case 7:
         if((y&1)==0) v=(PT(x+(y>>1))+PT(x+(y>>1)+1)+1)>>1;
         else v=(PT(x+(y>>1))+2*PT(x+(y>>1)+1)+PT(x+(y>>1)+2)+2)>>2;
         break;
      default: { int z=x+2*y;
         if(z<5){ if((z&1)==0) v=(PL(y+(x>>1))+PL(y+(x>>1)+1)+1)>>1;
                  else v=(PL(y+(x>>1))+2*PL(y+(x>>1)+1)+PL(y+(x>>1)+2)+2)>>2; }
         else if(z==5) v=(PL(2)+3*PL(3)+2)>>2;
         else v=PL(3); } break;
      }
      P(x,y)=(RH264_PEL)v;
   }
   #undef PT
   #undef PL
   #undef P
}

/* Intra 8x8 luma prediction (8.3.2): build the reference row/column with
 * top-right substitution, low-pass filter every reference sample once
 * (8.3.2.2.1), then apply the requested mode. Mode numbering matches the
 * 4x4 modes. */
static void RH264_FN(intra8x8)(RH264_PEL *dst, int stride, int mode,
      int have_up, int have_left, int have_ul, int have_ur, int bd)
{
   /* p[0]=corner, p[1..16]=top row (incl. top-right 8), p[17..24]=left */
   int p[25], q[25];
   int x, y, i;
   const RH264_PEL *up = dst - stride;
   p[0] = have_ul ? dst[-stride - 1] : 0;
   for (x = 0; x < 8; x++)  p[1 + x] = have_up ? up[x] : 0;
   for (x = 8; x < 16; x++) p[1 + x] = have_ur ? up[x]
                                     : (have_up ? p[8] : 0);
   for (y = 0; y < 8; y++)  p[17 + y] = have_left ? dst[y * stride - 1] : 0;
   /* filter (kept for every mode; unused samples are simply ignored) */
   for (i = 0; i < 25; i++) q[i] = p[i];
   if (have_ul)
   {
      if (have_up && have_left)
         q[0] = (p[17] + 2*p[0] + p[1] + 2) >> 2;
      else if (have_up)
         q[0] = (3*p[0] + p[1] + 2) >> 2;
      else if (have_left)
         q[0] = (3*p[0] + p[17] + 2) >> 2;
   }
   if (have_up)
   {
      q[1] = have_ul ? ((p[0] + 2*p[1] + p[2] + 2) >> 2)
                     : ((3*p[1] + p[2] + 2) >> 2);
      for (i = 2; i < 16; i++)
         q[i] = (p[i-1] + 2*p[i] + p[i+1] + 2) >> 2;
      q[16] = (p[15] + 3*p[16] + 2) >> 2;
   }
   if (have_left)
   {
      q[17] = have_ul ? ((p[0] + 2*p[17] + p[18] + 2) >> 2)
                      : ((3*p[17] + p[18] + 2) >> 2);
      for (i = 18; i < 24; i++)
         q[i] = (p[i-1] + 2*p[i] + p[i+1] + 2) >> 2;
      q[24] = (p[23] + 3*p[24] + 2) >> 2;
   }
/* index -1 refers to the filtered corner sample for either arm */
#define P8T(k) q[1 + (k)]
#define P8L(k) ((k) == -1 ? q[0] : q[17 + (k)])
   for (y = 0; y < 8; y++) for (x = 0; x < 8; x++)
   {
      int v = 0;
      switch (mode)
      {
      case 0: v = P8T(x); break;
      case 1: v = P8L(y); break;
      case 2:
      {
         int s = 0, cnt = 0;
         if (have_up)   { for (i = 0; i < 8; i++) s += P8T(i); cnt += 8; }
         if (have_left) { for (i = 0; i < 8; i++) s += P8L(i); cnt += 8; }
         v = (cnt == 16) ? (s + 8) >> 4 : (cnt == 8) ? (s + 4) >> 3 : RH264_PHALF;
         break;
      }
      case 3:
         v = (x == 7 && y == 7)
           ? (P8T(14) + 3*P8T(15) + 2) >> 2
           : (P8T(x+y) + 2*P8T(x+y+1) + P8T(x+y+2) + 2) >> 2;
         break;
      case 4:
         if (x > y)
            v = (P8T(x-y-2) + 2*P8T(x-y-1) + P8T(x-y) + 2) >> 2;
         else if (x < y)
            v = (P8L(y-x-2) + 2*P8L(y-x-1) + P8L(y-x) + 2) >> 2;
         else
            v = (P8T(0) + 2*q[0] + P8L(0) + 2) >> 2;
         break;
      case 5:
      {
         int z = 2*x - y;
         if (z >= 0)
         {
            int k = x - (y >> 1);
            v = ((z & 1) == 0) ? (P8T(k-1) + P8T(k) + 1) >> 1
                               : (P8T(k-2) + 2*P8T(k-1) + P8T(k) + 2) >> 2;
         }
         else if (z == -1)
            v = (P8L(0) + 2*q[0] + P8T(0) + 2) >> 2;
         else
            v = (P8L(y-2*x-1) + 2*P8L(y-2*x-2) + P8L(y-2*x-3) + 2) >> 2;
         break;
      }
      case 6:
      {
         int z = 2*y - x;
         if (z >= 0)
         {
            int k = y - (x >> 1);
            v = ((z & 1) == 0) ? (P8L(k-1) + P8L(k) + 1) >> 1
                               : (P8L(k-2) + 2*P8L(k-1) + P8L(k) + 2) >> 2;
         }
         else if (z == -1)
            v = (P8T(0) + 2*q[0] + P8L(0) + 2) >> 2;
         else
            v = (P8T(x-2*y-1) + 2*P8T(x-2*y-2) + P8T(x-2*y-3) + 2) >> 2;
         break;
      }
      case 7:
      {
         int k = x + (y >> 1);
         v = ((y & 1) == 0) ? (P8T(k) + P8T(k+1) + 1) >> 1
                            : (P8T(k) + 2*P8T(k+1) + P8T(k+2) + 2) >> 2;
         break;
      }
      case 8:
      {
         int z = x + 2*y;
         if (z < 13)
         {
            int k = y + (x >> 1);
            v = ((z & 1) == 0) ? (P8L(k) + P8L(k+1) + 1) >> 1
                               : (P8L(k) + 2*P8L(k+1) + P8L(k+2) + 2) >> 2;
         }
         else if (z == 13)
            v = (P8L(6) + 3*P8L(7) + 2) >> 2;
         else
            v = P8L(7);
         break;
      }
      }
      dst[y * stride + x] = (RH264_PEL)v;
   }
#undef P8T
#undef P8L
}

/* Filter one luma sample line across an edge (p3..p0 | q0..q3), stride s.
 * bS in 1..4. a=alpha, be=beta, tc0v from table (unused for bS==4). */
static void RH264_FN(filter_luma_edge)(RH264_PEL *e,int s,int bS,int a,int be,int tc0v, int bd)
{
   int p0=e[-1*s],p1=e[-2*s],p2=e[-3*s],p3=e[-4*s];
   int q0=e[0],q1=e[1*s],q2=e[2*s],q3=e[3*s];
   if(RH264_ABS(p0-q0)>=a||RH264_ABS(p1-p0)>=be||RH264_ABS(q1-q0)>=be) return;
   if(bS<4){
      int tc=tc0v, ap=RH264_ABS(p2-p0), aq=RH264_ABS(q2-q0), d;
      if(ap<be) tc++;
      if(aq<be) tc++;
      /* q0-p0 is a difference of two samples and so may be negative;
       * shifting that left is undefined, and this is the scalar path a
       * target without vector support takes (8.7.2.3). */
      d=(((q0-p0)*4)+(p1-q1)+4)>>3;
      if(d<-tc)d=-tc; else if(d>tc)d=tc;
      e[-1*s]=(RH264_PEL)RH264_CLIP(p0+d);
      e[0]   =(RH264_PEL)RH264_CLIP(q0-d);
      if(ap<be){ int dp=(p2+((p0+q0+1)>>1)-2*p1)>>1; if(dp<-tc0v)dp=-tc0v;else if(dp>tc0v)dp=tc0v; e[-2*s]=(RH264_PEL)(p1+dp); }
      if(aq<be){ int dq=(q2+((p0+q0+1)>>1)-2*q1)>>1; if(dq<-tc0v)dq=-tc0v;else if(dq>tc0v)dq=tc0v; e[1*s]=(RH264_PEL)(q1+dq); }
   } else {
      int ap=RH264_ABS(p2-p0), aq=RH264_ABS(q2-q0);
      if(RH264_ABS(p0-q0)<((a>>2)+2)){
         if(ap<be){
            e[-1*s]=(RH264_PEL)((p2+2*p1+2*p0+2*q0+q1+4)>>3);
            e[-2*s]=(RH264_PEL)((p2+p1+p0+q0+2)>>2);
            e[-3*s]=(RH264_PEL)((2*p3+3*p2+p1+p0+q0+4)>>3);
         } else e[-1*s]=(RH264_PEL)((2*p1+p0+q1+2)>>2);
         if(aq<be){
            e[0]   =(RH264_PEL)((q2+2*q1+2*q0+2*p0+p1+4)>>3);
            e[1*s] =(RH264_PEL)((q2+q1+q0+p0+2)>>2);
            e[2*s] =(RH264_PEL)((2*q3+3*q2+q1+q0+p0+4)>>3);
         } else e[0]=(RH264_PEL)((2*q1+q0+p1+2)>>2);
      } else {
         e[-1*s]=(RH264_PEL)((2*p1+p0+q1+2)>>2);
         e[0]   =(RH264_PEL)((2*q1+q0+p1+2)>>2);
      }
   }
}

#if RH264_BD == 8 && defined(RH264_SSE2)
/* Store the low four bytes of a packed vector. */
static RH264_INLINE void RH264_FN(sse2_store4)(RH264_PEL *d, __m128i v)
{
   uint32_t t = (uint32_t)_mm_cvtsi128_si32(v);
   memcpy(d, &t, 4);
}

/* Load exactly four bytes into the low lanes. */
static RH264_INLINE __m128i RH264_FN(sse2_load4)(const RH264_PEL *s)
{
   uint32_t t;
   memcpy(&t, s, 4);
   return _mm_cvtsi32_si128((int)t);
}
#endif

#if RH264_BD == 8 && defined(RH264_NEON)
/* Store the low four bytes of a packed vector. */
static RH264_INLINE void RH264_FN(neon_store4)(RH264_PEL *d, uint8x8_t v)
{
   uint32_t t = vget_lane_u32(vreinterpret_u32_u8(v), 0);
   memcpy(d, &t, 4);
}

/* Load exactly four bytes into the low lanes. */
static RH264_INLINE uint8x8_t RH264_FN(neon_load4)(const RH264_PEL *s)
{
   uint32_t t;
   memcpy(&t, s, 4);
   return vreinterpret_u8_u32(vdup_n_u32(t));
}
#endif
/* Add a dequantised, inverse-transformed residual block to the
 * prediction already in the plane and clip (8.5.13.3): r is rounded by
 * (r + 32) >> 6, n is 4 or 8, and stride is the plane's.  Every
 * residual path in the decoder ends here, so it runs a row at a time
 * rather than a sample at a time: unpack the predicted bytes into
 * 16-bit lanes, add, and pack back with unsigned saturation, which
 * performs the clip to [0,RH264_PMAX] for free. */
static void RH264_FN(add_residual)(RH264_PEL *dst,int stride,const int32_t *r,int n, int bd)
{
   int y;
#if RH264_BD == 8 && defined(RH264_SSE2)
   const __m128i vz=_mm_setzero_si128();
   const __m128i v32=_mm_set1_epi32(32);
   /* The sum is formed in 32-bit lanes and narrowed with saturation at
    * each step (32->16 signed, 16->8 unsigned).  Adding in 16-bit lanes
    * instead would be shorter but wraps on the out-of-range residuals a
    * corrupt stream can produce, which would make this path disagree
    * with the scalar one below. */
   for(y=0;y<n;y++){
      const int32_t *rr=r+y*n;
      __m128i p8=(n==8) ? _mm_loadl_epi64((const __m128i*)(dst+y*stride))
                        : RH264_FN(sse2_load4)(dst+y*stride);
      __m128i p16=_mm_unpacklo_epi8(p8,vz);
      __m128i s0=_mm_add_epi32(_mm_unpacklo_epi16(p16,vz),
            _mm_srai_epi32(_mm_add_epi32(
                  _mm_loadu_si128((const __m128i*)rr),v32),6));
      if(n==8){
         __m128i s1=_mm_add_epi32(_mm_unpackhi_epi16(p16,vz),
               _mm_srai_epi32(_mm_add_epi32(
                     _mm_loadu_si128((const __m128i*)(rr+4)),v32),6));
         _mm_storel_epi64((__m128i*)(dst+y*stride),
               _mm_packus_epi16(_mm_packs_epi32(s0,s1),vz));
      }else{
         uint32_t w=(uint32_t)_mm_cvtsi128_si32(
               _mm_packus_epi16(_mm_packs_epi32(s0,vz),vz));
         memcpy(dst+y*stride,&w,4);
      }
   }
#elif RH264_BD == 8 && defined(RH264_NEON)
   /* as above: sum in 32-bit lanes, saturate on the way down */
   for(y=0;y<n;y++){
      const int32_t *rr=r+y*n;
      uint16x8_t p16=vmovl_u8((n==8) ? vld1_u8(dst+y*stride)
                                     : RH264_FN(neon_load4)(dst+y*stride));
      int32x4_t s0=vaddq_s32(
            vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(p16))),
            vshrq_n_s32(vaddq_s32(vld1q_s32(rr),vdupq_n_s32(32)),6));
      if(n==8){
         int32x4_t s1=vaddq_s32(
               vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(p16))),
               vshrq_n_s32(vaddq_s32(vld1q_s32(rr+4),vdupq_n_s32(32)),6));
         vst1_u8(dst+y*stride,vqmovun_s16(
               vcombine_s16(vqmovn_s32(s0),vqmovn_s32(s1))));
      }else{
         RH264_FN(neon_store4)(dst+y*stride,vqmovun_s16(
               vcombine_s16(vqmovn_s32(s0),vdup_n_s16(0))));
      }
   }
#else
   { int x;
     for(y=0;y<n;y++) for(x=0;x<n;x++){
        int v=dst[y*stride+x]+((r[y*n+x]+32)>>6);
        dst[y*stride+x]=(RH264_PEL)RH264_CLIP(v); } }
#endif
}


#if RH264_BD == 8 && defined(RH264_SSE2)
#define RH264_SSE2_ABS16(x) _mm_max_epi16((x), _mm_sub_epi16(_mm_setzero_si128(), (x)))
#define RH264_SSE2_SEL(m, a, b) _mm_or_si128(_mm_and_si128((m), (a)), _mm_andnot_si128((m), (b)))

/* Transpose an 8x8 byte tile: in[k] and out[j] carry eight bytes in the
 * low half of each vector. Applying it twice returns the original tile. */
static void RH264_FN(sse2_tr8x8)(const __m128i in[8], __m128i out[8])
{
   __m128i t0 = _mm_unpacklo_epi8(in[0], in[1]);
   __m128i t1 = _mm_unpacklo_epi8(in[2], in[3]);
   __m128i t2 = _mm_unpacklo_epi8(in[4], in[5]);
   __m128i t3 = _mm_unpacklo_epi8(in[6], in[7]);
   __m128i u0 = _mm_unpacklo_epi16(t0, t1);
   __m128i u1 = _mm_unpackhi_epi16(t0, t1);
   __m128i u2 = _mm_unpacklo_epi16(t2, t3);
   __m128i u3 = _mm_unpackhi_epi16(t2, t3);
   __m128i c0 = _mm_unpacklo_epi32(u0, u2);
   __m128i c1 = _mm_unpackhi_epi32(u0, u2);
   __m128i c2 = _mm_unpacklo_epi32(u1, u3);
   __m128i c3 = _mm_unpackhi_epi32(u1, u3);
   out[0] = c0; out[1] = _mm_srli_si128(c0, 8);
   out[2] = c1; out[3] = _mm_srli_si128(c1, 8);
   out[4] = c2; out[5] = _mm_srli_si128(c2, 8);
   out[6] = c3; out[7] = _mm_srli_si128(c3, 8);
}

/* The luma edge filter (8.7.2.3/8.7.2.4) on up to eight lanes held as int16
 * vectors v[0..7] = p3..q3; filtered p2..q2 are written back into v[1..6].
 * Inactive lanes compute garbage the caller never stores. */
static void RH264_FN(sse2_luma_kernel)(__m128i v[8], int bS, int a, int be,
      __m128i t0)
{
   __m128i p3 = v[0], p2 = v[1], p1 = v[2], p0 = v[3];
   __m128i q0 = v[4], q1 = v[5], q2 = v[6], q3 = v[7];
   __m128i va  = _mm_set1_epi16((short)a);
   __m128i vbe = _mm_set1_epi16((short)be);
   __m128i d0  = RH264_SSE2_ABS16(_mm_sub_epi16(p0, q0));
   __m128i filt = _mm_and_si128(_mm_cmplt_epi16(d0, va), _mm_and_si128(
         _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(p1, p0)), vbe),
         _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(q1, q0)), vbe)));
   __m128i apl = _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(p2, p0)), vbe);
   __m128i aql = _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(q2, q0)), vbe);
   if (bS < 4)
   {
      __m128i one = _mm_set1_epi16(1);
      __m128i tc  = _mm_add_epi16(t0, _mm_add_epi16(
            _mm_and_si128(apl, one), _mm_and_si128(aql, one)));
      __m128i d = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(_mm_sub_epi16(q0, p0), 2),
            _mm_sub_epi16(p1, q1)), _mm_set1_epi16(4)), 3);
      __m128i half, dp, dq, np, nq;
      d = _mm_max_epi16(_mm_min_epi16(d, tc),
            _mm_sub_epi16(_mm_setzero_si128(), tc));
      np = _mm_max_epi16(_mm_min_epi16(_mm_add_epi16(p0, d),
            _mm_set1_epi16(RH264_PMAX)), _mm_setzero_si128());
      nq = _mm_max_epi16(_mm_min_epi16(_mm_sub_epi16(q0, d),
            _mm_set1_epi16(RH264_PMAX)), _mm_setzero_si128());
      v[3] = RH264_SSE2_SEL(filt, np, p0);
      v[4] = RH264_SSE2_SEL(filt, nq, q0);
      half = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(p0, q0), one), 1);
      dp = _mm_srai_epi16(_mm_sub_epi16(_mm_add_epi16(p2, half),
            _mm_slli_epi16(p1, 1)), 1);
      dp = _mm_max_epi16(_mm_min_epi16(dp, t0),
            _mm_sub_epi16(_mm_setzero_si128(), t0));
      v[2] = RH264_SSE2_SEL(_mm_and_si128(filt, apl),
            _mm_add_epi16(p1, dp), p1);
      dq = _mm_srai_epi16(_mm_sub_epi16(_mm_add_epi16(q2, half),
            _mm_slli_epi16(q1, 1)), 1);
      dq = _mm_max_epi16(_mm_min_epi16(dq, t0),
            _mm_sub_epi16(_mm_setzero_si128(), t0));
      v[5] = RH264_SSE2_SEL(_mm_and_si128(filt, aql),
            _mm_add_epi16(q1, dq), q1);
   }
   else
   {
      __m128i two   = _mm_set1_epi16(2);
      __m128i four  = _mm_set1_epi16(4);
      __m128i inner = _mm_cmplt_epi16(d0, _mm_set1_epi16((short)((a >> 2) + 2)));
      __m128i spm = _mm_and_si128(inner, apl);
      __m128i sqm = _mm_and_si128(inner, aql);
      __m128i pq0 = _mm_add_epi16(p0, q0);
      __m128i wkp = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(p1, 1), _mm_add_epi16(p0, q1)), two), 2);
      __m128i wkq = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(q1, 1), _mm_add_epi16(q0, p1)), two), 2);
      __m128i stp0 = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(p2,
            _mm_slli_epi16(_mm_add_epi16(p1, pq0), 1)),
            _mm_add_epi16(q1, four)), 3);
      __m128i stp1 = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(p2,
            _mm_add_epi16(p1, pq0)), two), 2);
      __m128i stp2 = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(p3, 1), _mm_add_epi16(
            _mm_add_epi16(_mm_slli_epi16(p2, 1), p2), p1)),
            _mm_add_epi16(pq0, four)), 3);
      __m128i stq0 = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(q2,
            _mm_slli_epi16(_mm_add_epi16(q1, pq0), 1)),
            _mm_add_epi16(p1, four)), 3);
      __m128i stq1 = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(q2,
            _mm_add_epi16(q1, pq0)), two), 2);
      __m128i stq2 = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(q3, 1), _mm_add_epi16(
            _mm_add_epi16(_mm_slli_epi16(q2, 1), q2), q1)),
            _mm_add_epi16(pq0, four)), 3);
      v[3] = RH264_SSE2_SEL(filt, RH264_SSE2_SEL(spm, stp0, wkp), p0);
      v[2] = RH264_SSE2_SEL(_mm_and_si128(filt, spm), stp1, p1);
      v[1] = RH264_SSE2_SEL(_mm_and_si128(filt, spm), stp2, p2);
      v[4] = RH264_SSE2_SEL(filt, RH264_SSE2_SEL(sqm, stq0, wkq), q0);
      v[5] = RH264_SSE2_SEL(_mm_and_si128(filt, sqm), stq1, q1);
      v[6] = RH264_SSE2_SEL(_mm_and_si128(filt, sqm), stq2, q2);
   }
}

/* The chroma edge filter (8.7.2.3/8.7.2.4) on int16 lanes v[0..3] =
 * p1,p0,q0,q1; filtered p0/q0 land in v[1]/v[2]. */
static void RH264_FN(sse2_chroma_kernel)(__m128i v[4], int bS, int a, int be,
      int tc0v)
{
   __m128i p1 = v[0], p0 = v[1], q0 = v[2], q1 = v[3];
   __m128i va  = _mm_set1_epi16((short)a);
   __m128i vbe = _mm_set1_epi16((short)be);
   __m128i filt = _mm_and_si128(
         _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(p0, q0)), va),
         _mm_and_si128(
         _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(p1, p0)), vbe),
         _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(q1, q0)), vbe)));
   if (bS < 4)
   {
      __m128i tc = _mm_set1_epi16((short)(tc0v + 1));
      __m128i d = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(_mm_sub_epi16(q0, p0), 2),
            _mm_sub_epi16(p1, q1)), _mm_set1_epi16(4)), 3);
      __m128i np, nq;
      d = _mm_max_epi16(_mm_min_epi16(d, tc),
            _mm_sub_epi16(_mm_setzero_si128(), tc));
      np = _mm_max_epi16(_mm_min_epi16(_mm_add_epi16(p0, d),
            _mm_set1_epi16(RH264_PMAX)), _mm_setzero_si128());
      nq = _mm_max_epi16(_mm_min_epi16(_mm_sub_epi16(q0, d),
            _mm_set1_epi16(RH264_PMAX)), _mm_setzero_si128());
      v[1] = RH264_SSE2_SEL(filt, np, p0);
      v[2] = RH264_SSE2_SEL(filt, nq, q0);
   }
   else
   {
      __m128i two = _mm_set1_epi16(2);
      __m128i np = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(p1, 1), _mm_add_epi16(p0, q1)), two), 2);
      __m128i nq = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(q1, 1), _mm_add_epi16(q0, p1)), two), 2);
      v[1] = RH264_SSE2_SEL(filt, np, p0);
      v[2] = RH264_SSE2_SEL(filt, nq, q0);
   }
}
#endif

#if RH264_BD == 8 && defined(RH264_NEON)
#define RH264_NEON_SEL(m, a, b) vbslq_s16((m), (a), (b))

/* Transpose an 8x8 byte tile held as eight uint8x8_t rows. */
static void RH264_FN(neon_tr8x8)(const uint8x8_t in[8], uint8x8_t out[8])
{
   uint8x8x2_t a0 = vtrn_u8(in[0], in[1]);
   uint8x8x2_t a1 = vtrn_u8(in[2], in[3]);
   uint8x8x2_t a2 = vtrn_u8(in[4], in[5]);
   uint8x8x2_t a3 = vtrn_u8(in[6], in[7]);
   uint16x4x2_t b0 = vtrn_u16(vreinterpret_u16_u8(a0.val[0]),
                              vreinterpret_u16_u8(a1.val[0]));
   uint16x4x2_t b1 = vtrn_u16(vreinterpret_u16_u8(a0.val[1]),
                              vreinterpret_u16_u8(a1.val[1]));
   uint16x4x2_t b2 = vtrn_u16(vreinterpret_u16_u8(a2.val[0]),
                              vreinterpret_u16_u8(a3.val[0]));
   uint16x4x2_t b3 = vtrn_u16(vreinterpret_u16_u8(a2.val[1]),
                              vreinterpret_u16_u8(a3.val[1]));
   uint32x2x2_t c0 = vtrn_u32(vreinterpret_u32_u16(b0.val[0]),
                              vreinterpret_u32_u16(b2.val[0]));
   uint32x2x2_t c1 = vtrn_u32(vreinterpret_u32_u16(b1.val[0]),
                              vreinterpret_u32_u16(b3.val[0]));
   uint32x2x2_t c2 = vtrn_u32(vreinterpret_u32_u16(b0.val[1]),
                              vreinterpret_u32_u16(b2.val[1]));
   uint32x2x2_t c3 = vtrn_u32(vreinterpret_u32_u16(b1.val[1]),
                              vreinterpret_u32_u16(b3.val[1]));
   out[0] = vreinterpret_u8_u32(c0.val[0]);
   out[1] = vreinterpret_u8_u32(c1.val[0]);
   out[2] = vreinterpret_u8_u32(c2.val[0]);
   out[3] = vreinterpret_u8_u32(c3.val[0]);
   out[4] = vreinterpret_u8_u32(c0.val[1]);
   out[5] = vreinterpret_u8_u32(c1.val[1]);
   out[6] = vreinterpret_u8_u32(c2.val[1]);
   out[7] = vreinterpret_u8_u32(c3.val[1]);
}

/* Luma edge filter on int16 lanes, mirroring the SSE2 kernel. */
static void RH264_FN(neon_luma_kernel)(int16x8_t v[8], int bS, int a, int be,
      int16x8_t t0)
{
   int16x8_t p3 = v[0], p2 = v[1], p1 = v[2], p0 = v[3];
   int16x8_t q0 = v[4], q1 = v[5], q2 = v[6], q3 = v[7];
   int16x8_t va  = vdupq_n_s16((int16_t)a);
   int16x8_t vbe = vdupq_n_s16((int16_t)be);
   int16x8_t d0  = vabsq_s16(vsubq_s16(p0, q0));
   uint16x8_t filt = vandq_u16(vcltq_s16(d0, va), vandq_u16(
         vcltq_s16(vabsq_s16(vsubq_s16(p1, p0)), vbe),
         vcltq_s16(vabsq_s16(vsubq_s16(q1, q0)), vbe)));
   uint16x8_t apl = vcltq_s16(vabsq_s16(vsubq_s16(p2, p0)), vbe);
   uint16x8_t aql = vcltq_s16(vabsq_s16(vsubq_s16(q2, q0)), vbe);
   if (bS < 4)
   {
      int16x8_t one = vdupq_n_s16(1);
      int16x8_t tc  = vaddq_s16(t0, vaddq_s16(
            vandq_s16(vreinterpretq_s16_u16(apl), one),
            vandq_s16(vreinterpretq_s16_u16(aql), one)));
      int16x8_t d = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(vsubq_s16(q0, p0), 2),
            vsubq_s16(p1, q1)), vdupq_n_s16(4)), 3);
      int16x8_t half, dp, dq, np, nq;
      d = vmaxq_s16(vminq_s16(d, tc), vnegq_s16(tc));
      np = vmaxq_s16(vminq_s16(vaddq_s16(p0, d), vdupq_n_s16(RH264_PMAX)),
            vdupq_n_s16(0));
      nq = vmaxq_s16(vminq_s16(vsubq_s16(q0, d), vdupq_n_s16(RH264_PMAX)),
            vdupq_n_s16(0));
      v[3] = RH264_NEON_SEL(filt, np, p0);
      v[4] = RH264_NEON_SEL(filt, nq, q0);
      half = vshrq_n_s16(vaddq_s16(vaddq_s16(p0, q0), one), 1);
      dp = vshrq_n_s16(vsubq_s16(vaddq_s16(p2, half), vshlq_n_s16(p1, 1)), 1);
      dp = vmaxq_s16(vminq_s16(dp, t0), vnegq_s16(t0));
      v[2] = RH264_NEON_SEL(vandq_u16(filt, apl), vaddq_s16(p1, dp), p1);
      dq = vshrq_n_s16(vsubq_s16(vaddq_s16(q2, half), vshlq_n_s16(q1, 1)), 1);
      dq = vmaxq_s16(vminq_s16(dq, t0), vnegq_s16(t0));
      v[5] = RH264_NEON_SEL(vandq_u16(filt, aql), vaddq_s16(q1, dq), q1);
   }
   else
   {
      int16x8_t two  = vdupq_n_s16(2);
      int16x8_t four = vdupq_n_s16(4);
      uint16x8_t inner = vcltq_s16(d0, vdupq_n_s16((int16_t)((a >> 2) + 2)));
      uint16x8_t spm = vandq_u16(inner, apl);
      uint16x8_t sqm = vandq_u16(inner, aql);
      int16x8_t pq0 = vaddq_s16(p0, q0);
      int16x8_t wkp = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(p1, 1), vaddq_s16(p0, q1)), two), 2);
      int16x8_t wkq = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(q1, 1), vaddq_s16(q0, p1)), two), 2);
      int16x8_t stp0 = vshrq_n_s16(vaddq_s16(vaddq_s16(p2,
            vshlq_n_s16(vaddq_s16(p1, pq0), 1)), vaddq_s16(q1, four)), 3);
      int16x8_t stp1 = vshrq_n_s16(vaddq_s16(vaddq_s16(p2,
            vaddq_s16(p1, pq0)), two), 2);
      int16x8_t stp2 = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(p3, 1), vaddq_s16(
            vaddq_s16(vshlq_n_s16(p2, 1), p2), p1)),
            vaddq_s16(pq0, four)), 3);
      int16x8_t stq0 = vshrq_n_s16(vaddq_s16(vaddq_s16(q2,
            vshlq_n_s16(vaddq_s16(q1, pq0), 1)), vaddq_s16(p1, four)), 3);
      int16x8_t stq1 = vshrq_n_s16(vaddq_s16(vaddq_s16(q2,
            vaddq_s16(q1, pq0)), two), 2);
      int16x8_t stq2 = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(q3, 1), vaddq_s16(
            vaddq_s16(vshlq_n_s16(q2, 1), q2), q1)),
            vaddq_s16(pq0, four)), 3);
      v[3] = RH264_NEON_SEL(filt, RH264_NEON_SEL(spm, stp0, wkp), p0);
      v[2] = RH264_NEON_SEL(vandq_u16(filt, spm), stp1, p1);
      v[1] = RH264_NEON_SEL(vandq_u16(filt, spm), stp2, p2);
      v[4] = RH264_NEON_SEL(filt, RH264_NEON_SEL(sqm, stq0, wkq), q0);
      v[5] = RH264_NEON_SEL(vandq_u16(filt, sqm), stq1, q1);
      v[6] = RH264_NEON_SEL(vandq_u16(filt, sqm), stq2, q2);
   }
}

/* Chroma edge filter on int16 lanes v[0..3] = p1,p0,q0,q1. */
static void RH264_FN(neon_chroma_kernel)(int16x8_t v[4], int bS, int a, int be,
      int tc0v)
{
   int16x8_t p1 = v[0], p0 = v[1], q0 = v[2], q1 = v[3];
   int16x8_t va  = vdupq_n_s16((int16_t)a);
   int16x8_t vbe = vdupq_n_s16((int16_t)be);
   uint16x8_t filt = vandq_u16(
         vcltq_s16(vabsq_s16(vsubq_s16(p0, q0)), va), vandq_u16(
         vcltq_s16(vabsq_s16(vsubq_s16(p1, p0)), vbe),
         vcltq_s16(vabsq_s16(vsubq_s16(q1, q0)), vbe)));
   if (bS < 4)
   {
      int16x8_t tc = vdupq_n_s16((int16_t)(tc0v + 1));
      int16x8_t d = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(vsubq_s16(q0, p0), 2),
            vsubq_s16(p1, q1)), vdupq_n_s16(4)), 3);
      int16x8_t np, nq;
      d = vmaxq_s16(vminq_s16(d, tc), vnegq_s16(tc));
      np = vmaxq_s16(vminq_s16(vaddq_s16(p0, d), vdupq_n_s16(RH264_PMAX)),
            vdupq_n_s16(0));
      nq = vmaxq_s16(vminq_s16(vsubq_s16(q0, d), vdupq_n_s16(RH264_PMAX)),
            vdupq_n_s16(0));
      v[1] = RH264_NEON_SEL(filt, np, p0);
      v[2] = RH264_NEON_SEL(filt, nq, q0);
   }
   else
   {
      int16x8_t two = vdupq_n_s16(2);
      int16x8_t np = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(p1, 1), vaddq_s16(p0, q1)), two), 2);
      int16x8_t nq = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(q1, 1), vaddq_s16(q0, p1)), two), 2);
      v[1] = RH264_NEON_SEL(filt, np, p0);
      v[2] = RH264_NEON_SEL(filt, nq, q0);
   }
}
#endif

/* Filter n lanes of a luma edge: lane k's samples sit at e + k*ls + i*s
 * for i in -4..3. Same arithmetic per lane as rh264_filter_luma_edge. */
static void RH264_FN(filter_luma_edge_n)(RH264_PEL *e, int s, int ls, int n,
      int bS, int a, int be, int tc0v, int bd)
{
   int k = 0;
#if RH264_BD == 8 && defined(RH264_SSE2)
   const __m128i vt0 = _mm_set1_epi16((short)tc0v);
   if (ls == 1)
   {
      /* lanes contiguous; samples at row offsets across the edge */
      const __m128i vz = _mm_setzero_si128();
      for (; k + 8 <= n; k += 8)
      {
         __m128i v[8];
         int i;
         for (i = 0; i < 8; i++)
            v[i] = _mm_unpacklo_epi8(_mm_loadl_epi64(
                  (const __m128i*)(e + k + (i - 4) * s)), vz);
         RH264_FN(sse2_luma_kernel)(v, bS, a, be, vt0);
         for (i = 1; i < 7; i++)
            _mm_storel_epi64((__m128i*)(e + k + (i - 4) * s),
                  _mm_packus_epi16(v[i], v[i]));
      }
      if (k + 4 <= n)
      {
         __m128i v[8];
         int i;
         for (i = 0; i < 8; i++)
            v[i] = _mm_unpacklo_epi8(RH264_FN(sse2_load4)(e + k + (i - 4) * s), vz);
         RH264_FN(sse2_luma_kernel)(v, bS, a, be, vt0);
         for (i = 1; i < 7; i++)
            RH264_FN(sse2_store4)(e + k + (i - 4) * s,
                  _mm_packus_epi16(v[i], v[i]));
         k += 4;
      }
   }
   else if (s == 1)
   {
      /* lanes down the rows; transpose an 8-wide tile per pass */
      const __m128i vz = _mm_setzero_si128();
      for (; k + 4 <= n; )
      {
         int rows = (k + 8 <= n) ? 8 : 4, i;
         __m128i r[8], c[8], v[8];
         for (i = 0; i < rows; i++)
            r[i] = _mm_loadl_epi64((const __m128i*)(e + (k + i) * ls - 4));
         for (; i < 8; i++) r[i] = vz;
         RH264_FN(sse2_tr8x8)(r, c);
         for (i = 0; i < 8; i++)
            v[i] = _mm_unpacklo_epi8(c[i], vz);
         RH264_FN(sse2_luma_kernel)(v, bS, a, be, vt0);
         for (i = 0; i < 8; i++)
            c[i] = _mm_packus_epi16(v[i], v[i]);
         RH264_FN(sse2_tr8x8)(c, r);
         for (i = 0; i < rows; i++)
            _mm_storel_epi64((__m128i*)(e + (k + i) * ls - 4), r[i]);
         k += rows;
      }
   }
#elif RH264_BD == 8 && defined(RH264_NEON)
   {
   const int16x8_t vt0 = vdupq_n_s16((int16_t)tc0v);
   if (ls == 1)
   {
      for (; k + 8 <= n; k += 8)
      {
         int16x8_t v[8];
         int i;
         for (i = 0; i < 8; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(
                  vld1_u8(e + k + (i - 4) * s)));
         RH264_FN(neon_luma_kernel)(v, bS, a, be, vt0);
         for (i = 1; i < 7; i++)
            vst1_u8(e + k + (i - 4) * s, vqmovun_s16(v[i]));
      }
      if (k + 4 <= n)
      {
         int16x8_t v[8];
         int i;
         for (i = 0; i < 8; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(
                  RH264_FN(neon_load4)(e + k + (i - 4) * s)));
         RH264_FN(neon_luma_kernel)(v, bS, a, be, vt0);
         for (i = 1; i < 7; i++)
            RH264_FN(neon_store4)(e + k + (i - 4) * s, vqmovun_s16(v[i]));
         k += 4;
      }
   }
   else if (s == 1)
   {
      for (; k + 4 <= n; )
      {
         int rows = (k + 8 <= n) ? 8 : 4, i;
         uint8x8_t r[8], c[8];
         int16x8_t v[8];
         for (i = 0; i < rows; i++)
            r[i] = vld1_u8(e + (k + i) * ls - 4);
         for (; i < 8; i++) r[i] = vdup_n_u8(0);
         RH264_FN(neon_tr8x8)(r, c);
         for (i = 0; i < 8; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(c[i]));
         RH264_FN(neon_luma_kernel)(v, bS, a, be, vt0);
         for (i = 0; i < 8; i++)
            c[i] = vqmovun_s16(v[i]);
         RH264_FN(neon_tr8x8)(c, r);
         for (i = 0; i < rows; i++)
            vst1_u8(e + (k + i) * ls - 4, r[i]);
         k += rows;
      }
   }
   }
#endif
   for (; k < n; k++)
      RH264_FN(filter_luma_edge)(e + k * ls, s, bS, a, be, tc0v, bd);
}

/* Filter one chroma sample across an edge. */
static void RH264_FN(filter_chroma_edge)(RH264_PEL *e,int s,int bS,int a,int be,int tc0v, int bd)
{
   int p1=e[-2*s],p0=e[-1*s],q0=e[0],q1=e[1*s];
   if(RH264_ABS(p0-q0)>=a||RH264_ABS(p1-p0)>=be||RH264_ABS(q1-q0)>=be) return;
   if(bS<4){
      int tc=tc0v+1, d=((q0-p0)*4+(p1-q1)+4)>>3;
      if(d<-tc)d=-tc; else if(d>tc)d=tc;
      e[-1*s]=(RH264_PEL)RH264_CLIP(p0+d);
      e[0]   =(RH264_PEL)RH264_CLIP(q0-d);
   } else {
      e[-1*s]=(RH264_PEL)((2*p1+p0+q1+2)>>2);
      e[0]   =(RH264_PEL)((2*q1+q0+p1+2)>>2);
   }
}

/* Filter two adjacent 4-lane luma segments of one edge in a single
 * pass. Both segments must be active and classify the same way
 * (bS < 4 or bS == 4); tc0 may differ per segment and rides in lane
 * halves. Halves the transpose work on vertical edges. */
static void RH264_FN(filter_luma_edge_pair)(RH264_PEL *e, int s, int ls,
      int bS0, int t00, int bS1, int t01, int a, int be, int bd)
{
   int k = 0;
#if RH264_BD == 8 && defined(RH264_SSE2)
   {
      const __m128i vt = _mm_set_epi16((short)t01, (short)t01, (short)t01,
            (short)t01, (short)t00, (short)t00, (short)t00, (short)t00);
      if (ls == 1)
      {
         const __m128i vz = _mm_setzero_si128();
         __m128i v[8];
         int i;
         for (i = 0; i < 8; i++)
            v[i] = _mm_unpacklo_epi8(_mm_loadl_epi64(
                  (const __m128i*)(e + (i - 4) * s)), vz);
         RH264_FN(sse2_luma_kernel)(v, bS0, a, be, vt);
         for (i = 1; i < 7; i++)
            _mm_storel_epi64((__m128i*)(e + (i - 4) * s),
                  _mm_packus_epi16(v[i], v[i]));
         k = 8;
      }
      else if (s == 1)
      {
         const __m128i vz = _mm_setzero_si128();
         __m128i r[8], c[8], v[8];
         int i;
         for (i = 0; i < 8; i++)
            r[i] = _mm_loadl_epi64((const __m128i*)(e + i * ls - 4));
         RH264_FN(sse2_tr8x8)(r, c);
         for (i = 0; i < 8; i++)
            v[i] = _mm_unpacklo_epi8(c[i], vz);
         RH264_FN(sse2_luma_kernel)(v, bS0, a, be, vt);
         for (i = 0; i < 8; i++)
            c[i] = _mm_packus_epi16(v[i], v[i]);
         RH264_FN(sse2_tr8x8)(c, r);
         for (i = 0; i < 8; i++)
            _mm_storel_epi64((__m128i*)(e + i * ls - 4), r[i]);
         k = 8;
      }
   }
#elif RH264_BD == 8 && defined(RH264_NEON)
   {
      const int16x8_t vt = vcombine_s16(vdup_n_s16((int16_t)t00),
            vdup_n_s16((int16_t)t01));
      if (ls == 1)
      {
         int16x8_t v[8];
         int i;
         for (i = 0; i < 8; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(e + (i - 4) * s)));
         RH264_FN(neon_luma_kernel)(v, bS0, a, be, vt);
         for (i = 1; i < 7; i++)
            vst1_u8(e + (i - 4) * s, vqmovun_s16(v[i]));
         k = 8;
      }
      else if (s == 1)
      {
         uint8x8_t r[8], c[8];
         int16x8_t v[8];
         int i;
         for (i = 0; i < 8; i++)
            r[i] = vld1_u8(e + i * ls - 4);
         RH264_FN(neon_tr8x8)(r, c);
         for (i = 0; i < 8; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(c[i]));
         RH264_FN(neon_luma_kernel)(v, bS0, a, be, vt);
         for (i = 0; i < 8; i++)
            c[i] = vqmovun_s16(v[i]);
         RH264_FN(neon_tr8x8)(c, r);
         for (i = 0; i < 8; i++)
            vst1_u8(e + i * ls - 4, r[i]);
         k = 8;
      }
   }
#endif
   for (; k < 8; k++)
      RH264_FN(filter_luma_edge)(e + k * ls, s, k < 4 ? bS0 : bS1, a, be,
            k < 4 ? t00 : t01, bd);
   (void)bS1;
}

/* Filter n lanes of a chroma edge, laid out as for the luma variant. */
static void RH264_FN(filter_chroma_edge_n)(RH264_PEL *e, int s, int ls, int n,
      int bS, int a, int be, int tc0v, int bd)
{
   int k = 0;
#if RH264_BD == 8 && defined(RH264_SSE2)
   if (ls == 1)
   {
      const __m128i vz = _mm_setzero_si128();
      for (; k + 8 <= n; k += 8)
      {
         __m128i v[4];
         int i;
         for (i = 0; i < 4; i++)
            v[i] = _mm_unpacklo_epi8(_mm_loadl_epi64(
                  (const __m128i*)(e + k + (i - 2) * s)), vz);
         RH264_FN(sse2_chroma_kernel)(v, bS, a, be, tc0v);
         _mm_storel_epi64((__m128i*)(e + k - s), _mm_packus_epi16(v[1], v[1]));
         _mm_storel_epi64((__m128i*)(e + k),     _mm_packus_epi16(v[2], v[2]));
      }
      if (k + 4 <= n)
      {
         __m128i v[4];
         int i;
         for (i = 0; i < 4; i++)
            v[i] = _mm_unpacklo_epi8(RH264_FN(sse2_load4)(e + k + (i - 2) * s), vz);
         RH264_FN(sse2_chroma_kernel)(v, bS, a, be, tc0v);
         RH264_FN(sse2_store4)(e + k - s, _mm_packus_epi16(v[1], v[1]));
         RH264_FN(sse2_store4)(e + k,     _mm_packus_epi16(v[2], v[2]));
         k += 4;
      }
   }
   else if (s == 1)
   {
      const __m128i vz = _mm_setzero_si128();
      for (; k + 4 <= n; )
      {
         int rows = (k + 8 <= n) ? 8 : 4, i;
         __m128i r[8], c[8], v[4];
         for (i = 0; i < rows; i++)
            r[i] = RH264_FN(sse2_load4)(e + (k + i) * ls - 2);
         for (; i < 8; i++) r[i] = vz;
         RH264_FN(sse2_tr8x8)(r, c);
         for (i = 0; i < 4; i++)
            v[i] = _mm_unpacklo_epi8(c[i], vz);
         RH264_FN(sse2_chroma_kernel)(v, bS, a, be, tc0v);
         for (i = 0; i < 4; i++)
            c[i] = _mm_packus_epi16(v[i], v[i]);
         for (; i < 8; i++) c[i] = vz;
         RH264_FN(sse2_tr8x8)(c, r);
         for (i = 0; i < rows; i++)
            RH264_FN(sse2_store4)(e + (k + i) * ls - 2, r[i]);
         k += rows;
      }
   }
#elif RH264_BD == 8 && defined(RH264_NEON)
   if (ls == 1)
   {
      for (; k + 8 <= n; k += 8)
      {
         int16x8_t v[4];
         int i;
         for (i = 0; i < 4; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(
                  vld1_u8(e + k + (i - 2) * s)));
         RH264_FN(neon_chroma_kernel)(v, bS, a, be, tc0v);
         vst1_u8(e + k - s, vqmovun_s16(v[1]));
         vst1_u8(e + k,     vqmovun_s16(v[2]));
      }
      if (k + 4 <= n)
      {
         int16x8_t v[4];
         int i;
         for (i = 0; i < 4; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(
                  RH264_FN(neon_load4)(e + k + (i - 2) * s)));
         RH264_FN(neon_chroma_kernel)(v, bS, a, be, tc0v);
         RH264_FN(neon_store4)(e + k - s, vqmovun_s16(v[1]));
         RH264_FN(neon_store4)(e + k,     vqmovun_s16(v[2]));
         k += 4;
      }
   }
   else if (s == 1)
   {
      for (; k + 4 <= n; )
      {
         int rows = (k + 8 <= n) ? 8 : 4, i;
         uint8x8_t r[8], c[8];
         int16x8_t v[4];
         for (i = 0; i < rows; i++)
            r[i] = RH264_FN(neon_load4)(e + (k + i) * ls - 2);
         for (; i < 8; i++) r[i] = vdup_n_u8(0);
         RH264_FN(neon_tr8x8)(r, c);
         for (i = 0; i < 4; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(c[i]));
         RH264_FN(neon_chroma_kernel)(v, bS, a, be, tc0v);
         for (i = 0; i < 4; i++)
            c[i] = vqmovun_s16(v[i]);
         for (; i < 8; i++) c[i] = vdup_n_u8(0);
         RH264_FN(neon_tr8x8)(c, r);
         for (i = 0; i < rows; i++)
            RH264_FN(neon_store4)(e + (k + i) * ls - 2, r[i]);
         k += rows;
      }
   }
#endif
   for (; k < n; k++)
      RH264_FN(filter_chroma_edge)(e + k * ls, s, bS, a, be, tc0v, bd);
}

/* Per-segment chroma edge filter: the whole chroma edge of a macroblock
 * in one pass, lanes carrying each 4-sample luma segment's own strength
 * class and tc0 (bS[4]/tc[4], lane k of n belongs to segment k*4/n).
 * The P/B derivation gives every segment its own strength, which the
 * scalar per-sample walk this replaces paid for one pixel at a time;
 * here both filter branches are computed vector-wide and blended per
 * lane, inactive lanes keeping their samples. */
#if RH264_BD == 8 && defined(RH264_SSE2)
static void RH264_FN(sse2_chroma_kernel_seg)(__m128i v[4], __m128i vt,
      __m128i vnorm, __m128i vstrong, int a, int be)
{
   __m128i p1 = v[0], p0 = v[1], q0 = v[2], q1 = v[3];
   __m128i va  = _mm_set1_epi16((short)a);
   __m128i vbe = _mm_set1_epi16((short)be);
   __m128i filt = _mm_and_si128(
         _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(p0, q0)), va),
         _mm_and_si128(
         _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(p1, p0)), vbe),
         _mm_cmplt_epi16(RH264_SSE2_ABS16(_mm_sub_epi16(q1, q0)), vbe)));
   __m128i mn = _mm_and_si128(filt, vnorm);
   __m128i ms = _mm_and_si128(filt, vstrong);
   {
      __m128i tc = _mm_add_epi16(vt, _mm_set1_epi16(1));
      __m128i d = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(_mm_sub_epi16(q0, p0), 2),
            _mm_sub_epi16(p1, q1)), _mm_set1_epi16(4)), 3);
      __m128i np, nq;
      d = _mm_max_epi16(_mm_min_epi16(d, tc),
            _mm_sub_epi16(_mm_setzero_si128(), tc));
      np = _mm_max_epi16(_mm_min_epi16(_mm_add_epi16(p0, d),
            _mm_set1_epi16(RH264_PMAX)), _mm_setzero_si128());
      nq = _mm_max_epi16(_mm_min_epi16(_mm_sub_epi16(q0, d),
            _mm_set1_epi16(RH264_PMAX)), _mm_setzero_si128());
      v[1] = RH264_SSE2_SEL(mn, np, p0);
      v[2] = RH264_SSE2_SEL(mn, nq, q0);
   }
   {
      __m128i two = _mm_set1_epi16(2);
      __m128i np = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(p1, 1), _mm_add_epi16(p0, q1)), two), 2);
      __m128i nq = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(
            _mm_slli_epi16(q1, 1), _mm_add_epi16(q0, p1)), two), 2);
      v[1] = RH264_SSE2_SEL(ms, np, v[1]);
      v[2] = RH264_SSE2_SEL(ms, nq, v[2]);
   }
}
#elif RH264_BD == 8 && defined(RH264_NEON)
static void RH264_FN(neon_chroma_kernel_seg)(int16x8_t v[4], int16x8_t vt,
      uint16x8_t vnorm, uint16x8_t vstrong, int a, int be)
{
   int16x8_t p1 = v[0], p0 = v[1], q0 = v[2], q1 = v[3];
   int16x8_t va  = vdupq_n_s16((int16_t)a);
   int16x8_t vbe = vdupq_n_s16((int16_t)be);
   uint16x8_t filt = vandq_u16(
         vcltq_s16(vabsq_s16(vsubq_s16(p0, q0)), va), vandq_u16(
         vcltq_s16(vabsq_s16(vsubq_s16(p1, p0)), vbe),
         vcltq_s16(vabsq_s16(vsubq_s16(q1, q0)), vbe)));
   uint16x8_t mn = vandq_u16(filt, vnorm);
   uint16x8_t ms = vandq_u16(filt, vstrong);
   {
      int16x8_t tc = vaddq_s16(vt, vdupq_n_s16(1));
      int16x8_t d = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(vsubq_s16(q0, p0), 2),
            vsubq_s16(p1, q1)), vdupq_n_s16(4)), 3);
      int16x8_t np, nq;
      d = vmaxq_s16(vminq_s16(d, tc), vnegq_s16(tc));
      np = vmaxq_s16(vminq_s16(vaddq_s16(p0, d), vdupq_n_s16(RH264_PMAX)),
            vdupq_n_s16(0));
      nq = vmaxq_s16(vminq_s16(vsubq_s16(q0, d), vdupq_n_s16(RH264_PMAX)),
            vdupq_n_s16(0));
      v[1] = RH264_NEON_SEL(mn, np, p0);
      v[2] = RH264_NEON_SEL(mn, nq, q0);
   }
   {
      int16x8_t two = vdupq_n_s16(2);
      int16x8_t np = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(p1, 1), vaddq_s16(p0, q1)), two), 2);
      int16x8_t nq = vshrq_n_s16(vaddq_s16(vaddq_s16(
            vshlq_n_s16(q1, 1), vaddq_s16(q0, p1)), two), 2);
      v[1] = RH264_NEON_SEL(ms, np, v[1]);
      v[2] = RH264_NEON_SEL(ms, nq, v[2]);
   }
}
#endif

static void RH264_FN(filter_chroma_edge_seg)(RH264_PEL *e, int s, int ls, int n,
      const int *bS, const int *tc, int a, int be, int bd)
{
   int k = 0;
#if RH264_BD == 8 && (defined(RH264_SSE2) || defined(RH264_NEON))
   short lt[16], lnm[16], lst[16];
   {
      int i;
      for (i = 0; i < n; i++)
      {
         int sg = (i * 4) / n;
         lt[i]  = (short)tc[sg];
         lnm[i] = (short)((bS[sg] && bS[sg] < 4) ? -1 : 0);
         lst[i] = (short)((bS[sg] == 4) ? -1 : 0);
      }
   }
#endif
#if RH264_BD == 8 && defined(RH264_SSE2)
   if (ls == 1)
   {
      const __m128i vz = _mm_setzero_si128();
      for (; k + 8 <= n; k += 8)
      {
         __m128i v[4];
         int i;
         for (i = 0; i < 4; i++)
            v[i] = _mm_unpacklo_epi8(_mm_loadl_epi64(
                  (const __m128i*)(e + k + (i - 2) * s)), vz);
         RH264_FN(sse2_chroma_kernel_seg)(v,
               _mm_loadu_si128((const __m128i*)(lt + k)),
               _mm_loadu_si128((const __m128i*)(lnm + k)),
               _mm_loadu_si128((const __m128i*)(lst + k)), a, be);
         _mm_storel_epi64((__m128i*)(e + k - s), _mm_packus_epi16(v[1], v[1]));
         _mm_storel_epi64((__m128i*)(e + k),     _mm_packus_epi16(v[2], v[2]));
      }
   }
   else if (s == 1)
   {
      const __m128i vz = _mm_setzero_si128();
      for (; k + 8 <= n; k += 8)
      {
         __m128i r[8], c[8], v[4];
         int i;
         for (i = 0; i < 8; i++)
            r[i] = RH264_FN(sse2_load4)(e + (k + i) * ls - 2);
         RH264_FN(sse2_tr8x8)(r, c);
         for (i = 0; i < 4; i++)
            v[i] = _mm_unpacklo_epi8(c[i], vz);
         RH264_FN(sse2_chroma_kernel_seg)(v,
               _mm_loadu_si128((const __m128i*)(lt + k)),
               _mm_loadu_si128((const __m128i*)(lnm + k)),
               _mm_loadu_si128((const __m128i*)(lst + k)), a, be);
         /* only p0/q0 change: interleave the two filtered columns into
          * per-row byte pairs and store two bytes a row, instead of
          * transposing all four columns back */
         {
            RH264_PEL pr[16];
            _mm_storeu_si128((__m128i*)pr, _mm_unpacklo_epi8(
                  _mm_packus_epi16(v[1], v[1]),
                  _mm_packus_epi16(v[2], v[2])));
            for (i = 0; i < 8; i++)
               memcpy(e + (k + i) * ls - 1, pr + 2 * i, 2);
         }
      }
   }
#elif RH264_BD == 8 && defined(RH264_NEON)
   if (ls == 1)
   {
      for (; k + 8 <= n; k += 8)
      {
         int16x8_t v[4];
         int i;
         for (i = 0; i < 4; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(
                  vld1_u8(e + k + (i - 2) * s)));
         RH264_FN(neon_chroma_kernel_seg)(v, vld1q_s16(lt + k),
               vreinterpretq_u16_s16(vld1q_s16(lnm + k)),
               vreinterpretq_u16_s16(vld1q_s16(lst + k)), a, be);
         vst1_u8(e + k - s, vqmovun_s16(v[1]));
         vst1_u8(e + k,     vqmovun_s16(v[2]));
      }
   }
   else if (s == 1)
   {
      for (; k + 8 <= n; k += 8)
      {
         uint8x8_t r[8], c[8];
         int16x8_t v[4];
         int i;
         for (i = 0; i < 8; i++)
            r[i] = RH264_FN(neon_load4)(e + (k + i) * ls - 2);
         RH264_FN(neon_tr8x8)(r, c);
         for (i = 0; i < 4; i++)
            v[i] = vreinterpretq_s16_u16(vmovl_u8(c[i]));
         RH264_FN(neon_chroma_kernel_seg)(v, vld1q_s16(lt + k),
               vreinterpretq_u16_s16(vld1q_s16(lnm + k)),
               vreinterpretq_u16_s16(vld1q_s16(lst + k)), a, be);
         /* only p0/q0 change: store the two filtered columns as
          * per-row byte pairs instead of transposing back */
         {
            RH264_PEL pr[16];
            uint8x8x2_t z;
            z.val[0] = vqmovun_s16(v[1]);
            z.val[1] = vqmovun_s16(v[2]);
            vst2_u8(pr, z);
            for (i = 0; i < 8; i++)
               memcpy(e + (k + i) * ls - 1, pr + 2 * i, 2);
         }
      }
   }
#endif
   for (; k < n; k++)
   {
      int sg = (k * 4) / n;
      if (bS[sg])
         RH264_FN(filter_chroma_edge)(e + k * ls, s, bS[sg], a, be, tc[sg], bd);
   }
}

static void RH264_FN(tb_copy)(RH264_PEL *dst, int dstride, const RH264_PEL *src,
      int sstride, int w, int h)
{
   int y;
   for (y = 0; y < h; y++)
      memcpy(dst + y*dstride, src + y*sstride, (size_t)w * sizeof(RH264_PEL));
}

/* A w x h window of the reference at (x0,y0), coordinates clamped into
 * the picture (8.4.2.2), a row at a time: the row index clamped once,
 * then the columns left of the picture repeating its first sample, the
 * ones inside copied, the ones right of it repeating its last. */
static void RH264_FN(mc_edge)(RH264_PEL *dst, int dstride, int w, int h,
      const RH264_PEL *ref, int stride, int rw, int rh, int x0, int y0)
{
   int r;
   int lo = x0 < 0 ? -x0 : 0;               /* columns left of 0  */
   int hi = rw - x0;                        /* first column past  */
   if (lo > w) lo = w;
   if (hi > w) hi = w;
   if (hi < lo) hi = lo;
   for (r = 0; r < h; r++)
   {
      int y = RH264_MC_CLAMP(y0 + r, rh), c;
      const RH264_PEL *row = ref + (size_t)y * stride;
      RH264_PEL *d = dst + (size_t)r * dstride;
      RH264_PEL first = row[0], last = row[rw - 1];
      for (c = 0; c < lo; c++)
         d[c] = first;
      if (hi > lo)
         memcpy(d + lo, row + x0 + lo, (size_t)(hi - lo) * sizeof(RH264_PEL));
      for (c = hi; c < w; c++)
         d[c] = last;
   }
}

/* 6-tap filter kernel: (E - 5F + 20G + 20H - 5I + J). */
static RH264_INLINE int RH264_FN(tap6)(int a, int b, int c, int d, int e, int f)
{
   return a - 5 * b + 20 * c + 20 * d - 5 * e + f;
}

/* Luma motion compensation for a bw x bh block at integer position (bx,by)
 * within the destination, with a quarter-pel MV (mvx,mvy) in 1/4 units.
 * Writes bw x bh predicted samples to dst (dstride). ref is the full ref
 * plane (rw x rh, stride rstride). (ox,oy) is the block's integer luma
 * position in the picture. */
#if RH264_BD == 8 && defined(RH264_SSE2)
/* Raw 6-tap (1,-5,20,20,-5,1) for eight consecutive positions; p points at
 * the first tap (position minus two). Fits int16: range -RH264_PMAX0..10710. */
static RH264_INLINE __m128i RH264_FN(sse2_tap6_u8)(const RH264_PEL *p0,
      const RH264_PEL *p1, const RH264_PEL *p2, const RH264_PEL *p3,
      const RH264_PEL *p4, const RH264_PEL *p5)
{
   const __m128i vz = _mm_setzero_si128();
   __m128i a = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)p0), vz);
   __m128i b = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)p1), vz);
   __m128i c = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)p2), vz);
   __m128i d = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)p3), vz);
   __m128i e = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)p4), vz);
   __m128i f = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)p5), vz);
   __m128i s05 = _mm_add_epi16(a, f);
   __m128i s14 = _mm_add_epi16(b, e);
   __m128i s23 = _mm_add_epi16(c, d);
   return _mm_sub_epi16(
         _mm_add_epi16(s05, _mm_mullo_epi16(s23, _mm_set1_epi16(20))),
         _mm_mullo_epi16(s14, _mm_set1_epi16(5)));
}

/* Round a raw 6-tap to the half-pel byte: clip((t + 16) >> 5). */
static RH264_INLINE __m128i RH264_FN(sse2_hp8)(__m128i t)
{
   t = _mm_srai_epi16(_mm_add_epi16(t, _mm_set1_epi16(16)), 5);
   return _mm_packus_epi16(t, t);
}

/* Second-stage 6-tap over int16 rows of raw taps, for eight columns:
 * clip((tap6 + 512) >> 10) as bytes. */
static RH264_INLINE __m128i RH264_FN(sse2_j8)(const int16_t *r0, const int16_t *r1,
      const int16_t *r2, const int16_t *r3, const int16_t *r4,
      const int16_t *r5)
{
   __m128i a = _mm_loadu_si128((const __m128i*)r0);
   __m128i b = _mm_loadu_si128((const __m128i*)r1);
   __m128i c = _mm_loadu_si128((const __m128i*)r2);
   __m128i d = _mm_loadu_si128((const __m128i*)r3);
   __m128i e = _mm_loadu_si128((const __m128i*)r4);
   __m128i f = _mm_loadu_si128((const __m128i*)r5);
   __m128i lo, hi;
   {
      __m128i s05l = _mm_add_epi32(_mm_srai_epi32(_mm_unpacklo_epi16(a, a), 16),
                                   _mm_srai_epi32(_mm_unpacklo_epi16(f, f), 16));
      __m128i s14l = _mm_add_epi32(_mm_srai_epi32(_mm_unpacklo_epi16(b, b), 16),
                                   _mm_srai_epi32(_mm_unpacklo_epi16(e, e), 16));
      __m128i s23l = _mm_add_epi32(_mm_srai_epi32(_mm_unpacklo_epi16(c, c), 16),
                                   _mm_srai_epi32(_mm_unpacklo_epi16(d, d), 16));
      lo = _mm_sub_epi32(_mm_add_epi32(s05l,
               _mm_add_epi32(_mm_slli_epi32(s23l, 4), _mm_slli_epi32(s23l, 2))),
            _mm_add_epi32(_mm_slli_epi32(s14l, 2), s14l));
      lo = _mm_srai_epi32(_mm_add_epi32(lo, _mm_set1_epi32(512)), 10);
   }
   {
      __m128i s05h = _mm_add_epi32(_mm_srai_epi32(_mm_unpackhi_epi16(a, a), 16),
                                   _mm_srai_epi32(_mm_unpackhi_epi16(f, f), 16));
      __m128i s14h = _mm_add_epi32(_mm_srai_epi32(_mm_unpackhi_epi16(b, b), 16),
                                   _mm_srai_epi32(_mm_unpackhi_epi16(e, e), 16));
      __m128i s23h = _mm_add_epi32(_mm_srai_epi32(_mm_unpackhi_epi16(c, c), 16),
                                   _mm_srai_epi32(_mm_unpackhi_epi16(d, d), 16));
      hi = _mm_sub_epi32(_mm_add_epi32(s05h,
               _mm_add_epi32(_mm_slli_epi32(s23h, 4), _mm_slli_epi32(s23h, 2))),
            _mm_add_epi32(_mm_slli_epi32(s14h, 2), s14h));
      hi = _mm_srai_epi32(_mm_add_epi32(hi, _mm_set1_epi32(512)), 10);
   }
   {
      __m128i w = _mm_packs_epi32(lo, hi);
      return _mm_packus_epi16(w, w);
   }
}
#endif


#if RH264_BD == 8 && defined(RH264_NEON)
/* Raw 6-tap (1,-5,20,20,-5,1) for eight consecutive positions; the value
 * range -RH264_PMAX0..10710 fits int16. */
static RH264_INLINE int16x8_t RH264_FN(neon_tap6_u8)(const RH264_PEL *p0,
      const RH264_PEL *p1, const RH264_PEL *p2, const RH264_PEL *p3,
      const RH264_PEL *p4, const RH264_PEL *p5)
{
   int16x8_t s05 = vreinterpretq_s16_u16(vaddl_u8(vld1_u8(p0), vld1_u8(p5)));
   int16x8_t s14 = vreinterpretq_s16_u16(vaddl_u8(vld1_u8(p1), vld1_u8(p4)));
   int16x8_t s23 = vreinterpretq_s16_u16(vaddl_u8(vld1_u8(p2), vld1_u8(p3)));
   return vmlsq_n_s16(vmlaq_n_s16(s05, s23, 20), s14, 5);
}

/* Round a raw 6-tap to the half-pel byte: clip((t + 16) >> 5), which is
 * exactly the saturating rounding narrow. */
static RH264_INLINE uint8x8_t RH264_FN(neon_hp8)(int16x8_t t)
{
   return vqrshrun_n_s16(t, 5);
}

/* Second-stage 6-tap over int16 rows of raw taps: clip((tap6 + 512) >> 10). */
static RH264_INLINE uint8x8_t RH264_FN(neon_j8)(const int16_t *r0,
      const int16_t *r1, const int16_t *r2, const int16_t *r3,
      const int16_t *r4, const int16_t *r5)
{
   int16x8_t a = vld1q_s16(r0), b = vld1q_s16(r1), c = vld1q_s16(r2);
   int16x8_t d = vld1q_s16(r3), e = vld1q_s16(r4), f = vld1q_s16(r5);
   int32x4_t lo = vmlsq_n_s32(vmlaq_n_s32(
         vaddl_s16(vget_low_s16(a), vget_low_s16(f)),
         vaddl_s16(vget_low_s16(c), vget_low_s16(d)), 20),
         vaddl_s16(vget_low_s16(b), vget_low_s16(e)), 5);
   int32x4_t hi = vmlsq_n_s32(vmlaq_n_s32(
         vaddl_s16(vget_high_s16(a), vget_high_s16(f)),
         vaddl_s16(vget_high_s16(c), vget_high_s16(d)), 20),
         vaddl_s16(vget_high_s16(b), vget_high_s16(e)), 5);
   return vqmovun_s16(vcombine_s16(vqrshrn_n_s32(lo, 10),
         vqrshrn_n_s32(hi, 10)));
}
#endif

/* 'scratch' (4 KiB, the frame's) holds the reference patch and the
 * intermediate taps at 16 bits, where they outgrow the console thread
 * stacks; the 8-bit instantiation keeps them local. */
static void RH264_FN(mc_luma)(RH264_PEL *dst, int dstride,
      const RH264_PEL *ref, int rstride, int rw, int rh,
      int ox, int oy, int bw, int bh, int mvx, int mvy, int bd,
      void *scratch)
{
   int fx = mvx & 3, fy = mvy & 3;
   int ix = ox + (mvx >> 2);
   int iy = oy + (mvy >> 2);
   int x, y;
   /* The interpolation window is the block plus two samples of margin on
    * the top/left and three on the bottom/right (8.4.2.2.1). Gathering it
    * once with the edge clamp baked in lets every filter below run over a
    * dense patch instead of re-clamping per tap. */
#if RH264_BD == 8
   RH264_PEL pat[21 * 21];
#else
   RH264_PEL *pat = (RH264_PEL*)scratch;
#endif
   int pw = bw + 5, ph = bh + 5, r, c;
   const RH264_PEL *pc;
   (void)scratch;

   if (fx == 0 && fy == 0)
   {
      /* full-pel: straight copy */
      if (ix >= 0 && iy >= 0 && ix + bw <= rw && iy + bh <= rh)
      {
         for (y = 0; y < bh; y++)
            memcpy(dst + y * dstride, ref + (iy + y) * rstride + ix, (size_t)bw * sizeof(RH264_PEL));
      }
      else
         RH264_FN(mc_edge)(dst, dstride, bw, bh, ref, rstride, rw, rh,
               ix, iy);
      return;
   }

   /* When the margined window lies inside the picture the filters can
    * run straight off the reference plane - the patch only exists to
    * bake the edge clamp in, so it is gathered solely at the borders. */
   if (ix >= 2 && iy >= 2 && ix + bw + 3 <= rw && iy + bh + 3 < rh)
   {
      /* strictly above the last row: the vector tails read a few bytes
       * past the 6-tap margin, and the spare row keeps them inside the
       * plane */
      pw = rstride;
      pc = ref + iy * rstride + ix;
   }
   else
   {
      RH264_FN(mc_edge)(pat, pw, pw, ph, ref, rstride, rw, rh,
            ix - 2, iy - 2);
      pc = pat + 2 * pw + 2;                /* patch centre (block origin) */
   }

#define PP(dx,dy) ((int)pc[(y + (dy)) * pw + x + (dx)])
#define HTAP(px,py) RH264_FN(tap6)((int)pc[(py)*pw+(px)-2],(int)pc[(py)*pw+(px)-1],\
      (int)pc[(py)*pw+(px)],(int)pc[(py)*pw+(px)+1],\
      (int)pc[(py)*pw+(px)+2],(int)pc[(py)*pw+(px)+3])
#define VTAP(px,py) RH264_FN(tap6)((int)pc[((py)-2)*pw+(px)],(int)pc[((py)-1)*pw+(px)],\
      (int)pc[(py)*pw+(px)],(int)pc[((py)+1)*pw+(px)],\
      (int)pc[((py)+2)*pw+(px)],(int)pc[((py)+3)*pw+(px)])

   if (fy == 0)
   {
      /* b half-pels and the a/c quarter-pels beside them */
      for (y = 0; y < bh; y++)
      {
         x = 0;
#if RH264_BD == 8 && defined(RH264_SSE2)
         for (; x + 8 <= bw; x += 8)
         {
            const RH264_PEL *rp = pc + y * pw + x - 2;
            __m128i b8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(rp, rp + 1,
                  rp + 2, rp + 3, rp + 4, rp + 5));
            if (fx != 2)
               b8 = _mm_avg_epu8(b8, _mm_loadl_epi64((const __m128i*)
                     (pc + y * pw + x + (fx == 3))));
            _mm_storel_epi64((__m128i*)(dst + y * dstride + x), b8);
         }
         if (x + 4 <= bw)
         {
            const RH264_PEL *rp = pc + y * pw + x - 2;
            __m128i b8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(rp, rp + 1,
                  rp + 2, rp + 3, rp + 4, rp + 5));
            if (fx != 2)
               b8 = _mm_avg_epu8(b8, _mm_loadl_epi64((const __m128i*)
                     (pc + y * pw + x + (fx == 3))));
            RH264_FN(sse2_store4)(dst + y * dstride + x, b8);
            x += 4;
         }
#elif RH264_BD == 8 && defined(RH264_NEON)
         for (; x + 8 <= bw; x += 8)
         {
            const RH264_PEL *rp = pc + y * pw + x - 2;
            uint8x8_t b8 = RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(rp, rp + 1,
                  rp + 2, rp + 3, rp + 4, rp + 5));
            if (fx != 2)
               b8 = vrhadd_u8(b8, vld1_u8(pc + y * pw + x + (fx == 3)));
            vst1_u8(dst + y * dstride + x, b8);
         }
         if (x + 4 <= bw)
         {
            const RH264_PEL *rp = pc + y * pw + x - 2;
            uint8x8_t b8 = RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(rp, rp + 1,
                  rp + 2, rp + 3, rp + 4, rp + 5));
            if (fx != 2)
               b8 = vrhadd_u8(b8, vld1_u8(pc + y * pw + x + (fx == 3)));
            RH264_FN(neon_store4)(dst + y * dstride + x, b8);
            x += 4;
         }
#endif
         for (; x < bw; x++)
         {
            int b = RH264_CLIP((HTAP(x, y) + 16) >> 5);
            int val;
            if (fx == 2)      val = b;
            else if (fx == 1) val = (b + PP(0,0) + 1) >> 1;
            else              val = (b + PP(1,0) + 1) >> 1;
            dst[y * dstride + x] = (RH264_PEL)val;
         }
      }
   }
   else if (fx == 0)
   {
      /* h half-pels and the d/n quarter-pels beside them */
      for (y = 0; y < bh; y++)
      {
         x = 0;
#if RH264_BD == 8 && defined(RH264_SSE2)
         for (; x + 8 <= bw; x += 8)
         {
            const RH264_PEL *cp = pc + (y - 2) * pw + x;
            __m128i h8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(cp, cp + pw,
                  cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
            if (fy != 2)
               h8 = _mm_avg_epu8(h8, _mm_loadl_epi64((const __m128i*)
                     (pc + (y + (fy == 3)) * pw + x)));
            _mm_storel_epi64((__m128i*)(dst + y * dstride + x), h8);
         }
         if (x + 4 <= bw)
         {
            const RH264_PEL *cp = pc + (y - 2) * pw + x;
            __m128i h8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(cp, cp + pw,
                  cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
            if (fy != 2)
               h8 = _mm_avg_epu8(h8, _mm_loadl_epi64((const __m128i*)
                     (pc + (y + (fy == 3)) * pw + x)));
            RH264_FN(sse2_store4)(dst + y * dstride + x, h8);
            x += 4;
         }
#elif RH264_BD == 8 && defined(RH264_NEON)
         for (; x + 8 <= bw; x += 8)
         {
            const RH264_PEL *cp = pc + (y - 2) * pw + x;
            uint8x8_t h8 = RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(cp, cp + pw,
                  cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
            if (fy != 2)
               h8 = vrhadd_u8(h8, vld1_u8(pc + (y + (fy == 3)) * pw + x));
            vst1_u8(dst + y * dstride + x, h8);
         }
         if (x + 4 <= bw)
         {
            const RH264_PEL *cp = pc + (y - 2) * pw + x;
            uint8x8_t h8 = RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(cp, cp + pw,
                  cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
            if (fy != 2)
               h8 = vrhadd_u8(h8, vld1_u8(pc + (y + (fy == 3)) * pw + x));
            RH264_FN(neon_store4)(dst + y * dstride + x, h8);
            x += 4;
         }
#endif
         for (; x < bw; x++)
         {
            int hh = RH264_CLIP((VTAP(x, y) + 16) >> 5);
            int val;
            if (fy == 2)      val = hh;
            else if (fy == 1) val = (hh + PP(0,0) + 1) >> 1;
            else              val = (hh + PP(0,1) + 1) >> 1;
            dst[y * dstride + x] = (RH264_PEL)val;
         }
      }
   }
   else if (fx == 2 || fy == 2)
   {
      /* the centre half-pel j: a vertical 6-tap over rows of raw horizontal
       * 6-taps. One row of taps serves six output rows, so compute the
       * bh+5 rows once. */
#if RH264_BD == 8
      RH264_HB hb[21 * 16];
#else
      RH264_HB *hb = (RH264_HB*)((RH264_PEL*)scratch + 21 * 21 + 1);
#endif
      for (r = 0; r < ph; r++)
      {
         c = 0;
#if RH264_BD == 8 && defined(RH264_SSE2)
         for (; c + 8 <= bw; c += 8)
         {
            const RH264_PEL *rp = pc - 2 * pw - 2 + r * pw + c;
            _mm_storeu_si128((__m128i*)(hb + r * bw + c),
                  RH264_FN(sse2_tap6_u8)(rp, rp + 1, rp + 2, rp + 3, rp + 4,
                        rp + 5));
         }
         if (c + 4 <= bw)
         {
            const RH264_PEL *rp = pc - 2 * pw - 2 + r * pw + c;
            _mm_storel_epi64((__m128i*)(hb + r * bw + c),
                  RH264_FN(sse2_tap6_u8)(rp, rp + 1, rp + 2, rp + 3, rp + 4,
                        rp + 5));
            c += 4;
         }
#elif RH264_BD == 8 && defined(RH264_NEON)
         for (; c + 8 <= bw; c += 8)
         {
            const RH264_PEL *rp = pc - 2 * pw - 2 + r * pw + c;
            vst1q_s16(hb + r * bw + c,
                  RH264_FN(neon_tap6_u8)(rp, rp + 1, rp + 2, rp + 3, rp + 4,
                        rp + 5));
         }
         if (c + 4 <= bw)
         {
            const RH264_PEL *rp = pc - 2 * pw - 2 + r * pw + c;
            vst1_s16(hb + r * bw + c,
                  vget_low_s16(RH264_FN(neon_tap6_u8)(rp, rp + 1, rp + 2,
                        rp + 3, rp + 4, rp + 5)));
            c += 4;
         }
#endif
         for (; c < bw; c++)
         {
            const RH264_PEL *rp = pc - 2 * pw - 2 + r * pw + c;
            hb[r * bw + c] = (RH264_HB)RH264_FN(tap6)(
                  (int)rp[0], (int)rp[1], (int)rp[2],
                  (int)rp[3], (int)rp[4], (int)rp[5]);
         }
      }
      for (y = 0; y < bh; y++)
      {
         x = 0;
#if RH264_BD == 8 && defined(RH264_SSE2)
         for (; x + 8 <= bw; x += 8)
         {
            __m128i j8 = RH264_FN(sse2_j8)(hb + y*bw + x, hb + (y+1)*bw + x,
                  hb + (y+2)*bw + x, hb + (y+3)*bw + x, hb + (y+4)*bw + x,
                  hb + (y+5)*bw + x);
            if (fx == 2 && fy != 2)
            {
               __m128i t = _mm_loadu_si128((const __m128i*)
                     (hb + (y + 2 + (fy == 3)) * bw + x));
               j8 = _mm_avg_epu8(RH264_FN(sse2_hp8)(t), j8);
            }
            else if (fy == 2 && fx != 2)
            {
               const RH264_PEL *cp = pc + (y - 2) * pw + x + (fx == 3);
               __m128i h8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(cp, cp + pw,
                     cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
               j8 = _mm_avg_epu8(h8, j8);
            }
            _mm_storel_epi64((__m128i*)(dst + y * dstride + x), j8);
         }
         if (x + 4 <= bw)
         {
            __m128i j8 = RH264_FN(sse2_j8)(hb + y*bw + x, hb + (y+1)*bw + x,
                  hb + (y+2)*bw + x, hb + (y+3)*bw + x, hb + (y+4)*bw + x,
                  hb + (y+5)*bw + x);
            if (fx == 2 && fy != 2)
            {
               __m128i t = _mm_loadu_si128((const __m128i*)
                     (hb + (y + 2 + (fy == 3)) * bw + x));
               j8 = _mm_avg_epu8(RH264_FN(sse2_hp8)(t), j8);
            }
            else if (fy == 2 && fx != 2)
            {
               const RH264_PEL *cp = pc + (y - 2) * pw + x + (fx == 3);
               __m128i h8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(cp, cp + pw,
                     cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
               j8 = _mm_avg_epu8(h8, j8);
            }
            RH264_FN(sse2_store4)(dst + y * dstride + x, j8);
            x += 4;
         }
#elif RH264_BD == 8 && defined(RH264_NEON)
         for (; x + 8 <= bw; x += 8)
         {
            uint8x8_t j8 = RH264_FN(neon_j8)(hb + y*bw + x, hb + (y+1)*bw + x,
                  hb + (y+2)*bw + x, hb + (y+3)*bw + x, hb + (y+4)*bw + x,
                  hb + (y+5)*bw + x);
            if (fx == 2 && fy != 2)
               j8 = vrhadd_u8(RH264_FN(neon_hp8)(
                     vld1q_s16(hb + (y + 2 + (fy == 3)) * bw + x)), j8);
            else if (fy == 2 && fx != 2)
            {
               const RH264_PEL *cp = pc + (y - 2) * pw + x + (fx == 3);
               j8 = vrhadd_u8(RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(cp, cp + pw,
                     cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw)),
                     j8);
            }
            vst1_u8(dst + y * dstride + x, j8);
         }
         if (x + 4 <= bw)
         {
            uint8x8_t j8 = RH264_FN(neon_j8)(hb + y*bw + x, hb + (y+1)*bw + x,
                  hb + (y+2)*bw + x, hb + (y+3)*bw + x, hb + (y+4)*bw + x,
                  hb + (y+5)*bw + x);
            if (fx == 2 && fy != 2)
               j8 = vrhadd_u8(RH264_FN(neon_hp8)(
                     vld1q_s16(hb + (y + 2 + (fy == 3)) * bw + x)), j8);
            else if (fy == 2 && fx != 2)
            {
               const RH264_PEL *cp = pc + (y - 2) * pw + x + (fx == 3);
               j8 = vrhadd_u8(RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(cp, cp + pw,
                     cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw)),
                     j8);
            }
            RH264_FN(neon_store4)(dst + y * dstride + x, j8);
            x += 4;
         }
#endif
         for (; x < bw; x++)
         {
            int j = RH264_CLIP((RH264_FN(tap6)(hb[y*bw+x], hb[(y+1)*bw+x],
                  hb[(y+2)*bw+x], hb[(y+3)*bw+x], hb[(y+4)*bw+x],
                  hb[(y+5)*bw+x]) + 512) >> 10);
            int val;
            if (fx == 2 && fy == 2)
               val = j;
            else if (fx == 2)
            {
               /* f / q: average with the b half-pel of the nearer row */
               int bc = RH264_CLIP((hb[(y + 2 + (fy == 3)) * bw + x] + 16) >> 5);
               val = (bc + j + 1) >> 1;
            }
            else
            {
               /* i / k: average with the h half-pel of the nearer column */
               int hc = RH264_CLIP((VTAP(x + (fx == 3), y) + 16) >> 5);
               val = (hc + j + 1) >> 1;
            }
            dst[y * dstride + x] = (RH264_PEL)val;
         }
      }
   }
   else
   {
      /* corner quarter-pels e,g,p,r: average the nearer b and h half-pels */
      for (y = 0; y < bh; y++)
      {
         x = 0;
#if RH264_BD == 8 && defined(RH264_SSE2)
         for (; x + 8 <= bw; x += 8)
         {
            const RH264_PEL *rp = pc + (y + (fy == 3)) * pw + x - 2;
            const RH264_PEL *cp = pc + (y - 2) * pw + x + (fx == 3);
            __m128i b8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(rp, rp + 1,
                  rp + 2, rp + 3, rp + 4, rp + 5));
            __m128i h8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(cp, cp + pw,
                  cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
            _mm_storel_epi64((__m128i*)(dst + y * dstride + x),
                  _mm_avg_epu8(b8, h8));
         }
         if (x + 4 <= bw)
         {
            const RH264_PEL *rp = pc + (y + (fy == 3)) * pw + x - 2;
            const RH264_PEL *cp = pc + (y - 2) * pw + x + (fx == 3);
            __m128i b8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(rp, rp + 1,
                  rp + 2, rp + 3, rp + 4, rp + 5));
            __m128i h8 = RH264_FN(sse2_hp8)(RH264_FN(sse2_tap6_u8)(cp, cp + pw,
                  cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
            RH264_FN(sse2_store4)(dst + y * dstride + x, _mm_avg_epu8(b8, h8));
            x += 4;
         }
#elif RH264_BD == 8 && defined(RH264_NEON)
         for (; x + 8 <= bw; x += 8)
         {
            const RH264_PEL *rp = pc + (y + (fy == 3)) * pw + x - 2;
            const RH264_PEL *cp = pc + (y - 2) * pw + x + (fx == 3);
            uint8x8_t b8 = RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(rp, rp + 1,
                  rp + 2, rp + 3, rp + 4, rp + 5));
            uint8x8_t h8 = RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(cp, cp + pw,
                  cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
            vst1_u8(dst + y * dstride + x, vrhadd_u8(b8, h8));
         }
         if (x + 4 <= bw)
         {
            const RH264_PEL *rp = pc + (y + (fy == 3)) * pw + x - 2;
            const RH264_PEL *cp = pc + (y - 2) * pw + x + (fx == 3);
            uint8x8_t b8 = RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(rp, rp + 1,
                  rp + 2, rp + 3, rp + 4, rp + 5));
            uint8x8_t h8 = RH264_FN(neon_hp8)(RH264_FN(neon_tap6_u8)(cp, cp + pw,
                  cp + 2 * pw, cp + 3 * pw, cp + 4 * pw, cp + 5 * pw));
            RH264_FN(neon_store4)(dst + y * dstride + x, vrhadd_u8(b8, h8));
            x += 4;
         }
#endif
         for (; x < bw; x++)
         {
            int bx = RH264_CLIP((HTAP(x, y + (fy == 3)) + 16) >> 5);
            int hy = RH264_CLIP((VTAP(x + (fx == 3), y) + 16) >> 5);
            dst[y * dstride + x] = (RH264_PEL)((bx + hy + 1) >> 1);
         }
      }
   }
#undef PP
#undef HTAP
#undef VTAP
}

/* Chroma 1/8-pel bilinear MC. mv is the LUMA quarter-pel MV; chroma uses
 * mv directly as 1/8-pel offsets (4:2:0 -> chroma mv = luma mv, 1/8 units). */
static void RH264_FN(mc_chroma)(RH264_PEL *dst, int dstride,
      const RH264_PEL *ref, int rstride, int rw, int rh,
      int ox, int oy, int bw, int bh, int mvx, int mvy, int bd)
{
   int ix = ox + (mvx >> 3);
   int iy = oy + (mvy >> 3);
   int fx = mvx & 7, fy = mvy & 7;
   int x, y;
   /* bilinear window: block plus one sample right/below, edge clamp baked */
   /* bilinear window: 4:2:0 chroma blocks are at most 8x8, 4:2:2 ones
    * at most 8x16, and the window is one sample wider and taller */
   RH264_PEL pat[9 * 17];
   const RH264_PEL *pc = pat;
   int pw = bw + 1;
   /* run off the reference plane when the window is in-bounds; the
    * patch is gathered only at the picture borders */
   if (ix >= 0 && iy >= 0 && ix + bw + 1 <= rw && iy + bh + 1 < rh)
   {
      /* strictly above the last row so vector tails past the bilinear
       * window stay inside the plane */
      pw = rstride;
      pc = ref + iy * rstride + ix;
   }
   else
      RH264_FN(mc_edge)(pat, pw, pw, bh + 1, ref, rstride, rw, rh, ix, iy);

   if (fx == 0 && fy == 0)
   {
      for (y = 0; y < bh; y++)
         memcpy(dst + y * dstride, pc + y * pw, (size_t)bw * sizeof(RH264_PEL));
      return;
   }
#if RH264_BD == 8 && defined(RH264_SSE2)
   {
      const __m128i vz  = _mm_setzero_si128();
      const __m128i w00 = _mm_set1_epi16((short)((8 - fx) * (8 - fy)));
      const __m128i w10 = _mm_set1_epi16((short)(fx * (8 - fy)));
      const __m128i w01 = _mm_set1_epi16((short)((8 - fx) * fy));
      const __m128i w11 = _mm_set1_epi16((short)(fx * fy));
      const __m128i rnd = _mm_set1_epi16(32);
      for (y = 0; y < bh; y++)
      {
         const RH264_PEL *r0 = pc + y * pw, *r1 = r0 + pw;
         for (x = 0; x + 8 <= bw; x += 8)
         {
            __m128i a = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(r0 + x)), vz);
            __m128i b = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(r0 + x + 1)), vz);
            __m128i cc2 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(r1 + x)), vz);
            __m128i d = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(r1 + x + 1)), vz);
            __m128i s2 = _mm_add_epi16(_mm_add_epi16(
                     _mm_mullo_epi16(a, w00), _mm_mullo_epi16(b, w10)),
                  _mm_add_epi16(
                     _mm_mullo_epi16(cc2, w01), _mm_mullo_epi16(d, w11)));
            s2 = _mm_srli_epi16(_mm_add_epi16(s2, rnd), 6);
            _mm_storel_epi64((__m128i*)(dst + y * dstride + x),
                  _mm_packus_epi16(s2, s2));
         }
         if (x + 4 <= bw)
         {
            __m128i a = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(r0 + x)), vz);
            __m128i b = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(r0 + x + 1)), vz);
            __m128i cc2 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(r1 + x)), vz);
            __m128i d = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(r1 + x + 1)), vz);
            __m128i s2 = _mm_add_epi16(_mm_add_epi16(
                     _mm_mullo_epi16(a, w00), _mm_mullo_epi16(b, w10)),
                  _mm_add_epi16(
                     _mm_mullo_epi16(cc2, w01), _mm_mullo_epi16(d, w11)));
            s2 = _mm_srli_epi16(_mm_add_epi16(s2, rnd), 6);
            RH264_FN(sse2_store4)(dst + y * dstride + x, _mm_packus_epi16(s2, s2));
            x += 4;
         }
         for (; x < bw; x++)
         {
            int a = r0[x], b = r0[x + 1], c2 = r1[x], d = r1[x + 1];
            dst[y * dstride + x] = (RH264_PEL)(((8 - fx) * (8 - fy) * a
                  + fx * (8 - fy) * b + (8 - fx) * fy * c2
                  + fx * fy * d + 32) >> 6);
         }
      }
      return;
   }
#elif RH264_BD == 8 && defined(RH264_NEON)
   {
      int w00 = (8 - fx) * (8 - fy), w10 = fx * (8 - fy);
      int w01 = (8 - fx) * fy,       w11 = fx * fy;
      for (y = 0; y < bh; y++)
      {
         const RH264_PEL *r0 = pc + y * pw, *r1 = r0 + pw;
         for (x = 0; x + 8 <= bw; x += 8)
         {
            uint16x8_t s = vmull_u8(vld1_u8(r0 + x), vdup_n_u8((RH264_PEL)w00));
            s = vmlal_u8(s, vld1_u8(r0 + x + 1), vdup_n_u8((RH264_PEL)w10));
            s = vmlal_u8(s, vld1_u8(r1 + x),     vdup_n_u8((RH264_PEL)w01));
            s = vmlal_u8(s, vld1_u8(r1 + x + 1), vdup_n_u8((RH264_PEL)w11));
            vst1_u8(dst + y * dstride + x, vrshrn_n_u16(s, 6));
         }
         if (x + 4 <= bw)
         {
            uint16x8_t s = vmull_u8(vld1_u8(r0 + x), vdup_n_u8((RH264_PEL)w00));
            s = vmlal_u8(s, vld1_u8(r0 + x + 1), vdup_n_u8((RH264_PEL)w10));
            s = vmlal_u8(s, vld1_u8(r1 + x),     vdup_n_u8((RH264_PEL)w01));
            s = vmlal_u8(s, vld1_u8(r1 + x + 1), vdup_n_u8((RH264_PEL)w11));
            RH264_FN(neon_store4)(dst + y * dstride + x, vrshrn_n_u16(s, 6));
            x += 4;
         }
         for (; x < bw; x++)
         {
            int a = r0[x], b = r0[x + 1], c2 = r1[x], d = r1[x + 1];
            dst[y * dstride + x] = (RH264_PEL)((w00 * a + w10 * b
                  + w01 * c2 + w11 * d + 32) >> 6);
         }
      }
      return;
   }
#endif
   for (y = 0; y < bh; y++)
      for (x = 0; x < bw; x++)
      {
         int a = pc[y * pw + x],        b = pc[y * pw + x + 1];
         int c2 = pc[(y+1) * pw + x],   d = pc[(y+1) * pw + x + 1];
         dst[y * dstride + x] = (RH264_PEL)(((8 - fx) * (8 - fy) * a
               + fx * (8 - fy) * b + (8 - fx) * fy * c2
               + fx * fy * d + 32) >> 6);
      }
}

/* d[i] = clip(((d[i]*w + rnd) >> sh) + o): explicit weighting of one
 * prediction in place (8.4.2.3.2; rnd is 1<<(sh-1), or 0 when sh is 0). */
static void RH264_FN(wp_row)(RH264_PEL *d, int n, int w, int rnd, int sh, int o, int bd)
{
   int x = 0;
#if RH264_BD == 8 && defined(RH264_SSE2)
   {
      const __m128i vz = _mm_setzero_si128();
      const __m128i vw = _mm_set1_epi16((short)w);
      const __m128i vr = _mm_set1_epi16((short)rnd);
      const __m128i vo = _mm_set1_epi16((short)o);
      const __m128i sc = _mm_cvtsi32_si128(sh);
      for (; x + 8 <= n; x += 8)
      {
         __m128i v = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(d + x)), vz);
         v = _mm_add_epi16(_mm_sra_epi16(
               _mm_add_epi16(_mm_mullo_epi16(v, vw), vr), sc), vo);
         _mm_storel_epi64((__m128i*)(d + x), _mm_packus_epi16(v, v));
      }
      if (x + 4 <= n)
      {
         __m128i v = _mm_unpacklo_epi8(RH264_FN(sse2_load4)(d + x), vz);
         v = _mm_add_epi16(_mm_sra_epi16(
               _mm_add_epi16(_mm_mullo_epi16(v, vw), vr), sc), vo);
         RH264_FN(sse2_store4)(d + x, _mm_packus_epi16(v, v));
         x += 4;
      }
   }
#elif RH264_BD == 8 && defined(RH264_NEON)
   {
      const int16x8_t vr = vdupq_n_s16((int16_t)rnd);
      const int16x8_t vo = vdupq_n_s16((int16_t)o);
      const int16x8_t vs = vdupq_n_s16((int16_t)-sh);
      for (; x + 8 <= n; x += 8)
      {
         int16x8_t v = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(d + x)));
         v = vaddq_s16(vshlq_s16(vaddq_s16(
               vmulq_n_s16(v, (int16_t)w), vr), vs), vo);
         vst1_u8(d + x, vqmovun_s16(v));
      }
      if (x + 4 <= n)
      {
         int16x8_t v = vreinterpretq_s16_u16(vmovl_u8(RH264_FN(neon_load4)(d + x)));
         v = vaddq_s16(vshlq_s16(vaddq_s16(
               vmulq_n_s16(v, (int16_t)w), vr), vs), vo);
         RH264_FN(neon_store4)(d + x, vqmovun_s16(v));
         x += 4;
      }
   }
#endif
   for (; x < n; x++)
   {
      int v = ((d[x] * w + rnd) >> sh) + o;
      d[x] = (RH264_PEL)RH264_CLIP(v);
   }
}

/* d[i] = clip(((s0[i]*w0 + s1[i]*w1 + bias) >> sh) + o): weighted
 * bi-prediction combine (explicit or implicit, 8.4.2.3.2). Products can
 * exceed int16 (implicit weights reach -64..128), so the accumulation is
 * 32-bit. */

/* Weighted bi-prediction (8.4.2.3) over a whole block.  This was a
 * per-row helper that rebuilt its weight, bias, shift and offset
 * vectors on entry, so a 16-row block paid sixteen identical setups;
 * the weights are constant across a block, so the rows are driven from
 * inside one setup instead. */
static void RH264_FN(bi_block)(RH264_PEL *d, int dstride,
      const RH264_PEL *s0, int s0stride, const RH264_PEL *s1, int s1stride,
      int w, int h, int w0, int w1, int bias, int sh, int o, int bd)
{
   int y;
#if RH264_BD == 8 && defined(RH264_SSE2)
   const __m128i vz = _mm_setzero_si128();
   const __m128i vk = _mm_set1_epi32(
         (int32_t)(((uint32_t)(uint16_t)(int16_t)w1 << 16)
                 |  (uint32_t)(uint16_t)(int16_t)w0));
   const __m128i vb = _mm_set1_epi32(bias);
   const __m128i vo = _mm_set1_epi32(o);
   const __m128i sc = _mm_cvtsi32_si128(sh);
   for (y = 0; y < h; y++)
   {
      RH264_PEL *dr = d + y * dstride;
      const RH264_PEL *a0 = s0 + y * s0stride, *a1 = s1 + y * s1stride;
      int x = 0;
      for (; x + 8 <= w; x += 8)
      {
         __m128i a = _mm_unpacklo_epi8(
               _mm_loadl_epi64((const __m128i*)(a0 + x)), vz);
         __m128i b = _mm_unpacklo_epi8(
               _mm_loadl_epi64((const __m128i*)(a1 + x)), vz);
         __m128i lo = _mm_add_epi32(_mm_sra_epi32(_mm_add_epi32(
               _mm_madd_epi16(_mm_unpacklo_epi16(a, b), vk), vb), sc), vo);
         __m128i hi = _mm_add_epi32(_mm_sra_epi32(_mm_add_epi32(
               _mm_madd_epi16(_mm_unpackhi_epi16(a, b), vk), vb), sc), vo);
         __m128i pw = _mm_packs_epi32(lo, hi);
         _mm_storel_epi64((__m128i*)(dr + x), _mm_packus_epi16(pw, pw));
      }
      if (x + 4 <= w)
      {
         __m128i a = _mm_unpacklo_epi8(RH264_FN(sse2_load4)(a0 + x), vz);
         __m128i b = _mm_unpacklo_epi8(RH264_FN(sse2_load4)(a1 + x), vz);
         __m128i lo = _mm_add_epi32(_mm_sra_epi32(_mm_add_epi32(
               _mm_madd_epi16(_mm_unpacklo_epi16(a, b), vk), vb), sc), vo);
         __m128i pw = _mm_packs_epi32(lo, lo);
         RH264_FN(sse2_store4)(dr + x, _mm_packus_epi16(pw, pw));
         x += 4;
      }
      for (; x < w; x++)
      {
         int v = ((a0[x] * w0 + a1[x] * w1 + bias) >> sh) + o;
         dr[x] = (RH264_PEL)RH264_CLIP(v);
      }
   }
#elif RH264_BD == 8 && defined(RH264_NEON)
   const int32x4_t vb = vdupq_n_s32(bias);
   const int32x4_t vo = vdupq_n_s32(o);
   const int32x4_t vs = vdupq_n_s32(-sh);
   for (y = 0; y < h; y++)
   {
      RH264_PEL *dr = d + y * dstride;
      const RH264_PEL *a0 = s0 + y * s0stride, *a1 = s1 + y * s1stride;
      int x = 0;
      for (; x + 8 <= w; x += 8)
      {
         int16x8_t a = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(a0 + x)));
         int16x8_t b = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(a1 + x)));
         int32x4_t lo = vmlal_n_s16(vmull_n_s16(vget_low_s16(a),
               (int16_t)w0), vget_low_s16(b), (int16_t)w1);
         int32x4_t hi = vmlal_n_s16(vmull_n_s16(vget_high_s16(a),
               (int16_t)w0), vget_high_s16(b), (int16_t)w1);
         lo = vaddq_s32(vshlq_s32(vaddq_s32(lo, vb), vs), vo);
         hi = vaddq_s32(vshlq_s32(vaddq_s32(hi, vb), vs), vo);
         vst1_u8(dr + x, vqmovun_s16(vcombine_s16(vqmovn_s32(lo),
               vqmovn_s32(hi))));
      }
      if (x + 4 <= w)
      {
         int16x8_t a = vreinterpretq_s16_u16(vmovl_u8(RH264_FN(neon_load4)(a0 + x)));
         int16x8_t b = vreinterpretq_s16_u16(vmovl_u8(RH264_FN(neon_load4)(a1 + x)));
         int32x4_t lo = vmlal_n_s16(vmull_n_s16(vget_low_s16(a),
               (int16_t)w0), vget_low_s16(b), (int16_t)w1);
         int16x4_t pw;
         lo = vaddq_s32(vshlq_s32(vaddq_s32(lo, vb), vs), vo);
         pw = vqmovn_s32(lo);
         RH264_FN(neon_store4)(dr + x, vqmovun_s16(vcombine_s16(pw, pw)));
         x += 4;
      }
      for (; x < w; x++)
      {
         int v = ((a0[x] * w0 + a1[x] * w1 + bias) >> sh) + o;
         dr[x] = (RH264_PEL)RH264_CLIP(v);
      }
   }
#else
   { int x;
     for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
           int v = ((s0[y*s0stride+x] * w0 + s1[y*s1stride+x] * w1
                 + bias) >> sh) + o;
           d[y*dstride+x] = (RH264_PEL)RH264_CLIP(v);
        } }
#endif
}

/* (s0 + s1 + 1) >> 1 per sample: the default bi-prediction (8.4.2.3.1) */
static void RH264_FN(avg_block)(RH264_PEL *d, int dstride,
      const RH264_PEL *s0, int s0s, const RH264_PEL *s1, int s1s,
      int w, int h, int bd)
{
   int x, y;
   for (y = 0; y < h; y++)
   {
      RH264_PEL *dr = d + (size_t)y*dstride;
      const RH264_PEL *a = s0 + (size_t)y*s0s, *b = s1 + (size_t)y*s1s;
      x = 0;
#if RH264_BD == 8
#ifdef RH264_SSE2
      for (; x + 8 <= w; x += 8)
         _mm_storel_epi64((__m128i*)(dr + x), _mm_avg_epu8(
               _mm_loadl_epi64((const __m128i*)(a + x)),
               _mm_loadl_epi64((const __m128i*)(b + x))));
      if (x + 4 <= w)
      {
         RH264_FN(sse2_store4)(dr + x, _mm_avg_epu8(
               RH264_FN(sse2_load4)(a + x), RH264_FN(sse2_load4)(b + x)));
         x += 4;
      }
#elif defined(RH264_NEON)
      for (; x + 8 <= w; x += 8)
         vst1_u8(dr + x, vrhadd_u8(vld1_u8(a + x), vld1_u8(b + x)));
      if (x + 4 <= w)
      {
         RH264_FN(neon_store4)(dr + x, vrhadd_u8(
               RH264_FN(neon_load4)(a + x), RH264_FN(neon_load4)(b + x)));
         x += 4;
      }
#endif
#endif
      for (; x < w; x++)
         dr[x] = (RH264_PEL)((a[x] + b[x] + 1) >> 1);
   }
}

#undef RH264_PMAX
#undef RH264_PHALF
#undef RH264_CLIP
#undef RH264_HB
