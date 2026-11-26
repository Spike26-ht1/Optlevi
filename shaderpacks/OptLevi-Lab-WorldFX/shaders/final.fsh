#version 120
#include "/lib/lab_settings.glsl"
varying vec2 uv;
uniform sampler2D colortex0;
#ifdef LAB_BLOOM
uniform sampler2D colortex2;
#endif
void main(){
    vec3 c=labGrade(texture2D(colortex0,uv).rgb);
#ifdef LAB_BLOOM
    c+=LAB_BLOOM_STRENGTH*texture2D(colortex2,uv).rgb;
#endif
    vec2 v=uv*2.0-1.0;
    float shade=1.0-LAB_VIGNETTE*clamp(dot(v,v)*0.5,0.0,1.0);
    gl_FragColor=vec4(clamp(c*shade,0.0,1.0),1.0);
}
