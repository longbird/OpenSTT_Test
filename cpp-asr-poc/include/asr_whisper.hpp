// whisper.cpp 보조 전사 래퍼 (PoC-C).
//
// 역할: 통화 전체/세그먼트의 "전체 전사"를 생성 — 로그/감사/분쟁 대비 용도.
// 실시간 동의 판단은 PoC-B(Vosk)가 담당하고, whisper 는 오프 경로에서 동작한다.
//
// 주의(설계 문서 반영): initial_prompt 바이어싱은 무음/짧은 발화에서 환각·반복을
// 유발할 수 있어 기본값은 비워 둔다. 실험 시에만 주입한다.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace asr {

struct TranscribeConfig {
    std::string model_path;          // ggml-*.bin (예: ggml-small / base)
    std::string language = "ko";
    std::string initial_prompt = ""; // 기본 비움(바이어싱 위험)
    int n_threads = 4;
    bool translate = false;
};

struct TranscriptSegment {
    double t0_ms = 0.0;
    double t1_ms = 0.0;
    std::string text;
};

// 16kHz PCM16 mono 를 받아 전사 세그먼트를 반환.
class WhisperTranscriber {
public:
    explicit WhisperTranscriber(const TranscribeConfig& cfg);
    ~WhisperTranscriber();

    WhisperTranscriber(const WhisperTranscriber&) = delete;
    WhisperTranscriber& operator=(const WhisperTranscriber&) = delete;

    bool ok() const { return ctx_ != nullptr; }
    const std::string& error() const { return err_; }

    std::vector<TranscriptSegment> transcribe(const int16_t* pcm16k, size_t n);

private:
    TranscribeConfig cfg_;
    std::string err_;
    void* ctx_ = nullptr; // whisper_context*
};

} // namespace asr
