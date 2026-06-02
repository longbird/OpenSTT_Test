#include "streaming_resampler.hpp"
#include "fir.hpp"
#include <cmath>

namespace asr {

namespace {
int16_t clamp16(double v) {
    if (v > 32767.0) return 32767;
    if (v < -32768.0) return -32768;
    return static_cast<int16_t>(std::lround(v));
}
} // namespace

StreamingResampler8kTo16k::StreamingResampler8kTo16k() {
    taps_ = 47;
    h_ = make_lowpass_fir(0.25, taps_); // 16k 기준 4kHz 차단
    for (double& v : h_) v *= 2.0;      // 업샘플(x2) 게인 보정
    // 선형위상 FIR(중심 m/2)의 그룹지연 = m/2(업샘플 도메인) → 입력 m/2 샘플 컨텍스트 필요
    int m = taps_ - 1;
    hist_len_ = static_cast<size_t>(m / 2);
    hist_.assign(hist_len_, 0); // 워밍업: 0으로 시작
}

// 업샘플 u[2t]=buf[t], u[2t+1]=0. 출력 y[k]=Σ_j h[j]·u[k-j].
// buf = [hist_(H) | in(n)]. 새 입력 i(=buf index t=H+i)에 대응하는 출력 k=2t, 2t+1 은
// buf index (k-j)/2 가 i..H+i 범위(모두 buf 내부)라 완전 계산 가능.
std::vector<int16_t> StreamingResampler8kTo16k::process(const int16_t* in, size_t n) {
    const size_t H = hist_len_;
    std::vector<int16_t> buf;
    buf.reserve(H + n);
    buf.insert(buf.end(), hist_.begin(), hist_.end());
    buf.insert(buf.end(), in, in + n);

    const int m = taps_ - 1;
    std::vector<int16_t> out;
    out.reserve(2 * n);

    for (size_t i = 0; i < n; ++i) {
        long t = static_cast<long>(H + i);
        for (int p = 0; p < 2; ++p) {            // 두 출력 위상
            long k = 2 * t + p;
            double acc = 0.0;
            for (int j = p; j <= m; j += 2) {    // u 가 0이 아닌 항만(짝수 k-j)
                long src = (k - j) / 2;          // buf 입력 인덱스
                if (src < 0) break;
                if (static_cast<size_t>(src) < buf.size())
                    acc += h_[j] * buf[static_cast<size_t>(src)];
            }
            out.push_back(clamp16(acc));
        }
    }

    // 다음 호출용 꼬리(마지막 H개 입력 샘플) 유지
    if (buf.size() >= H)
        hist_.assign(buf.end() - static_cast<long>(H), buf.end());
    else
        hist_ = buf;
    return out;
}

} // namespace asr
