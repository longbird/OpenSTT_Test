// 하이브리드 파이프라인 오케스트레이터 (Vosk 실시간 + whisper 보조).
//
// 설계 문서의 이중 경로를 코드로 묶는다:
//   - 모든 오디오 청크 → 실시간 동의 엔진(Vosk)에 연속 공급
//   - VAD 세그먼트가 닫히면 → 해당 구간 오디오를 보조 전사 엔진(whisper)에 전달
//
// 엔진은 std::function 으로 주입한다(모델 비의존 → 페이크로 단위 테스트 가능).
// 실제 Vosk/whisper 결선은 CLI(main_pipeline)에서 수행.
#pragma once
#include "vad.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace asr {

class HybridPipeline {
public:
    // 실시간 동의 엔진에 PCM16(16k) 청크를 공급.
    using ConsentFeed = std::function<void(const int16_t*, size_t)>;
    // 닫힌 발화 세그먼트와 그 오디오를 보조 전사 엔진에 전달.
    using SegmentSink = std::function<void(const Segment&, const int16_t*, size_t)>;

    HybridPipeline(const VadConfig& vad, ConsentFeed consent, SegmentSink on_segment);

    // 16kHz PCM16 mono 청크 입력.
    void process(const int16_t* samples, size_t n);

    // 스트림 종료: 미종료 세그먼트 마감 후 전사 경로로 흘림.
    void finish();

    size_t segments_emitted() const { return emitted_; }

private:
    void flush_new_segments();
    size_t ms_to_idx(double ms) const;

    Segmenter seg_;
    ConsentFeed consent_;
    SegmentSink on_segment_;
    int sample_rate_;
    std::vector<int16_t> all_; // 세그먼트 슬라이싱용 누적 버퍼
    size_t emitted_ = 0;
};

} // namespace asr
