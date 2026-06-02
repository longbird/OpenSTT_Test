// 스트리밍(상태 유지) 8kHz → 16kHz 업샘플러 (송신측 파이프라인용).
//
// 블록 단위 resample_to_16k 와 달리 FIR 딜레이라인을 청크 간에 유지하므로,
// 통화처럼 20ms(160샘플) 패킷이 연속 도착하는 스트림에서 경계 아티팩트 없이
// 연속적인 16k 출력을 만든다. 입력 n 샘플 → 출력 2n 샘플.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace asr {

class StreamingResampler8kTo16k {
public:
    StreamingResampler8kTo16k();

    // 8kHz PCM16 청크를 입력하면 16kHz PCM16(2배 길이)을 반환.
    std::vector<int16_t> process(const int16_t* in, size_t n);

    int output_rate() const { return 16000; }

private:
    std::vector<double> h_;     // 저역통과 FIR(업샘플 게인 보정 포함)
    int taps_;                  // 탭 수
    size_t hist_len_;           // 유지할 과거 입력 샘플 수(= m/2)
    std::vector<int16_t> hist_; // 직전 입력 꼬리(필터 컨텍스트)
};

} // namespace asr
