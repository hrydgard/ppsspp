//Simple Scanlines shader


#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
float offset = 1.0;
float frequency = 170.0;

varying vec2 v_texcoord0;

void main()
{
    float pos0 = (v_texcoord0.y + offset) * frequency;
    float pos1 = cos((fract( pos0 ) - 0.5)*3.14);
    vec4 pel = texture2D( sampler0, v_texcoord0 );

    gl_FragColor = mix(vec4(0,0,0,0), pel, pos1);
}
