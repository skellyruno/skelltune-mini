#include "../dsp/PhaseCrossfadeLut.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
int main() {
    skelly::PhaseCrossfadeLut lut;
    lut.prepare();
    constexpr double pi = 3.14159265358979323846;
    int samples = 0;
    double worstError = 0;
    for (int size = 1; size <= 1536; ++size) {
        double prior = 0.0;
        for (int i = 0; i < size; ++i) {
            const double actual = lut.blend(i, size);
            const double exact = 0.5 - 0.5*std::cos(pi*(i+1.0)/size);
            worstError = std::max(worstError, std::abs(exact-actual));
            if (!(std::isfinite(actual) && actual >= prior && actual <= 1.0 &&
                  std::abs(exact-actual) < 1.3e-6)) {
                std::cerr << "Bad window: " << size << ' ' << i << ' ' << actual << '\n';
                return 1;
            }
            prior = actual;
            ++samples;
        }
        if (lut.blend(size-1,size) != 1.0) return 2;
    }
    if (lut.blend(0, 0) != 0.0) return 3;
    std::cout << "PASS " << samples << " cycle-blend samples; worst error " << worstError << '\n';
}
