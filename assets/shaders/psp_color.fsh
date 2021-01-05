/*
	psp_color vertex shader
	
	Original code written by hunterk, modified by Pokefan531 and
	released into the public domain
	
	'Ported' (i.e. copy/paste) to PPSSPP format by jdgleaver
	
	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the Free
	Software Foundation; either version 2 of the License, or (at your option)
	any later version.
	
	Notes: This shader replicates the LCD dynamics of the PSP 1000 and PSP 2000
*/

//================
#ifdef GL_ES
precision mediump float;
precision mediump int;
// For android, use this instead...
//precision highp float;
//precision highp int;
//
#endif

//================
#define target_gamma 2.21
#define display_gamma 2.2
#define r 0.98
#define g 0.795
#define b 0.98
#define rg 0.04
#define rb 0.01
#define gr 0.20
#define gb 0.01
#define br -0.18
#define bg 0.165

//================
uniform sampler2D sampler0;
varying vec2 v_texcoord0;

//================
void main()
{
	// Apply colour correction
	vec3 screen = pow(texture2D(sampler0, v_texcoord0.xy).rgb, vec3(target_gamma));
	// screen = clamp(screen, 0.0, 1.0);
	screen = pow(
		mat3(r,  rg, rb,
			  gr, g,  gb,
			  br, bg, b) * screen,
		vec3(1.0 / display_gamma)
	);
	
	gl_FragColor = vec4(screen, 1.0);
}
