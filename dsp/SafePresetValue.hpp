#pragma once
// SkellyTune: independent schema for validating host-supplied state.
#include <algorithm>
#include <charconv>
#include <system_error>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace skelly {
struct SavedParameter {
    const char* id;
    double minimum;
    double maximum;
    bool integer;
};

inline constexpr std::array<SavedParameter, 16> kSavedParameters {{
    {"speed", 0.0, 200.0, false},
    {"amount", 0.0, 100.0, false},
    {"mix", 0.0, 100.0, false},
    {"transient", 0.0, 100.0, false},
    {"air", 0.0, 100.0, false},
    {"vibratoPreserve", 0.0, 100.0, false},
    {"flex", 0.0, 60.0, false},
    {"formantPreserve", 0.0, 100.0, false},
    {"cycleStabilize", 0.0, 100.0, false},
    {"phaseAlign", 0.0, 1.0, true},
    {"a4", 400.0, 480.0, false},
    {"synthesisMode", 0.0, 1.0, true},
    {"key", 0.0, 11.0, true},
    {"scale", 0.0, 2.0, true},
    {"noteMask", 0.0, 4095.0, true},
    {"bypass", 0.0, 1.0, true},
}};

// Strict locale-independent number parser (no NaN, infinity, or trailing text).
inline std::optional<double> parseSavedNumber(std::string_view text) noexcept {
    if (text.empty() || text.size() > 63) return std::nullopt;
    double number = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                        number, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(number)) return std::nullopt;
    return number;
}
inline const SavedParameter* findSavedParameter(std::string_view id) noexcept {
    for (const auto& spec : kSavedParameters)
        if (id == spec.id) return &spec;
    return nullptr;
}
inline std::optional<double> sanitizedSavedValue(std::string_view id, double value) noexcept {
    const auto* spec = findSavedParameter(id);
    if (spec == nullptr || !std::isfinite(value)) return std::nullopt;
    value = std::clamp(value, spec->minimum, spec->maximum);
    if (spec->integer) value = std::round(value);
    return value;
}
} // namespace skelly
