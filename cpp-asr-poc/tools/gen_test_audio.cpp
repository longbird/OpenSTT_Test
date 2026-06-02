// 합성 테스트 오디오 생성: 묵음 사이에 톤 버스트(=발화 대용)를 배치.
// 실제 통화 데이터 없이 세그멘터 동작을 수동 확인하기 위한 도구.
//
// 사용: gen_test_audio out.wav [sample_rate]
#include "wav_io.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {
void append_silence(std::vector<int16_t>& v, int rate, double ms, double noise_amp,
                    std::mt19937& rng) {
    std::normal_distribution<double> nd(0.0, noise_amp);
    size_t n = static_cast<size_t>(rate * ms / 1000.0);
    for (size_t i = 0; i < n; ++i) {
        double s = nd(rng);
        v.push_back(static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, s))));
    }
}
void append_tone(std::vector<int16_t>& v, int rate, double ms, double freq, double amp) {
    size_t n = static_cast<size_t>(rate * ms / 1000.0);
    for (size_t i = 0; i < n; ++i) {
        // 페이드 인/아웃으로 클릭 방지
        double t = static_cast<double>(i) / rate;
        double env = 1.0;
        double fade = std::min(ms * 0.1, 30.0) / 1000.0;
        double cur = static_cast<double>(i) / rate;
        double end = ms / 1000.0;
        if (cur < fade) env = cur / fade;
        else if (cur > end - fade) env = (end - cur) / fade;
        double s = amp * env * std::sin(2.0 * M_PI * freq * t);
        v.push_back(static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, s))));
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: gen_test_audio out.wav [rate]\n"); return 2; }
    std::string out = argv[1];
    int rate = (argc >= 3) ? std::atoi(argv[2]) : 16000;

    std::mt19937 rng(1234);
    std::vector<int16_t> v;
    double noise = 60.0; // 낮은 배경 잡음

    // 구조: [무음0.5s][톤0.6s][무음0.4s][톤0.7s][무음0.5s]  → 2개 발화 기대
    append_silence(v, rate, 500, noise, rng);
    append_tone(v, rate, 600, 300.0, 9000.0);
    append_silence(v, rate, 400, noise, rng);
    append_tone(v, rate, 700, 220.0, 12000.0);
    append_silence(v, rate, 500, noise, rng);

    std::string err;
    if (!asr::write_wav_pcm16_mono(out, rate, v, err)) {
        std::fprintf(stderr, "write error: %s\n", err.c_str());
        return 1;
    }
    std::printf("wrote %s (%d Hz, %zu samples, %.2f s)\n",
                out.c_str(), rate, v.size(), static_cast<double>(v.size()) / rate);
    return 0;
}
