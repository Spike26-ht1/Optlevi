#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace modmenu::android::shaderbridge::camera {
using Matrix=std::array<float,16>;
using Vec=std::array<double,3>;
struct Projection {
    Matrix matrix{},inverse{};
    float nearClip=0,farClip=0;
};
inline double dot(Vec a,Vec b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Vec scale(Vec a,double s){for(double& x:a)x*=s;return a;}
inline Vec cross(Vec a,Vec b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
// Recover P only for a centered, right-handed, finite OpenGL perspective
// P*V with a rigid view transform and independently observed camera eye.
// Never publish a guessed identity, orthographic or reversed-Z projection.
inline bool recoverProjection(Matrix const& vp,std::array<float,4> const& eye,Projection& output){
    for(float x:vp)if(!std::isfinite(x))return false;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(eye[i]))return false;
    Vec r0{vp[0],vp[4],vp[8]},r1{vp[1],vp[5],vp[9]},r2{vp[2],vp[6],vp[10]};
    Vec back{-vp[3],-vp[7],-vp[11]};
    double fx=std::sqrt(dot(r0,r0)),fy=std::sqrt(dot(r1,r1)),bz=std::sqrt(dot(back,back));
    constexpr double eps=2e-5;
    if(fx<0.01||fx>100||fy<0.01||fy>100||std::abs(bz-1)>eps)return false;
    Vec right=scale(r0,1/fx),up=scale(r1,1/fy);
    if(std::abs(dot(right,up))>eps||std::abs(dot(right,back))>eps||
       std::abs(dot(up,back))>eps||std::abs(dot(cross(right,up),back)-1)>eps)return false;
    double a=dot(r2,back);
    for(unsigned i=0;i<3;++i)if(std::abs(r2[i]-a*back[i])>eps)return false;
    double tx=vp[12]/fx,ty=vp[13]/fy,tz=-double(vp[15]);
    Vec e{eye[0],eye[1],eye[2]};
    // Confirm the eye is the origin of this particular view matrix, not
    // an unrelated player coordinate or a position from a different draw.
    double tolerance=0.005+std::sqrt(dot(e,e))*2e-6;
    if(std::abs(tx+dot(right,e))>tolerance||std::abs(ty+dot(up,e))>tolerance||
       std::abs(tz+dot(back,e))>tolerance)return false;
    double b=double(vp[14])-a*tz;
    if(!(a<-1.0000001&&b<0))return false;
    double n=b/(a-1),f=b/(a+1);
    if(!(n>0.0001&&n<100&&f>n&&f<1000000))return false;
    // Large coordinates can erase the near term in a float VP. Refuse
    // poorly conditioned subtraction rather than invent clip distances.
    if((std::abs(vp[14])+std::abs(a*tz))*2e-7>std::abs(b)*0.01)return false;
    Projection p;
    p.matrix[0]=float(fx);p.matrix[5]=float(fy);
    p.matrix[10]=float(a);p.matrix[11]=-1;p.matrix[14]=float(b);
    p.inverse[0]=float(1/fx);p.inverse[5]=float(1/fy);
    p.inverse[11]=float(1/b);p.inverse[14]=-1;p.inverse[15]=float(a/b);
    p.nearClip=float(n);p.farClip=float(f);
    output=p;
    return true;
}
}
