/*
	psp_color vertex shader
	
	Original code written by hunterk, modified by Pokefan531 and
	released into the public domain
	
	'Ported' (i.e. copy/paste) to PPSSPP format by jdgleaver
	
	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the Free
	Software Foundation; either version 2 of the License, or (at your option)
	any later version.
*/

attribute vec4 a_position;
attribute vec2 a_texcoord0;
varying vec2 v_texcoord0;

void main()
{
	v_texcoord0 = a_texcoord0; // + 0.000001; // HLSL precision workaround (is this needed???)
	gl_Position = a_position;
}
