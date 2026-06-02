#include "hybrid.hpp"
#include <algorithm>
#include <cmath>

namespace asr {

HybridPipeline::HybridPipeline(const VadConfig& vad, ConsentFeed consent,
                               SegmentSink on_segment)
    : seg_(vad), consent_(std::move(consent)), on_segment_(std::move(on_segment)),
      sample_rate_(vad.sample_rate) {}

size_t HybridPipeline::ms_to_idx(double ms) const {
    if (ms < 0) ms = 0;
    long idx = std::lround(ms / 1000.0 * sample_rate_);
    if (idx < 0) idx = 0;
    if (static_cast<size_t>(idx) > all_.size()) idx = static_cast<long>(all_.size());
    return static_cast<size_t>(idx);
}

void HybridPipeline::process(const int16_t* samples, size_t n) {
    // 1) 실시간 동의 경로: 즉시 공급
    if (consent_) consent_(samples, n);

    // 2) 세그먼트 누적 + VAD
    all_.insert(all_.end(), samples, samples + n);
    seg_.process(samples, n);
    flush_new_segments();
}

void HybridPipeline::finish() {
    seg_.finish();
    flush_new_segments();
}

void HybridPipeline::flush_new_segments() {
    const auto& segs = seg_.segments();
    for (size_t i = emitted_; i < segs.size(); ++i) {
        size_t s0 = ms_to_idx(segs[i].start_ms);
        size_t s1 = ms_to_idx(segs[i].end_ms);
        if (s1 < s0) s1 = s0;
        if (on_segment_) on_segment_(segs[i], all_.data() + s0, s1 - s0);
    }
    emitted_ = segs.size();
}

} // namespace asr
