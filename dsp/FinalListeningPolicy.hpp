#pragma once
#include <array>
namespace skelly::playground {
inline constexpr std::array<unsigned,5> finalListeningFlags{{0,5,0,0,0}};
static_assert(finalListeningFlags[2]==0 && finalListeningFlags[3]==0);
}
