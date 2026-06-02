// PoC-A 자동 검증: 합성 신호로 세그멘터/디코드 동작을 확인.
#include "g711.hpp"
#include "vad.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {
int g_fail = 0;
void check(bool cond, const char* msg) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond) g_fail++;
}

void add_silence(std::vector<int16_t>& v, int rate, double ms, std::mt19937& rng) {
    std::normal_distribution<double> nd(0.0, 60.0);
    size_t n = static_cast<size_t>(rate * ms / 1000.0);
    for (size_t i = 0; i < n; ++i) v.push_back(static_cast<int16_t>(nd(rng)));
}
void add_tone(std::vector<int16_t>& v, int rate, double ms, double f, double amp) {
    size_t n = static_cast<size_t>(rate * ms / 1000.0);
    for (size_t i = 0; i < n; ++i)
        v.push_back(static_cast<int16_t>(amp * std::sin(2.0 * M_PI * f * i / rate)));
}
} // namespace

int main() {
    // 1) G.711 라운드트립 sanity: μ-law 0xFF는 0 근처, 0x00은 최대 진폭 부호
    {
        int16_t z = asr::ulaw_to_pcm16(0xFF); // 무음 코드
        check(std::abs(z) < 64, "ulaw 0xFF decodes near zero");
        int16_t big = asr::ulaw_to_pcm16(0x00);
        check(std::abs(big) > 30000, "ulaw 0x00 decodes to large magnitude");

        // 인코드→디코드 라운드트립: μ-law 양자화 오차 내로 복원
        int maxerr = 0;
        for (int v = -32000; v <= 32000; v += 137) {
            int16_t back = asr::ulaw_to_pcm16(asr::pcm16_to_ulaw(static_cast<int16_t>(v)));
            int rel = std::abs(back - v) * 100 / (std::abs(v) + 256);
            if (rel > maxerr) maxerr = rel;
        }
        char rb[96];
        std::snprintf(rb, sizeof(rb), "ulaw encode/decode roundtrip within ~quant (maxrel=%d%%)", maxerr);
        check(maxerr < 15, rb);
    }

    // 2) 세그먼트 카운트: 무음/톤/무음/톤/무음 → 2 세그먼트
    {
        int rate = 16000;
        std::mt19937 rng(7);
        std::vector<int16_t> v;
        add_silence(v, rate, 500, rng);
        add_tone(v, rate, 600, 300.0, 9000.0);
        add_silence(v, rate, 400, rng);
        add_tone(v, rate, 700, 220.0, 12000.0);
        add_silence(v, rate, 500, rng);

        asr::VadConfig cfg;
        cfg.sample_rate = rate;
        asr::Segmenter seg(cfg);
        seg.process(v.data(), v.size());
        seg.finish();

        const auto& segs = seg.segments();
        char buf[128];
        std::snprintf(buf, sizeof(buf), "expected 2 segments, got %zu", segs.size());
        check(segs.size() == 2, buf);

        if (segs.size() == 2) {
            // 첫 발화는 대략 500ms 부근에서 시작(패딩/지연 감안 ±200ms)
            check(segs[0].start_ms > 250 && segs[0].start_ms < 650,
                  "segment 1 starts near 500ms");
            // 둘째 발화는 대략 1500ms 부근(500+600+400)
            check(segs[1].start_ms > 1250 && segs[1].start_ms < 1700,
                  "segment 2 starts near 1500ms");
            check(segs[0].duration_ms() > 400 && segs[0].duration_ms() < 1000,
                  "segment 1 duration plausible (~600ms+pad)");
        }
    }

    // 3) 순수 무음(낮은 잡음)은 0 세그먼트
    {
        int rate = 16000;
        std::mt19937 rng(3);
        std::vector<int16_t> v;
        add_silence(v, rate, 2000, rng);
        asr::VadConfig cfg;
        cfg.sample_rate = rate;
        asr::Segmenter seg(cfg);
        seg.process(v.data(), v.size());
        seg.finish();
        char buf[128];
        std::snprintf(buf, sizeof(buf), "pure silence yields 0 segments, got %zu",
                      seg.segments().size());
        check(seg.segments().empty(), buf);
    }

    std::printf("\n%s\n", g_fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_fail == 0 ? 0 : 1;
}
