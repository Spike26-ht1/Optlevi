#version 120
#include "/lib/lab_settings.glsl"
varying vec2 uv;
uniform sampler2D colortex1;
uniform float viewWidth;
uniform float viewHeight;
void main(){
    vec2 px=1.0/vec2(viewWidth,viewHeight);
    vec3 blur=texture2D(colortex1,uv).rgb*0.24;
    blur+=texture2D(colortex1,uv+vec2(px.x,0.0)).rgb*0.16;
    blur+=texture2D(colortex1,uv-vec2(px.x,0.0)).rgb*0.16;
    blur+=texture2D(colortex1,uv+vec2(0.0,px.y)).rgb*0.16;
    blur+=texture2D(colortex1,uv-vec2(0.0,px.y)).rgb*0.16;
    blur+=texture2D(colortex1,uv+px).rgb*0.06;
    blur+=texture2D(colortex1,uv-px).rgb*0.06;
    /* DRAWBUFFERS:2 */
    gl_FragData[0]=vec4(blur,1.0);
}
