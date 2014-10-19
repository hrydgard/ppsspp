/*
   AA 2D+3D GLSL shader
   Copyright (C) 2011 guest(r) - guest.r@gmail.com
*/

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;

varying vec4 v_texcoord0;
varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;
varying vec4 v_texcoord4;
varying vec4 v_texcoord5;
varying vec4 v_texcoord6;

void main()
{
    vec3 c11 = texture2D(sampler0, v_texcoord0.xy).xyz;
    vec3 s00 = texture2D(sampler0, v_texcoord1.xy).xyz; 
    vec3 s20 = texture2D(sampler0, v_texcoord2.xy).xyz; 
    vec3 s22 = texture2D(sampler0, v_texcoord3.xy).xyz; 
    vec3 s02 = texture2D(sampler0, v_texcoord4.xy).xyz; 
    vec3 c00 = texture2D(sampler0, v_texcoord5.xy).xyz; 
    vec3 c22 = texture2D(sampler0, v_texcoord6.xy).xyz; 
    vec3 c20 = texture2D(sampler0, v_texcoord5.zw).xyz;
    vec3 c02 = texture2D(sampler0, v_texcoord6.zw).xyz;
    vec3 c10 = texture2D(sampler0, v_texcoord1.zw).xyz; 
    vec3 c21 = texture2D(sampler0, v_texcoord2.zw).xyz; 
    vec3 c12 = texture2D(sampler0, v_texcoord3.zw).xyz; 
    vec3 c01 = texture2D(sampler0, v_texcoord4.zw).xyz;     
    vec3 dt = vec3(1.0,1.0,1.0);

    float d1=dot(abs(c00-c22),dt)+0.001;
    float d2=dot(abs(c20-c02),dt)+0.001;
    float hl=dot(abs(c01-c21),dt)+0.001;
    float vl=dot(abs(c10-c12),dt)+0.001;
    float m1=dot(abs(s00-s22),dt)+0.001;
    float m2=dot(abs(s02-s20),dt)+0.001;

    c11 =.5*(m2*(s00+s22)+m1*(s02+s20))/(m1+m2);

    float k1 = max(dot(abs(c00-c11),dt),dot(abs(c22-c11),dt))+0.01;k1=1.0/k1;
    float k2 = max(dot(abs(c20-c11),dt),dot(abs(c02-c11),dt))+0.01;k2=1.0/k2;
    float k3 = max(dot(abs(c01-c11),dt),dot(abs(c21-c11),dt))+0.01;k3=1.0/k3;
    float k4 = max(dot(abs(c01-c11),dt),dot(abs(c12-c11),dt))+0.01;k4=1.0/k4;

    c11 = 0.5*(k1*(c00+c22)+k2*(c20+c02)+k3*(c01+c21)+k4*(c10+c12))/(k1+k2+k3+k4);

    vec3 mn1 = min(min(c00,c01),c02);
    vec3 mn2 = min(min(c10,c11),c12);
    vec3 mn3 = min(min(c20,c21),c22);
    vec3 mx1 = max(max(c00,c01),c02);
    vec3 mx2 = max(max(c10,c11),c12);
    vec3 mx3 = max(max(c20,c21),c22);
    mn1 = min(min(mn1,mn2),mn3);
    mx1 = max(max(mx1,mx2),mx3);

    float filterparam = 1.7; 
    vec3 dif1 = abs(c11-mn1) + 0.001*dt;
    vec3 dif2 = abs(c11-mx1) + 0.001*dt;

    dif1=vec3(pow(dif1.x,filterparam),pow(dif1.y,filterparam),pow(dif1.z,filterparam));
    dif2=vec3(pow(dif2.x,filterparam),pow(dif2.y,filterparam),pow(dif2.z,filterparam));

    c11.r = (dif1.x*mx1.x + dif2.x*mn1.x)/(dif1.x + dif2.x);
    c11.g = (dif1.y*mx1.y + dif2.y*mn1.y)/(dif1.y + dif2.y);
    c11.b = (dif1.z*mx1.z + dif2.z*mn1.z)/(dif1.z + dif2.z);

    gl_FragColor.xyz=c11;
}