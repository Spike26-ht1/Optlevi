#version 120
#include "/lib/settings.glsl"
varying vec2 uv;
varying vec4 tint;
uniform sampler2D texture;
uniform float frameTimeCounter;
void main() {
    vec4 water = texture2D(texture, uv) * tint;
#ifdef WATER_TINT
    float ripple = 0.025 * sin(uv.x * 90.0 + uv.y * 55.0 + frameTimeCounter * 2.0);
    water.rgb = mix(water.rgb, vec3(0.12, 0.42, 0.58), 0.3) + ripple;
#endif
    /* DRAWBUFFERS:0 */
    gl_FragData[0] = water;
}
