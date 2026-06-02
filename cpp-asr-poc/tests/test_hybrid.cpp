// 하이브리드 오케스트레이터 검증(페이크 엔진, 모델 불필요):
// 동의 경로엔 전체 오디오가 공급되고, 발화 세그먼트마다 전사 경로가 호출되는지.
#include "hybrid.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {
int g_fail = 0;
void check(bool c, const char* m) {
    std::printf("[%s] %s\n", c ? "PASS" : "FAIL", m);
    if (!c) g_fail++;
}
void add_silence(std::vector<int16_t>& v, int rate, double ms, std::mt19937& rng) {
    std::normal_distribution<double> nd(0.0, 60.0);
    size_t n = static_cast<size_t>(rate * ms / 1000.0);
    for (size_t i = 0; i < n; ++i) v.push_back(static_cast<int16_t>(nd(rng)));
}
void add_tone(std::vector<int16_t>& v, int rate, double ms, double f, double amp) {
    size_t n = static_cast<size_t>(rate * ms / 1000.0);
    for (size_t i = 0; i < n; ++i)
        v.push_back(static_cast<int16_t>(amp * std::sin(2 * M_PI * f * i / rate)));
}
} // namespace

int main() {
    using namespace asr;

    int rate = 16000;
    std::mt19937 rng(11);
    std::vector<int16_t> audio;
    add_silence(audio, rate, 500, rng);
    add_tone(audio, rate, 600, 300.0, 9000.0);
    add_silence(audio, rate, 400, rng);
    add_tone(audio, rate, 700, 220.0, 12000.0);
    add_silence(audio, rate, 500, rng);

    // 페이크 엔진
    size_t consent_samples = 0;
    int consent_calls = 0;
    std::vector<size_t> seg_lengths;

    VadConfig cfg;
    cfg.sample_rate = rate;
    HybridPipeline pipe(
        cfg,
        [&](const int16_t*, size_t n) { consent_samples += n; consent_calls++; },
        [&](const Segment& s, const int16_t* /*p*/, size_t n) {
            (void)s;
            seg_lengths.push_back(n);
        });

    // 청크(100ms) 단위로 스트리밍 공급
    size_t chunk = static_cast<size_t>(rate * 0.1);
    for (size_t off = 0; off < audio.size(); off += chunk) {
        size_t n = std::min(chunk, audio.size() - off);
        pipe.process(audio.data() + off, n);
    }
    pipe.finish();

    char buf[160];
    std::snprintf(buf, sizeof(buf), "consent fed all audio (%zu == %zu, calls=%d)",
                  consent_samples, audio.size(), consent_calls);
    check(consent_samples == audio.size() && consent_calls > 0, buf);

    std::snprintf(buf, sizeof(buf), "two speech segments routed to transcriber (got %zu)",
                  seg_lengths.size());
    check(seg_lengths.size() == 2, buf);

    if (seg_lengths.size() == 2) {
        // 첫 발화 ~600ms+패딩 → 대략 9600~13000 샘플 범위
        check(seg_lengths[0] > 8000 && seg_lengths[0] < 16000,
              "segment 1 sample length plausible");
        check(seg_lengths[1] > 9000 && seg_lengths[1] < 18000,
              "segment 2 sample length plausible");
    }
    check(pipe.segments_emitted() == 2, "segments_emitted == 2");

    std::printf("\n%s\n", g_fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_fail == 0 ? 0 : 1;
}
