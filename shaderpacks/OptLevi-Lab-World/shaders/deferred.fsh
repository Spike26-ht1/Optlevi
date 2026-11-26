#version 120
/*
const int colortex0Format = RGBA8;
const int colortex1Format = RGBA8;
*/
varying vec2 uv;
uniform sampler2D colortex0;
uniform sampler2D colortex1;
uniform sampler2D depthtex0;
void main() {
    vec4 color = texture2D(colortex0, uv);
    vec3 normal = texture2D(colortex1, uv).rgb * 2.0 - 1.0;
    float depth = texture2D(depthtex0, uv).r;
    float shade = depth < 0.9999 ? mix(0.92, 1.08, clamp(normal.y * 0.5 + 0.5, 0.0, 1.0)) : 1.0;
    /* DRAWBUFFERS:0 */
    gl_FragData[0] = vec4(color.rgb * shade, color.a);
}
