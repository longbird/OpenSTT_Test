// PoC-B 보조 로직 검증 (libvosk 불필요): 리샘플러 + 동의 매칭/그래머.
#include "consent_match.hpp"
#include "resample.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int g_fail = 0;
void check(bool c, const char* m) {
    std::printf("[%s] %s\n", c ? "PASS" : "FAIL", m);
    if (!c) g_fail++;
}

// 영교차 횟수로 대략 주파수 추정
int zero_crossings(const std::vector<int16_t>& v) {
    int z = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i - 1] < 0) != (v[i] < 0)) z++;
    return z;
}
} // namespace

int main() {
    using namespace asr;

    // 1) 8k→16k: 길이 2배, 주파수(영교차 밀도) 보존
    {
        int in_rate = 8000;
        double freq = 1000.0;
        std::vector<int16_t> in;
        for (int i = 0; i < in_rate; ++i) // 1초
            in.push_back(static_cast<int16_t>(10000 * std::sin(2 * M_PI * freq * i / in_rate)));
        auto out = resample_to_16k(in, in_rate);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "8k->16k length doubles (%zu -> %zu)", in.size(), out.size());
        check(out.size() == in.size() * 2, buf);

        // 1kHz는 1초에 약 2000 영교차. 리샘플 후에도 유사해야 함(±10%).
        int zc = zero_crossings(out);
        std::snprintf(buf, sizeof(buf), "1kHz preserved after resample (zc=%d, expect ~2000)", zc);
        check(zc > 1800 && zc < 2200, buf);

        // 진폭이 발산하지 않음
        int16_t peak = 0;
        for (auto s : out) peak = std::max<int16_t>(peak, static_cast<int16_t>(std::abs(s)));
        std::snprintf(buf, sizeof(buf), "amplitude bounded (peak=%d)", peak);
        check(peak > 6000 && peak < 16000, buf);
    }

    // 2) 16k passthrough
    {
        std::vector<int16_t> in(1000, 123);
        auto out = resample_to_16k(in, 16000);
        check(out.size() == in.size() && out[0] == 123, "16k passthrough unchanged");
    }

    // 3) 동의 매칭
    {
        std::vector<std::string> ph = {"네", "오케이", "올려 주세요"};
        check(text_matches_consent("네", ph), "single token consent matches");
        check(text_matches_consent("음 그래 오케이", ph), "token within sentence matches");
        check(text_matches_consent("금액 올려 주세요", ph), "multi-word phrase matches");
        check(!text_matches_consent("아니요 싫어요", ph), "non-consent rejected");
        check(!text_matches_consent("", ph), "empty rejected");
        // "네"가 부분 포함된 다른 단어는 토큰 정확매칭이라 오탐 안 함
        check(!text_matches_consent("안네요", ph), "substring of token does not false-match");
    }

    // 4) 그래머 JSON
    {
        auto g = build_grammar_json({"네", "오케이"});
        check(g.find("\"네\"") != std::string::npos &&
              g.find("\"[unk]\"") != std::string::npos &&
              g.front() == '[' && g.back() == ']',
              "grammar json well-formed with [unk]");
    }

    std::printf("\n%s\n", g_fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_fail == 0 ? 0 : 1;
}
