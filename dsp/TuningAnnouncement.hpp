#pragma once
#include <cstdint>

namespace skelly {
// Message-thread inbox. Skell Key re-announces the SAME tuning every second
// for late receivers. The transport version is a heartbeat, not a new edit.
class TuningAnnouncement {
public:
    bool acceptFull(std::uint64_t payload, int key) noexcept {
        if (payload == 0 || key == 0) return false;
        const bool changed = payload != full_ || key != key_;
        full_ = payload; key_ = key;
        return changed;
    }
    bool acceptKey(int key) noexcept {
        if (key == 0 || key == key_) return false;
        full_ = 0; key_ = key;
        return true;
    }
    void reset() noexcept { full_ = 0; key_ = 0; }
private:
    std::uint64_t full_ = 0;
    int key_ = 0;
};
} // namespace skelly
