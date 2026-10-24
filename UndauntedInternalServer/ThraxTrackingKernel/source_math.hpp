#pragma once
// Original implementations of measured scalar math contracts. Constants are
// file-backed in the pinned executable; source_oracle.py supplies byte-level controls.
#include <array>
#include <cmath>
#include <algorithm>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace feareater_tracking::source_math {
constexpr float pi=3.1415927410125732f, half_pi=1.5707963705062866f;
constexpr float radians_to_degrees=57.2957763671875f;
inline float inverse_sqrt(float n) {
#if defined(_M_X64) || defined(__x86_64__)
    float r=_mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(n)));
#else
    float r=1.f/std::sqrt(n);
#endif
    r=r+r*(.5f-n*.5f*r*r);r=r+r*(.5f-n*.5f*r*r);return r;
}
inline float atan2(float y,float x) {
    const float ax=std::fabs(x),ay=std::fabs(y);const bool swapped=ax<ay;
    const float high=swapped?ay:ax,low=swapped?ax:ay;if(high==0)return 0;
    const float r=low/high,v=r*r;
    float p=v*.0072128851898014545f-.03505967929959297f;
    p=p*v+.08167587965726852f;p=p*v-.13374657928943634f;
    p=p*v+.19856563210487366f;p=p*v-.3332499861717224f;
    float angle=r*(p*v+1.f);if(swapped)angle=half_pi-angle;
    if(x<0)angle=pi-angle;if(y<0)angle=-angle;return angle;
}
struct SinCos {float sin,cos;};
inline SinCos polynomial(float x,float cosine_sign) {
    const float v=x*x;
    float s=.0000027525561563379597f-v*2.388985897994189e-8f;
    s=s*v-.00019840874301735312f;s=s*v+.008333330973982811f;
    s=s*v-.1666666716337204f;s=(s*v+1.f)*x;
    float c=.0000247604948526714f-v*2.605161455448979e-7f;
    c=c*v-.0013888378161936998f;c=c*v+.04166663810610771f;
    c=c*v-.5f;c=(c*v+1.f)*cosine_sign;return{s,c};
}
inline SinCos sincos(float radians) {
    const float turns=radians*.15915493667125702f+(radians<0?-.5f:.5f);
    float x=radians-float(int(turns))*6.2831854820251465f,sign=1;
    if(x>half_pi){x=pi-x;sign=-1;}else if(x< -half_pi){x=-pi-x;sign=-1;}
    return polynomial(x,sign);
}
inline std::array<float,4> direction(std::array<float,3> v) {
    const float horizontal=std::sqrt(v[0]*v[0]+v[1]*v[1]);
    const auto y=sincos(atan2(v[1],v[0])*.5f),p=sincos(atan2(v[2],horizontal)*.5f);
    return{y.sin*p.sin,-y.cos*p.sin,y.sin*p.cos,y.cos*p.cos};
}
inline std::array<float,4> rotator(float pitch,float yaw) {
    // The configured pitch/yaw are already bounded; these match the native
    // SIMD polynomial without its unnecessary whole-turn range reduction.
    const auto p=polynomial(pitch*.008726646192371845f,1),y=polynomial(yaw*.008726646192371845f,1);
    return{p.sin*y.sin,-p.sin*y.cos,p.cos*y.sin,p.cos*y.cos};
}
inline float asin(float x) {
    const float v=std::fabs(x);
    float p=.006670089904218912f-v*.0012624911032617092f;
    p=p*v-.01708812639117241f;p=p*v+.03089188039302826f;
    p=p*v-.050174303352832794f;p=p*v+.08897899091243744f;
    p=p*v-.21459880471229553f;p=p*v+1.570796251296997f;
    const float a=std::sqrt(std::max(0.f,1.f-v))*p;
    return x<0?a-1.570796251296997f:1.570796251296997f-a;
}
}
