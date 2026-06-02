#include "asr_whisper.hpp"
#include "whisper.h"

namespace asr {

WhisperTranscriber::WhisperTranscriber(const TranscribeConfig& cfg) : cfg_(cfg) {
    whisper_context_params cparams = whisper_context_default_params();
    ctx_ = whisper_init_from_file_with_params(cfg_.model_path.c_str(), cparams);
    if (!ctx_) {
        err_ = "whisper_init_from_file failed: " + cfg_.model_path;
    }
}

WhisperTranscriber::~WhisperTranscriber() {
    if (ctx_) whisper_free(static_cast<whisper_context*>(ctx_));
}

std::vector<TranscriptSegment> WhisperTranscriber::transcribe(const int16_t* pcm16k,
                                                              size_t n) {
    std::vector<TranscriptSegment> out;
    if (!ctx_ || n == 0) return out;

    // int16 → float32 [-1, 1] (whisper 입력 포맷)
    std::vector<float> f32(n);
    for (size_t i = 0; i < n; ++i) f32[i] = pcm16k[i] / 32768.0f;

    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.language = cfg_.language.c_str();
    p.n_threads = cfg_.n_threads;
    p.translate = cfg_.translate;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.print_special = false;
    p.no_context = true; // 세그먼트 간 문맥 이월 비활성(환각 억제)
    if (!cfg_.initial_prompt.empty()) p.initial_prompt = cfg_.initial_prompt.c_str();

    auto* ctx = static_cast<whisper_context*>(ctx_);
    if (whisper_full(ctx, p, f32.data(), static_cast<int>(f32.size())) != 0) {
        err_ = "whisper_full failed";
        return out;
    }

    int n_seg = whisper_full_n_segments(ctx);
    out.reserve(n_seg);
    for (int i = 0; i < n_seg; ++i) {
        TranscriptSegment s;
        // whisper t0/t1 단위는 10ms
        s.t0_ms = whisper_full_get_segment_t0(ctx, i) * 10.0;
        s.t1_ms = whisper_full_get_segment_t1(ctx, i) * 10.0;
        const char* txt = whisper_full_get_segment_text(ctx, i);
        s.text = txt ? txt : "";
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace asr
