#include "resample.hpp"
#include <algorithm>
#include <cmath>

namespace asr {

namespace {

constexpr double kPi = 3.14159265358979323846;

// fc = 정규화 차단주파수(출력 샘플레이트 기준, cycles/sample), num_taps 홀수 권장.
std::vector<double> make_lowpass(double fc, int num_taps) {
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
    for (double& v : h) v /= sum; // DC 정규화
    return h;
}

int16_t clamp16(double v) {
    if (v > 32767.0) return 32767;
    if (v < -32768.0) return -32768;
    return static_cast<int16_t>(std::lround(v));
}

// 2배 업샘플(zero-stuff) + 저역통과 FIR. 폴리페이즈로 0곱 연산을 생략.
std::vector<int16_t> upsample2(const std::vector<int16_t>& in) {
    const int taps = 47;
    // 차단 0.25(=16k 기준 4kHz). 업샘플 게인 보정 위해 ×2.
    std::vector<double> h = make_lowpass(0.25, taps);
    for (double& v : h) v *= 2.0;

    size_t out_n = in.size() * 2;
    std::vector<int16_t> out(out_n);
    int m = taps - 1;
    // 업샘플 신호 y[k]: 짝수 인덱스 = in[k/2], 홀수 = 0
    for (size_t k = 0; k < out_n; ++k) {
        double acc = 0.0;
        // y[k - j] 가 0이 아니려면 (k - j)가 짝수 → 입력 인덱스 (k-j)/2
        for (int j = (static_cast<int>(k) & 1); j <= m; j += 2) {
            long src = static_cast<long>(k) - j;
            if (src < 0) break;
            size_t in_idx = static_cast<size_t>(src) / 2;
            if (in_idx < in.size()) acc += h[j] * in[in_idx];
        }
        out[k] = clamp16(acc);
    }
    return out;
}

std::vector<int16_t> linear_resample(const std::vector<int16_t>& in, int in_rate) {
    if (in.empty()) return {};
    double ratio = 16000.0 / in_rate;
    size_t out_n = static_cast<size_t>(in.size() * ratio);
    std::vector<int16_t> out(out_n);
    for (size_t i = 0; i < out_n; ++i) {
        double src = i / ratio;
        size_t i0 = static_cast<size_t>(src);
        double frac = src - i0;
        int16_t a = in[std::min(i0, in.size() - 1)];
        int16_t b = in[std::min(i0 + 1, in.size() - 1)];
        out[i] = clamp16(a + (b - a) * frac);
    }
    return out;
}

} // namespace

std::vector<int16_t> resample_to_16k(const std::vector<int16_t>& in, int in_rate) {
    if (in_rate == 16000) return in;
    if (in_rate == 8000) return upsample2(in);
    return linear_resample(in, in_rate);
}

} // namespace asr
