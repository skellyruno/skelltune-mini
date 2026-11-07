#include "../dsp/SafePresetValue.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <string>
int main() {
    using namespace skelly;
    int checks = 0;
    const auto require = [&](bool pass, const char* name) {
        ++checks;
        if (!pass) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
    };
    require(kSavedParameters.size() == 16, "parameter count");
    require(parseSavedNumber("25.5") == 25.5, "decimal parse");
    require(parseSavedNumber("0") == 0.0, "true zero parse");
    require(parseSavedNumber("1.25e2") == 125.0, "scientific parse");
    require(!parseSavedNumber(""), "empty parse");
    require(!parseSavedNumber("garbage"), "invalid parse");
    require(!parseSavedNumber("1.5garbage"), "trailing parse");
    require(!parseSavedNumber("nan"), "nan parse rejected");
    require(!parseSavedNumber("inf"), "infinity parse rejected");
    require(!parseSavedNumber("1e309"), "overflow parse rejected");
    require(!parseSavedNumber(std::string_view("1.0\0junk", 8)), "embedded NUL parse rejected");
    require(!parseSavedNumber(std::string(70, '2')), "oversized numeric parse rejected");
    for (const auto& spec : kSavedParameters) {
        require(findSavedParameter(spec.id) != nullptr, "known id");
        require(sanitizedSavedValue(spec.id, spec.minimum) == spec.minimum, "minimum accepted");
        require(sanitizedSavedValue(spec.id, spec.maximum) == spec.maximum, "maximum accepted");
        require(sanitizedSavedValue(spec.id, -100000.0) == spec.minimum, "underflow clamped");
        require(sanitizedSavedValue(spec.id, +100000.0) == spec.maximum, "overflow clamped");
        require(!sanitizedSavedValue(spec.id, std::numeric_limits<double>::infinity()), "inf rejected");
        require(!sanitizedSavedValue(spec.id, std::numeric_limits<double>::quiet_NaN()), "NaN rejected");
    }
    require(!sanitizedSavedValue("deprecatedParameter", 25.0), "old id rejected");
    require(!sanitizedSavedValue("", 1.0), "empty id rejected");
    require(sanitizedSavedValue("speed", 0.0) == 0.0, "true zero speed preserved");
    require(sanitizedSavedValue("speed", 1.0) == 1.0, "one ms preserved");
    require(sanitizedSavedValue("noteMask", 4095.0) == 4095.0, "full scale mask preserved");
    require(sanitizedSavedValue("noteMask", 12.6) == 13.0, "mask integer rounded");
    require(sanitizedSavedValue("key", 13.0) == 11.0, "key clamped");
    require(sanitizedSavedValue("scale", 9.0) == 2.0, "scale clamped");
    require(sanitizedSavedValue("bypass", 0.5) == 1.0, "bool rounded");
    std::cout << "PASS: " << checks << " state schema checks (Release and Debug).\n";
}
