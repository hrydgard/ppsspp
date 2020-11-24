/*
   Hyllian's 5xBR v3.5a Shader
   
   Copyright (C) 2011 Hyllian/Jararaca - sergiogdb@gmail.com
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
   
*/
#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
varying vec2 v_texcoord0;

const float coef			= 2.0;
const vec3  rgbw			= vec3(16.163, 23.351, 8.4772);

const vec4 Ao = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bo = vec4( 1.0,  1.0, -1.0,-1.0 );
const vec4 Co = vec4( 1.5,  0.5, -0.5, 0.5 );
const vec4 Ax = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bx = vec4( 0.5,  2.0, -0.5,-2.0 );
const vec4 Cx = vec4( 1.0,  1.0, -0.5, 0.0 );
const vec4 Ay = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 By = vec4( 2.0,  0.5, -2.0,-0.5 );
const vec4 Cy = vec4( 2.0,  0.0, -1.0, 0.5 );


vec4 df(vec4 A, vec4 B) {
	return abs(A-B);
}

vec4 weighted_distance(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
	return (df(a,b) + df(a,c) + df(d,e) + df(d,f) + 4.0*df(g,h));
}


void main(){

 bool upscale = u_texelDelta.x > (1.6 * u_pixelDelta.x);
 vec3 res = texture2D(sampler0, v_texcoord0.xy).xyz;

 // Let's skip the whole scaling if output size smaller than 1.6x of input size
 if (upscale) {

	bvec4 edr, edr_left, edr_up, px; // px = pixel, edr = edge detection rule
	bvec4 interp_restriction_lv1, interp_restriction_lv2_left, interp_restriction_lv2_up;
	bvec4 nc; // new_color
	bvec4 fx, fx_left, fx_up; // inequations of straight lines.

	vec2 pS  = 1.0 / u_texelDelta.xy;
	vec2 fp  = fract(v_texcoord0.xy*pS.xy);
	vec2 TexCoord_0 = v_texcoord0.xy-fp*u_pixelDelta.xy;
	vec2 dx  = vec2(u_texelDelta.x,0.0);
	vec2 dy  = vec2(0.0,u_texelDelta.y);
	vec2 y2  = dy + dy; vec2 x2  = dx + dx;

	vec3 A  = texture2D(sampler0, TexCoord_0 -dx -dy	).xyz;
	vec3 B  = texture2D(sampler0, TexCoord_0	 -dy	).xyz;
	vec3 C  = texture2D(sampler0, TexCoord_0 +dx -dy	).xyz;
	vec3 D  = texture2D(sampler0, TexCoord_0 -dx		).xyz;
	vec3 E  = texture2D(sampler0, TexCoord_0			).xyz;
	vec3 F  = texture2D(sampler0, TexCoord_0 +dx		).xyz;
	vec3 G  = texture2D(sampler0, TexCoord_0 -dx +dy	).xyz;
	vec3 H  = texture2D(sampler0, TexCoord_0	 +dy	).xyz;
	vec3 I  = texture2D(sampler0, TexCoord_0 +dx +dy	).xyz;
	vec3 A1 = texture2D(sampler0, TexCoord_0	 -dx -y2).xyz;
	vec3 C1 = texture2D(sampler0, TexCoord_0	 +dx -y2).xyz;
	vec3 A0 = texture2D(sampler0, TexCoord_0 -x2	 -dy).xyz;
	vec3 G0 = texture2D(sampler0, TexCoord_0 -x2	 +dy).xyz;
	vec3 C4 = texture2D(sampler0, TexCoord_0 +x2	 -dy).xyz;
	vec3 I4 = texture2D(sampler0, TexCoord_0 +x2	 +dy).xyz;
	vec3 G5 = texture2D(sampler0, TexCoord_0	 -dx +y2).xyz;
	vec3 I5 = texture2D(sampler0, TexCoord_0	 +dx +y2).xyz;
	vec3 B1 = texture2D(sampler0, TexCoord_0		 -y2).xyz;
	vec3 D0 = texture2D(sampler0, TexCoord_0 -x2		).xyz;
	vec3 H5 = texture2D(sampler0, TexCoord_0		 +y2).xyz;
	vec3 F4 = texture2D(sampler0, TexCoord_0 +x2		).xyz;

	vec4 b  = vec4(dot(B ,rgbw), dot(D ,rgbw), dot(H ,rgbw), dot(F ,rgbw));
	vec4 c  = vec4(dot(C ,rgbw), dot(A ,rgbw), dot(G ,rgbw), dot(I ,rgbw));
	vec4 d  = vec4(b.y, b.z, b.w, b.x);
	vec4 e  = vec4(dot(E,rgbw));
	vec4 f  = vec4(b.w, b.x, b.y, b.z);
	vec4 g  = vec4(c.z, c.w, c.x, c.y);
	vec4 h  = vec4(b.z, b.w, b.x, b.y);
	vec4 i  = vec4(c.w, c.x, c.y, c.z);
	vec4 i4 = vec4(dot(I4,rgbw), dot(C1,rgbw), dot(A0,rgbw), dot(G5,rgbw));
	vec4 i5 = vec4(dot(I5,rgbw), dot(C4,rgbw), dot(A1,rgbw), dot(G0,rgbw));
	vec4 h5 = vec4(dot(H5,rgbw), dot(F4,rgbw), dot(B1,rgbw), dot(D0,rgbw));
	vec4 f4 = vec4(h5.y, h5.z, h5.w, h5.x);

	// These inequations define the line below which interpolation occurs.
	fx	  = greaterThan(Ao*fp.y+Bo*fp.x,Co); 
	fx_left = greaterThan(Ax*fp.y+Bx*fp.x,Cx);
	fx_up   = greaterThan(Ay*fp.y+By*fp.x,Cy);

	interp_restriction_lv1	  = bvec4(vec4(notEqual(e,f))*vec4(notEqual(e,h)));
	interp_restriction_lv2_left = bvec4(vec4(notEqual(e,g))*vec4(notEqual(d,g)));
	interp_restriction_lv2_up   = bvec4(vec4(notEqual(e,c))*vec4(notEqual(b,c)));

	edr	  = bvec4(vec4(lessThan(weighted_distance( e, c, g, i, h5, f4, h, f), weighted_distance( h, d, i5, f, i4, b, e, i)))*vec4(interp_restriction_lv1));
	edr_left = bvec4(vec4(lessThanEqual(coef*df(f,g),df(h,c)))*vec4(interp_restriction_lv2_left)); 
	edr_up   = bvec4(vec4(greaterThanEqual(df(f,g),coef*df(h,c)))*vec4(interp_restriction_lv2_up));

	nc.x = ( edr.x && (fx.x || edr_left.x && fx_left.x || edr_up.x && fx_up.x) );
	nc.y = ( edr.y && (fx.y || edr_left.y && fx_left.y || edr_up.y && fx_up.y) );
	nc.z = ( edr.z && (fx.z || edr_left.z && fx_left.z || edr_up.z && fx_up.z) );
	nc.w = ( edr.w && (fx.w || edr_left.w && fx_left.w || edr_up.w && fx_up.w) );	

	px = lessThanEqual(df(e,f),df(e,h));

	res = nc.x ? px.x ? F : H : nc.y ? px.y ? B : F : nc.z ? px.z ? D : B : nc.w ? px.w ? H : D : E;	
 }
	gl_FragColor.rgb = res;
	gl_FragColor.a = 1.0;
}
