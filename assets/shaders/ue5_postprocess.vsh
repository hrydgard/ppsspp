#ifdef GL_ES
precision highp float;
#endif

uniform vec2 u_pixelDelta;
uniform vec2 u_texelDelta;

varying vec2 v_texcoord0;
varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;
varying vec4 v_texcoord4;
varying vec4 v_texcoord5;
varying vec4 v_texcoord6;

attribute vec4 a_position;
attribute vec2 a_texcoord0;

void main() {
    gl_Position = a_position;
    v_texcoord0 = a_texcoord0;
    vec2 texel = u_texelDelta;
    v_texcoord1 = vec4(a_texcoord0.x - texel.x, a_texcoord0.y - texel.y,
                        a_texcoord0.x + texel.x, a_texcoord0.y - texel.y);
    v_texcoord2 = vec4(a_texcoord0.x + texel.x, a_texcoord0.y - texel.y,
                        a_texcoord0.x - texel.x, a_texcoord0.y + texel.y);
    v_texcoord3 = vec4(a_texcoord0.x - texel.x, a_texcoord0.y + texel.y,
                        a_texcoord0.x + texel.x, a_texcoord0.y + texel.y);
    v_texcoord4 = vec4(a_texcoord0.x + texel.x, a_texcoord0.y + texel.y, 0.0, 0.0);
    v_texcoord5 = vec4(a_texcoord0.x - texel.x, a_texcoord0.y,
                        a_texcoord0.x + texel.x, a_texcoord0.y);
    v_texcoord6 = vec4(a_texcoord0.x, a_texcoord0.y - texel.y,
                        a_texcoord0.x, a_texcoord0.y + texel.y);
}
