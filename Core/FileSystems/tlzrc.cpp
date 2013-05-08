
// tlzrc.c: LZRC decodeer
//   based on benhur's code, rewrite by tpu

// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__SYMBIAN32__) && !defined(__MAC_10_6)
#include <malloc.h>
#endif

#include "Common.h"

/*************************************************************/

typedef unsigned int   u32;
typedef unsigned short u16;
typedef unsigned char  u8;

/*************************************************************/

typedef struct{

	// input stream
	u8 *input;
	int in_ptr;
	int in_len;

	// output stream
	u8 *output;
	int out_ptr;
	int out_len;

	// range decode
	u32 range;
	u32 code;
	u32 out_code;
	u8 lc;

	u8 bm_literal[8][256];
	u8 bm_dist_bits[8][39];
	u8 bm_dist[18][8];
	u8 bm_match[8][8];
	u8 bm_len[8][31];
}LZRC_DECODE;

/*************************************************************/

static u8 rc_getbyte(LZRC_DECODE *rc)
{
	if(rc->in_ptr == rc->in_len){
		_dbg_assert_msg_(LOADER, false, "LZRC: End of input!");
	}

	return rc->input[rc->in_ptr++];
}

static void rc_putbyte(LZRC_DECODE *rc, u8 byte)
{
	if(rc->out_ptr == rc->out_len){
		_dbg_assert_msg_(LOADER, false, "LZRC: Output overflow!");
	}

	rc->output[rc->out_ptr++] = byte;
}

static void rc_init(LZRC_DECODE *rc, void *out, int out_len, void *in, int in_len)
{
	rc->input = (u8*)in;
	rc->in_len = in_len;
	rc->in_ptr = 0;

	rc->output = (u8*)out;
	rc->out_len = out_len;
	rc->out_ptr = 0;

	rc->range = 0xffffffff;
	rc->lc = rc_getbyte(rc);
	rc->code =  (rc_getbyte(rc)<<24) |
				(rc_getbyte(rc)<<16) | 
				(rc_getbyte(rc)<< 8) |
				(rc_getbyte(rc)<< 0) ;
	rc->out_code = 0xffffffff;

	memset(rc->bm_literal,   0x80, 2048);
	memset(rc->bm_dist_bits, 0x80, 312);
	memset(rc->bm_dist,      0x80, 144);
	memset(rc->bm_match,     0x80, 64);
	memset(rc->bm_len,       0x80, 248);

}



/*************************************************************/

/* range decode */

static void normalize(LZRC_DECODE *rc)
{
	if(rc->range<0x01000000){
		rc->range <<= 8;
		rc->code = (rc->code<<8)+rc->input[rc->in_ptr];
		rc->in_ptr++;
	}
}

/* Decode a bit */
static int rc_bit(LZRC_DECODE *rc, u8 *prob)
{
	u32 bound;

	normalize(rc);

	bound = (rc->range>>8)*(*prob);
	*prob -= *prob>>3;

	if(rc->code < bound){
		rc->range = bound;
		*prob += 31;
		return 1;
	}else{
		rc->code -= bound;
		rc->range -= bound;
		return 0;
	}
}

/* Decode a bittree starting from MSB */
static int rc_bittree(LZRC_DECODE *rc, u8 *probs, int limit)
{
	int number = 1;

	do{
		number = (number<<1)+rc_bit(rc, probs+number);
	}while(number<limit);

	return number;
}


/*
 * decode a number
 *
 * a number are divide into three part:
 *   MSB 2bits
 *   direct bits (don't use probability modle)
 *   LSB 3bits
 */
static int rc_number(LZRC_DECODE *rc, u8 *prob, int n)
{
	int i, number = 1;

	if(n>3){
		number = (number<<1)+rc_bit(rc, prob+3);
		if(n>4){
			number = (number<<1)+rc_bit(rc, prob+3);
			if(n>5){
				// direct bits
				normalize(rc);

				for(i=0; i<n-5; i++){
					rc->range >>= 1;
					number <<= 1;
					if (rc->code < rc->range){
						number += 1;
					}else{
						rc->code -= rc->range;
					}
				}
			}
		}
	}

	if(n>0){
		number = (number<<1)+rc_bit(rc, prob);
		if(n>1){
			number = (number<<1)+rc_bit(rc, prob+1);
			if(n>2){
				number = (number<<1)+rc_bit(rc, prob+2);
			}
		}	
	}

	return number;
}


int lzrc_decompress(void *out, int out_len, void *in, int in_len)
{
	LZRC_DECODE rc;
	int match_step, rc_state, len_state, dist_state;
	int i, bit, byte, last_byte;
	int match_len, len_bits;
	int match_dist, dist_bits, limit;
	u8 *match_src;
	int round = -1;

	rc_init(&rc, out, out_len, in, in_len);

	if(rc.lc&0x80){
		/* plain text */
		memcpy(rc.output, rc.input+5, rc.code);
		return rc.code; 
	}

	rc_state = 0;
	last_byte = 0;


	while (1) {
		round += 1;
		match_step = 0;

		bit = rc_bit(&rc, &rc.bm_match[rc_state][match_step]);
		if (bit==0) {
			/* 0 -> raw char */
			if(rc_state>0)
				rc_state -= 1;

			byte = rc_bittree(&rc, &rc.bm_literal[((last_byte>>rc.lc)&0x07)][0], 0x100);
			byte -= 0x100;

			rc_putbyte(&rc, byte);
		} else {                       
			/* 1 -> a match */

			/* find bits of match length */
			len_bits = 0;
			for(i=0; i<7; i++){
				match_step += 1;
				bit = rc_bit(&rc, &rc.bm_match[rc_state][match_step]);
				if(bit==0)
					break;
				len_bits += 1;
			}

			/* find match length */
			if(len_bits==0){
				match_len = 1;
			}else{
				len_state = ((len_bits-1)<<2)+((rc.out_ptr<<(len_bits-1))&0x03);
				match_len = rc_number(&rc, &rc.bm_len[rc_state][len_state], len_bits);
				if (match_len == 0xFF){
					//end of stream
					return rc.out_ptr;
				}
			}

			/* find number of bits of match distance */
			dist_state = 0;
			limit = 8;
			if(match_len>2){
				dist_state += 7;
				limit = 44;
			}
			dist_bits = rc_bittree(&rc, &rc.bm_dist_bits[len_bits][dist_state], limit);
			dist_bits -= limit;

			/* find match distance */
			if(dist_bits>0){
				match_dist = rc_number(&rc, &rc.bm_dist[dist_bits][0], dist_bits);
			} else {
				match_dist = 1;
			}

			/* copy match bytes */
			if(match_dist>rc.out_ptr || match_dist<0){
				printf("match_dist out of range! %08x\n", match_dist);
				return -1;
			}
			match_src = rc.output+rc.out_ptr-match_dist;
			for(i=0; i<match_len+1; i++){
				rc_putbyte(&rc, *match_src++);
			}
			rc_state = 6+((rc.out_ptr+1)&1);
		}
		last_byte = rc.output[rc.out_ptr-1];
	}
}
