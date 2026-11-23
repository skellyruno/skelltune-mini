#pragma once
// Three-second absolute DAW time viewport for pitch rendering and click/erase.
// Host-clock-free sessions keep the previous evenly-spaced live-note fallback.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
namespace skelly {
// Fixed, message-thread-only view of the phrase being edited. Audio and live
// analyzer history continue advancing underneath this immutable gesture view.
template <std::size_t Capacity>
class PitchGraphGestureSnapshot {
    static_assert(Capacity > 0, "graph history must be nonempty");
public:
    void freeze(int head, int available,
                const std::array<float,Capacity>& input,
                const std::array<float,Capacity>& output,
                const std::array<int64_t,Capacity>& clock) noexcept {
        frames_ = std::clamp(available,0,int(Capacity));
        const int end=((head%int(Capacity))+int(Capacity))%int(Capacity);
        for(int i=0;i<frames_;++i) {
            const auto index=std::size_t((end+int(Capacity)-frames_+i)%int(Capacity));
            input_[std::size_t(i)]=input[index];
            output_[std::size_t(i)]=output[index];
            clock_[std::size_t(i)]=clock[index];
        }
        newestMs_=frames_>0?clock_[std::size_t(frames_-1)]:-1;
    }
    void release() noexcept { frames_=0; newestMs_=-1; }
    bool active() const noexcept { return frames_>0; }
    int count() const noexcept { return frames_; }
    int64_t newestMs() const noexcept { return newestMs_; }
    float inputAt(int i) const noexcept { return input_[std::size_t(i)]; }
    float outputAt(int i) const noexcept { return output_[std::size_t(i)]; }
    int64_t timeAt(int i) const noexcept { return clock_[std::size_t(i)]; }
private:
    std::array<float,Capacity> input_{},output_{};
    std::array<int64_t,Capacity> clock_{};
    int frames_=0;
    int64_t newestMs_=-1;
};
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
