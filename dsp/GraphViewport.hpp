#pragma once
// Three-second absolute DAW time viewport for pitch rendering and click/erase.
// Host-clock-free sessions keep the previous evenly-spaced live-note fallback.
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace skelly {
inline float graphViewportTimeX(int64_t sampleMs,int64_t newestMs,
                                float left,float right) noexcept {
    if(newestMs<0 || sampleMs<0 || !std::isfinite(left) ||
       !std::isfinite(right) || right<=left)return left;
    const double age=std::clamp(double(newestMs)-double(sampleMs),0.0,3000.0);
    return right-float(age/3000.0)*float(right-left);
}
// Exact inverse of the visible three-second DAW pitch-time axis. Using the
// nearest cached GUI point for an ERASE hit can delete an adjacent note if
// the host drops/merges UI updates; the pointer's real time is authoritative.
inline int64_t graphViewportTimeMs(float x,int64_t newestMs,
                                  float left,float right) noexcept {
    if(newestMs<0 || !std::isfinite(x) || !std::isfinite(left) ||
       !std::isfinite(right) || !(right>left))return -1;
    const double fraction=std::clamp(
        (double(right)-double(x))/double(right-left),0.0,1.0);
    const int64_t time=std::int64_t(std::llround(
        double(newestMs)-fraction*3000.0));
    return std::max<int64_t>(0,time);
}
inline float graphViewportX(int index,int count,int64_t sampleMs,
                            int64_t newestMs,float left,float right) noexcept {
    if(newestMs>=0 && sampleMs>=0)
        return graphViewportTimeX(sampleMs,newestMs,left,right);
    if(count<=1)return right;
    return left+std::clamp(float(index)/float(count-1),0.0f,1.0f)*(right-left);
}
inline bool graphDiscontinuity(int64_t previousMs,int64_t currentMs) noexcept {
    return previousMs>=0 && currentMs>=0 &&
           (currentMs<previousMs || currentMs-previousMs>110);
}
inline float graphWheelPan(float center,float deltaY) noexcept {
    if(!std::isfinite(center) || !std::isfinite(deltaY))return 60.0f;
    return std::clamp(center+std::clamp(deltaY,-4.0f,4.0f)*8.0f,36.0f,96.0f);
}
} // namespace skelly
