#pragma once
#include <array>
namespace skelly::playground {
// Deterministic default if not calibrated. Owner audition CI replaces only
// this header after a bounded sweep; exact generated source accompanies ZIP.
// Legacy slot order is handled by the Bank. Classic/Crystal MUST be zero.
inline constexpr std::array<unsigned,5> finalListeningFlags{{0,0,0,0,0}};
static_assert(finalListeningFlags[2]==0 && finalListeningFlags[3]==0);
}
