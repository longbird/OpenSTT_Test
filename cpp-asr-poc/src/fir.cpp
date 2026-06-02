#include "fir.hpp"
#include <cmath>

namespace asr {

std::vector<double> make_lowpass_fir(double fc, int num_taps) {
    constexpr double kPi = 3.14159265358979323846;
    std::vector<double> h(num_taps);
    int m = num_taps - 1;
    double sum = 0.0;
    for (int n = 0; n < num_taps; ++n) {
        double x = n - m / 2.0;
        double sinc = (std::abs(x) < 1e-9) ? 2.0 * fc
                                           : std::sin(2.0 * kPi * fc * x) / (kPi * x);
        double win = 0.54 - 0.46 * std::cos(2.0 * kPi * n / m); // Hamming
        h[n] = sinc * win;
        sum += h[n];
    }
    for (double& v : h) v /= sum;
    return h;
}

} // namespace asr
