#include "resample.hpp"
#include "fir.hpp"
#include <algorithm>
#include <cmath>

namespace asr {

namespace {

int16_t clamp16(double v) {
    if (v > 32767.0) return 32767;
    if (v < -32768.0) return -32768;
    return static_cast<int16_t>(std::lround(v));
}

// 2배 업샘플(zero-stuff) + 저역통과 FIR. 폴리페이즈로 0곱 연산을 생략.
std::vector<int16_t> upsample2(const std::vector<int16_t>& in) {
    const int taps = 47;
    // 차단 0.25(=16k 기준 4kHz). 업샘플 게인 보정 위해 ×2.
    std::vector<double> h = make_lowpass_fir(0.25, taps);
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
