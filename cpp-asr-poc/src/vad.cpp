#include "vad.hpp"
#include <algorithm>
#include <cmath>

namespace asr {

Segmenter::Segmenter(const VadConfig& cfg) : cfg_(cfg) {
    frame_len_ = static_cast<size_t>(cfg_.sample_rate) * cfg_.frame_ms / 1000;
    if (frame_len_ == 0) frame_len_ = 1;
    frame_dur_ms_ = 1000.0 * static_cast<double>(frame_len_) / cfg_.sample_rate;
    min_speech_frames_ = std::max(1, cfg_.min_speech_ms / cfg_.frame_ms);
    min_silence_frames_ = std::max(1, cfg_.min_silence_ms / cfg_.frame_ms);
    noise_db_ = cfg_.noise_floor_init_db;
    carry_.reserve(frame_len_);
}

static double frame_energy_db(const int16_t* f, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double s = static_cast<double>(f[i]) / 32768.0;
        acc += s * s;
    }
    double rms2 = acc / static_cast<double>(n);
    return 10.0 * std::log10(rms2 + 1e-12); // dBFS
}

void Segmenter::process(const int16_t* samples, size_t n) {
    size_t pos = 0;
    // 이전 잔여 + 새 입력을 프레임 경계로 잘라 처리
    if (!carry_.empty()) {
        while (pos < n && carry_.size() < frame_len_) carry_.push_back(samples[pos++]);
        if (carry_.size() == frame_len_) {
            push_frame(carry_.data());
            carry_.clear();
        }
    }
    while (pos + frame_len_ <= n) {
        push_frame(samples + pos);
        pos += frame_len_;
    }
    while (pos < n) carry_.push_back(samples[pos++]);
}

void Segmenter::push_frame(const int16_t* frame) {
    double e_db = frame_energy_db(frame, frame_len_);
    double now_ms = static_cast<double>(frame_index_) * frame_dur_ms_;
    frame_index_++;

    double start_th = noise_db_ + cfg_.start_threshold_db;
    double end_th = noise_db_ + cfg_.end_threshold_db;

    bool is_speech = in_speech_ ? (e_db > end_th) : (e_db > start_th);

    if (is_speech) {
        speech_run_++;
        silence_run_ = 0;
        if (!in_speech_ && speech_run_ >= min_speech_frames_) {
            in_speech_ = true;
            // 진입 시점에서 확정 지연(min_speech)과 패딩만큼 앞으로 당김
            double back = (min_speech_frames_ - 1) * frame_dur_ms_ + cfg_.speech_pad_ms;
            seg_start_ms_ = std::max(0.0, now_ms - back);
        }
    } else {
        // 비발화 프레임에서만 노이즈 플로어를 천천히 추적
        noise_db_ += (e_db - noise_db_) * cfg_.noise_adapt;
        silence_run_++;
        speech_run_ = 0;
        if (in_speech_ && silence_run_ >= min_silence_frames_) {
            // 종료 시점은 묵음이 시작된 지점 + 뒤 패딩
            double seg_end = now_ms - (min_silence_frames_ - 1) * frame_dur_ms_
                             + cfg_.speech_pad_ms;
            close_segment(seg_end);
        }
    }
}

void Segmenter::close_segment(double end_ms) {
    in_speech_ = false;
    silence_run_ = 0;
    speech_run_ = 0;
    if (end_ms > seg_start_ms_) {
        segments_.push_back(Segment{seg_start_ms_, end_ms});
    }
}

void Segmenter::finish() {
    // 잔여 샘플을 0패딩해 마지막 프레임으로 소비
    if (!carry_.empty()) {
        while (carry_.size() < frame_len_) carry_.push_back(0);
        push_frame(carry_.data());
        carry_.clear();
    }
    if (in_speech_) {
        double now_ms = static_cast<double>(frame_index_) * frame_dur_ms_;
        close_segment(now_ms + cfg_.speech_pad_ms);
    }
}

} // namespace asr
