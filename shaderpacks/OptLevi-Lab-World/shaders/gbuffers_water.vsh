#version 120
varying vec2 uv;
varying vec4 tint;
varying vec3 normalView;
varying vec3 worldRelative;
varying vec2 lightUV;
void main() {
    uv = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;
    tint = gl_Color;
    normalView = normalize(gl_NormalMatrix * gl_Normal);
    lightUV = (gl_TextureMatrix[1] * gl_MultiTexCoord1).xy;
    worldRelative = gl_Vertex.xyz;
    gl_Position = ftransform();
}
