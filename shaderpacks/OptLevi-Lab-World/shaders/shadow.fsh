#version 120
#include "/lib/settings.glsl"
varying vec2 uv;
varying vec4 tint;
uniform sampler2D texture;
void main() {
    /* DRAWBUFFERS:0 */
    gl_FragData[0] = texture2D(texture, uv) * tint;
}
