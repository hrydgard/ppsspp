//arcade scanlines v1.1 by Nick
//preset 3

//intended for use with the original PSP resolution, but works with high res too


#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif



/////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// CUSTOMIZATION SETTINGS //////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////


float scanlines = 0.25;						//default 0.25
float brighten = 0.6;						//default 0.6
float colorshift = 0.6;						//default 0.6
float lineblur = 0.3;						//default 0.3
float phosphorgrid = 0.7;					//default 0.7
float screencurve = 0.2;					//default 0.2
float interlacing = 0.6;					//default 0.6

//range for all the above: 0.0 - 1.0


 
float scanline_count = 272.0;				//default 272.0
							//For use with arcade conversions such as SFA3, Darkstalkers
							//or Metal Slug. Change this to 224.0 and change PPSSPP's
							//internal rendering resolution by 2x, to align the
							//scanlines to the game's original arcade resolution.


/////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////









//DISCLAIMER: I am not a coder, so please excuse anything that may make your eyes bleed. :)



//initializing and adjusting stuff:

uniform vec2 u_pixelDelta;
uniform vec2 u_texelDelta;
uniform vec4 u_time;
uniform sampler2D sampler0;
varying vec2 v_texcoord0;

void main()
{

float rendersize = 1.0 / u_texelDelta.y / 272.0;
float lines = scanline_count / 272.0 / u_texelDelta.y / rendersize;
float xRes = 1.0 / u_pixelDelta.x;
float yRes = 1.0 / u_pixelDelta.y;
float pi = 3.141592653;
float hblur = 0.002 * lineblur;
float cshift = 0.001 * colorshift;
float phosphor = 1.0 - clamp(phosphorgrid, 0.0, 1.0);
float darken = 1.0 - clamp(scanlines, 0.0, 1.0);
float scanwidthcurve = scanlines / 2.0;

//TV screen curve:

	//horizontal and vertical screen curve functions:

	vec2 f = vec2(
		(v_texcoord0.x - 0.5)*(v_texcoord0.x - 0.5) * screencurve, 
		(v_texcoord0.y - 0.5)*(v_texcoord0.y - 0.5) * screencurve * 0.567
	);

	//applying and aligning functions to screen pixels:

	vec2 curvedcoord = vec2(	
		v_texcoord0.x + (f.y - f.y * (1.5 - v_texcoord0.x)),
		v_texcoord0.y + (f.x - f.x * (1.5 - v_texcoord0.y))
	);

	//make the border outside the screen black instead of stretched colors:

	float xborder = floor(curvedcoord.x + 1.0);
	float yborder = floor(curvedcoord.y + 1.0);



//horizontal blur and color shift, and a sine based line sampler that stretches filtered pixels vertically
//to make them fit the scanlines better and prevent scanline bleed:	

	float sinsample = (sin(curvedcoord.y * lines * 2.0 * pi) + curvedcoord.y * lines * 2.0 * pi)
					 /(lines * 2.0 * pi);


	float r =  (texture2D(sampler0, vec2(curvedcoord.x + cshift, sinsample)).x
			  + texture2D(sampler0, vec2(curvedcoord.x + cshift + hblur, sinsample)).x
			  + texture2D(sampler0, vec2(curvedcoord.x + cshift - hblur, sinsample)).x)
			  / (3.0-brighten);

	float g =  (texture2D(sampler0, vec2(curvedcoord.x - cshift, sinsample)).y
			  + texture2D(sampler0, vec2(curvedcoord.x - cshift + hblur, sinsample)).y
			  + texture2D(sampler0, vec2(curvedcoord.x - cshift - hblur, sinsample)).y)
			  / (3.0-brighten);

	float b =  (texture2D(sampler0, vec2(curvedcoord.x, sinsample)).z
			  + texture2D(sampler0, vec2(curvedcoord.x + hblur, sinsample)).z
			  + texture2D(sampler0, vec2(curvedcoord.x - hblur, sinsample)).z)
			  / (3.0-brighten);

	//phosphor rgb grid:
	//rgb color lines:

	int posr = int(v_texcoord0.x * xRes + 2.0);
	int posg = int(v_texcoord0.x * xRes + 1.0);
	int posb = int(v_texcoord0.x * xRes);

	float intr = mod(float(posr), 3.0);
	float intg = mod(float(posg), 3.0);
	float intb = mod(float(posb), 3.0);

	r *= clamp(intg * intb, phosphor, 1.0);
	g *= clamp(intr * intb, phosphor, 1.0);
	b *= clamp(intr * intg, phosphor, 1.0);

	//breaks between phosphor rgb elements in a hexagonal pattern:

	int yposPhosbreak1 = int(v_texcoord0.y * yRes);
	int yposPhosbreak2 = int(v_texcoord0.y * yRes + 2.0);
	int xposPhosbreak = int(v_texcoord0.x * xRes/3.0 - 0.333333333);

	float intPhosbreak1 = mod(float(yposPhosbreak1), 4.0) + mod(float(xposPhosbreak), 2.0);
	float intPhosbreak2 = mod(float(yposPhosbreak2), 4.0) + (1.0-mod(float(xposPhosbreak), 2.0));


//final composition, phosphor seems to slightly redden image (?), so if it's on, we slightly mute r:

	vec3 rgb = vec3(r * (0.88 + 0.12 * phosphor), g, b);

//apply phosphor breaks:

	rgb *= clamp(intPhosbreak1 * intPhosbreak2 + 0.5 + 0.5 * phosphor, 0.7, 1.0);


//make scanlines:

	rgb -= 1.0 - clamp(darken + scanwidthcurve * (1.0 + cos( 0.75 * pi + curvedcoord.y * pi
		* 2.0 * lines)), 0.0, 1.0);


//interlacing:

	rgb *= 0.35 * interlacing + clamp(sin( -0.125 * pi + mod(u_time.w, 2.0) * pi + curvedcoord.y * pi * lines), 1.0 - interlacing, 1.0);



//apply border:

	rgb *= mod(float(xborder), 2.0) * mod(float(yborder), 2.0);


//pixel all done!

	gl_FragColor.rgb = rgb;
	gl_FragColor.a = 1.0;

}