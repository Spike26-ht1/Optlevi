#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace modmenu::android::shaderbridge::camera {
using Matrix=std::array<float,16>;
using Vec=std::array<double,3>;
// Invert a supplied, column-major matrix without replacing a singular
// matrix by an identity. Validate the result after rounding to GPU floats.
inline bool invert(Matrix const& input,Matrix& output){
    double m[4][8]{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){
        double v=input[c*4+r];if(!std::isfinite(v))return false;
        m[r][c]=v;m[r][4+c]=r==c?1.0:0.0;
    }
    for(int c=0;c<4;++c){
        int pivot=c;
        for(int r=c+1;r<4;++r)if(std::abs(m[r][c])>std::abs(m[pivot][c]))pivot=r;
        if(std::abs(m[pivot][c])<1e-12)return false;
        for(int k=0;k<8;++k)std::swap(m[c][k],m[pivot][k]);
        double divisor=m[c][c];for(double& v:m[c])v/=divisor;
        for(int r=0;r<4;++r)if(r!=c){
            double scale=m[r][c];for(int k=0;k<8;++k)m[r][k]-=scale*m[c][k];
        }
    }
    Matrix result{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){
        result[c*4+r]=float(m[r][4+c]);if(!std::isfinite(result[c*4+r]))return false;
    }
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){
        double value=0;
        for(int k=0;k<4;++k)value+=double(input[k*4+r])*result[c*4+k];
        if(std::abs(value-(r==c?1.0:0.0))>0.001)return false;
    }
    output=result;return true;
}
struct ShadowSettings {
    int resolution=1024;
    float distance=160,nearPlane=-100.05f,farPlane=156,interval=2,rotation=0;
};
struct ShadowCamera { Matrix view{},projection{},viewInverse{},projectionInverse{}; };
inline Matrix multiply(Matrix const& a,Matrix const& b){
    Matrix result{};
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)
        for(int k=0;k<4;++k)result[c*4+r]+=a[k*4+r]*b[c*4+k];
    return result;
}
inline bool validShadowSettings(ShadowSettings const& s){
    return s.resolution>=16&&s.resolution<=8192&&
        std::isfinite(s.distance)&&s.distance>0&&s.distance<=16384&&
        std::isfinite(s.nearPlane)&&std::isfinite(s.farPlane)&&s.farPlane-s.nearPlane>0.001f&&
        std::isfinite(s.interval)&&s.interval>=0&&s.interval<=1024&&
        std::isfinite(s.rotation)&&std::abs(s.rotation)<=360;
}
// Java 1.21 Iris-compatible orthographic shadow convention. Coordinates
// are camera-relative Java world coordinates, NOT native Bedrock matrices.
inline bool shadowCamera(ShadowSettings const& s,float sunAngle,Vec const& eye,ShadowCamera& output){
    if(!validShadowSettings(s)||!std::isfinite(sunAngle)||sunAngle<0||sunAngle>1)return false;
    for(double x:eye)if(!std::isfinite(x))return false;
    constexpr double pi=3.14159265358979323846;
    double angle=double(sunAngle);if(angle>0.5)angle-=0.5;
    double sky=angle-0.25;double z=-2*pi*sky,x=s.rotation*pi/180;
    float cz=float(std::cos(z)),sz=float(std::sin(z)),cx=float(std::cos(x)),sx=float(std::sin(x));
    Matrix rx90={1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1};
    Matrix rz={cz,sz,0,0, -sz,cz,0,0, 0,0,1,0, 0,0,0,1};
    Matrix rx={1,0,0,0, 0,cx,sx,0, 0,-sx,cx,0, 0,0,0,1};
    ShadowCamera result;result.view=multiply(multiply(rx90,rz),rx);
    if(s.interval>0){
        // Preserve Java signed remainder convention for grid snapping.
        for(int r=0;r<3;++r)for(int c=0;c<3;++c)
            result.view[12+r]+=result.view[c*4+r]*float(std::fmod(eye[c],double(s.interval))-s.interval*0.5);
    }
    float span=s.farPlane-s.nearPlane;
    result.projection={1/s.distance,0,0,0, 0,1/s.distance,0,0,
        0,0,-2/span,0, 0,0,-(s.farPlane+s.nearPlane)/span,1};
    if(!invert(result.view,result.viewInverse)||!invert(result.projection,result.projectionInverse))return false;
    output=result;return true;
}
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
