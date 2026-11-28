#version 120
varying vec2 uv;
uniform sampler2D colortex0;
uniform sampler2D optNativeTerrainDepth;
uniform mat4 gbufferProjectionInverse;
uniform float viewWidth;
uniform float viewHeight;
vec3 positionAt(vec2 coord,float depth){
    vec4 p=gbufferProjectionInverse*vec4(coord*2.0-1.0,depth*2.0-1.0,1.0);
    return p.xyz/p.w;
}
void main(){
    vec4 base=texture2D(colortex0,uv);
    float depth=texture2D(optNativeTerrainDepth,uv).r;
    if(depth>=0.99999){gl_FragColor=base;return;}
    vec3 center=positionAt(uv,depth);
    vec2 px=1.0/vec2(viewWidth,viewHeight);
    float occlusion=0.0;
    // Eight screen-space samples of actual terrain depth. This is contact
    // shading, not sun-cast shadows or a substitute for Java shadowtex.
    for(int i=0;i<8;++i){
        float a=float(i)*0.78539816;
        vec2 q=clamp(uv+vec2(cos(a),sin(a))*px*5.0,px,vec2(1.0)-px);
        float z=texture2D(optNativeTerrainDepth,q).r;
        if(z<0.99999){
            vec3 other=positionAt(q,z);
            vec3 delta=other-center;
            float distanceTo=length(delta);
            if(distanceTo>0.02&&distanceTo<2.0&&delta.z>0.025)
                occlusion+=max(0.0,1.0-distanceTo*0.5)*clamp(delta.z/distanceTo,0.0,1.0);
        }
    }
    float shade=1.0-clamp(occlusion*0.24,0.0,0.48);
    gl_FragColor=vec4(base.rgb*shade,base.a);
}
