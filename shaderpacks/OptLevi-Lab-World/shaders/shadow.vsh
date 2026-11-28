#version 120
#include "/lib/settings.glsl"
varying vec2 uv;
varying vec4 tint;
void main() {
    uv = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;
    tint = gl_Color;
    gl_Position = ftransform();
}
