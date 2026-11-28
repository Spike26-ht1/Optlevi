#version 120
#include "/lib/lab_settings.glsl"
varying vec2 uv;
uniform sampler2D colortex0;
void main(){
    vec3 c=texture2D(colortex0,uv).rgb;
    float highlight=max(max(c.r,c.g),c.b);
    vec3 glow=max(c-vec3(0.48),vec3(0.0))*(0.7+0.3*smoothstep(0.48,0.9,highlight));
    /* DRAWBUFFERS:1 */
    gl_FragData[0]=vec4(glow,1.0);
}
