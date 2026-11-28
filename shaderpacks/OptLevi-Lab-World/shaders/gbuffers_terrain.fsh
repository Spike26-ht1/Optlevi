#version 120
#include "/lib/settings.glsl"
varying vec2 uv;
varying vec4 tint;
varying vec3 normalView;
varying vec3 worldRelative;
varying vec2 lightUV;
uniform sampler2D texture;
#ifdef SHADOWS
uniform sampler2DShadow shadowtex0;
uniform mat4 shadowModelView;
uniform mat4 shadowProjection;
#endif
void main() {
    vec4 albedo = texture2D(texture, uv) * tint;
    float light = mix(0.35, 1.0, clamp(max(lightUV.x, lightUV.y), 0.0, 1.0));
#ifdef SHADOWS
    vec4 clip = shadowProjection * shadowModelView * vec4(worldRelative, 1.0);
    vec3 coord = clip.xyz / clip.w * 0.5 + 0.5;
    if (all(greaterThanEqual(coord, vec3(0.0))) && all(lessThanEqual(coord, vec3(1.0)))) {
        float visibility = shadow2D(shadowtex0, vec3(coord.xy, coord.z - 0.0015)).r;
        light *= mix(0.65, 1.0, visibility);
    }
#endif
    /* DRAWBUFFERS:01 */
    gl_FragData[0] = vec4(albedo.rgb * light, albedo.a);
    gl_FragData[1] = vec4(normalView * 0.5 + 0.5, 1.0);
}
