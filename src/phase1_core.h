#pragma once
#include <algorithm>
#include <cmath>

struct Vec3 { float x, y, z; };
inline Vec3 add(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 mul(Vec3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float length(Vec3 a) { return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z); }
inline Vec3 clamp_distance(Vec3 p, Vec3 origin, float limit) {
    Vec3 d{p.x-origin.x,p.y-origin.y,p.z-origin.z};
    float n=length(d);
    return n>limit && n>0 ? add(origin,mul(d,limit/n)) : p;
}
inline float deadzone(short v, int zone=7000) {
    int a=std::abs(static_cast<int>(v));
    if(a<=zone) return 0;
    float n=static_cast<float>(a-zone)/static_cast<float>(32767-zone);
    return (v<0?-1.f:1.f)*std::min(n,1.f);
}
inline bool held_combo(unsigned buttons, unsigned mask, unsigned long long now,
                       unsigned long long &since, bool &latched, unsigned hold=600) {
    if((buttons&mask)!=mask) { since=0; latched=false; return false; }
    if(!since) since=now;
    if(!latched && now-since>=hold) {latched=true; return true;}
    return false;
}
// GTA SA camera basis (CMatrix right/forward/up, Z up). Yaw 0 looks along +Y.
struct Basis { Vec3 right, forward, up; };
inline Basis basis_from_angles(float yaw, float pitch, float roll) {
    float sy=std::sin(yaw),cy=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch);
    Vec3 right{cy,-sy,0},front{sy*cp,cy*cp,sp},up{-sy*sp,-cy*sp,cp};
    float cr=std::cos(roll),sr=std::sin(roll);
    return {add(mul(right,cr),mul(up,sr)),front,add(mul(up,cr),mul(right,-sr))};
}
// Matches a basis to the game's own handedness: sign of right.(forward x up) measured on the live camera matrix.
// A mirrored matrix makes RwFrameOrthoNormalize flip axes, which shows as random 180 degree turns or an upside-down view.
inline float handedness(Vec3 right, Vec3 forward, Vec3 up) { return dot(right,cross(forward,up))<0 ? -1.f : 1.f; }
inline Basis with_handedness(Basis b, float sign) { if(sign<0) b.right=mul(b.right,-1.f); return b; }
// Removes the part of a move that goes into a surface (normal points out of it), so the camera slides along walls.
inline Vec3 slide(Vec3 move, Vec3 normal) { float into=dot(move,normal); return into<0 ? sub(move,mul(normal,into)) : move; }
inline Vec3 normalized(Vec3 v) { float n=length(v); return n>0 ? mul(v,1.f/n) : v; }
inline void angles_from_forward(Vec3 f, float &yaw, float &pitch) {
    float n=length(f); if(n<=0) { yaw=0; pitch=0; return; }
    f=mul(f,1.f/n);
    yaw=std::atan2(f.x,f.y); pitch=std::asin(std::clamp(f.z,-1.f,1.f));
}

// Rodrigues rotation of v about unit axis k by the angle whose cosine/sine are c/s.
inline Vec3 rotate_axis(Vec3 v, Vec3 k, float c, float s) { return add(add(mul(v, c), mul(cross(k, v), s)), mul(k, dot(k, v) * (1 - c))); }
// Where a head should look: toward the target, but at most maxYaw sideways from the body's facing and maxPitch up or down.
// bodyFacing is used flattened; the result is a unit vector.
inline Vec3 limited_look(Vec3 bodyFacing, Vec3 toTarget, float maxYaw, float maxPitch) {
    Vec3 f = normalized(Vec3{bodyFacing.x, bodyFacing.y, 0}); Vec3 t = normalized(toTarget);
    float bodyYaw = std::atan2(f.y, f.x), yaw = std::atan2(t.y, t.x) - bodyYaw;
    while (yaw > 3.14159265f) yaw -= 6.2831853f; while (yaw < -3.14159265f) yaw += 6.2831853f;
    yaw = std::clamp(yaw, -maxYaw, maxYaw);
    float pitch = std::clamp(std::atan2(t.z, std::sqrt(t.x * t.x + t.y * t.y)), -maxPitch, maxPitch);
    float a = bodyYaw + yaw; return {std::cos(pitch) * std::cos(a), std::cos(pitch) * std::sin(a), std::sin(pitch)};
}
