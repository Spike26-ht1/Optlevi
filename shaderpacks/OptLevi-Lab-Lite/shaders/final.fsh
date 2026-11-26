#version 120
#include "/lib/grade.glsl"
varying vec2 uv;
uniform sampler2D colortex0;
void main() {
    vec3 color = texture2D(colortex0, uv).rgb;
    gl_FragColor = vec4(labGrade(color), 1.0);
}
