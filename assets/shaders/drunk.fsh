// Drunk shader

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
uniform vec4 u_time;

varying vec2 v_texcoord0;

void main() {

  // Tweek this to increase/decrease the divergence
  
  float speed = .8;
  float distance = .01;
  float strength = .7;

  // Calculate the delta distance

  float delta = cos(u_time.x * speed) * distance;

  // Calculate the luminance of the pixels in the 4 diagonal at distance delta

  vec3 luma = vec3(.299, .587, .114);

  vec3 rgbM  = texture2D(sampler0, v_texcoord0.xy).xyz;
  vec3 rgbNW = texture2D(sampler0, v_texcoord0.xy + (vec2(-1., -1.) * delta)).xyz;
  vec3 rgbNE = texture2D(sampler0, v_texcoord0.xy + (vec2(+1., -1.) * delta)).xyz;
  vec3 rgbSW = texture2D(sampler0, v_texcoord0.xy + (vec2(-1., +1.) * delta)).xyz;
  vec3 rgbSE = texture2D(sampler0, v_texcoord0.xy + (vec2(+1., +1.) * delta)).xyz; 

  float lumaNW = dot(rgbNW, luma);
  float lumaNE = dot(rgbNE, luma);
  float lumaSW = dot(rgbSW, luma);
  float lumaSE = dot(rgbSE, luma);
  float lumaM  = dot(rgbM,  luma);

  // Get the min and max luminance
	
  float lumaMin = min(min(lumaNW, lumaNE), min(lumaSW, lumaSE));
  float lumaMax = max(max(lumaNW, lumaNE), max(lumaSW, lumaSE));

  // If the luma of the pixel is between max and min, then set rgb to the min one

  if ((lumaM > lumaMin) && (lumaM < lumaMax)) {

    vec3 rgb;
    if (lumaMin == lumaNW) rgb = rgbNW;
    else if (lumaMin == lumaNE) rgb = rgbNE;
    else if (lumaMin == lumaSW) rgb = rgbSW;
    else if (lumaMin == lumaSE) rgb = rgbSE;

    // Blend based on strength
    
    rgbM = rgbM * (1. - strength) + rgb * strength;
 
  }  

  // Darken the image periodically

  gl_FragColor.xyz = rgbM - cos(u_time.x * speed * 1.3) * .2 * strength;

  gl_FragColor.a = 1.;

}
