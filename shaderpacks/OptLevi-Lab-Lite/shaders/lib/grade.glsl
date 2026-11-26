// Original OptLevi Lab color transform.
#define SATURATION 1.12 // [1.0 1.12 1.25]
#define EXPOSURE 1.05 // [1.0 1.05 1.15]
vec3 labGrade(vec3 color) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, SATURATION);
    return clamp(color * EXPOSURE, 0.0, 1.0);
}
