// OptLevi Lab WorldFX 0.2 - original mobile-focused renderer.
// These are compile-time options, not uniforms requiring a material.bin.
#define LAB_BLOOM
#define LAB_EXPOSURE 1.05 // [1.0 1.05 1.12]
#define LAB_SATURATION 1.12 // [1.0 1.12 1.22]
#define LAB_BLOOM_STRENGTH 0.18 // [0.12 0.18 0.28]
#define LAB_VIGNETTE 0.08 // [0.0 0.08 0.14]
vec3 labGrade(vec3 c) {
    float l = dot(c,vec3(0.2126,0.7152,0.0722));
    c=mix(vec3(l),c,LAB_SATURATION);
    c=max(c,vec3(0.0))*LAB_EXPOSURE;
    // Original soft shoulder: retain bright highlights without hard clipping.
    return c/(1.0+0.10*c);
}
