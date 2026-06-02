// 에너지 기반 적응형 VAD + 발화 세그먼트 추출 (PoC-A).
//
// 목적: PCM16 mono 스트림을 프레임 단위로 받아 "발화 구간 [start_ms, end_ms]"을
//       추출한다. 외부 라이브러리 없이 자립 동작하며, 추후 WebRTC/Silero VAD로
//       교체 가능하도록 인터페이스를 단순하게 유지한다.
//
// 알고리즘 요지:
//  - 프레임 에너지(dBFS) 계산
//  - 적응형 노이즈 플로어(EMA, 비발화 구간에서만 갱신)
//  - 히스테리시스 임계값(start/end) + 최소 발화/최소 묵음 길이로 채터링 방지
//  - 세그먼트 앞뒤 패딩(speech_pad_ms)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace asr {

struct VadConfig {
    int sample_rate = 16000;
    int frame_ms = 20;            // WebRTC 호환 위해 10/20/30 권장
    double start_threshold_db = 9.0; // 노이즈 플로어 대비 진입 임계(dB)
    double end_threshold_db = 6.0;   // 이탈 임계(dB), start보다 낮게(히스테리시스)
    int min_speech_ms = 150;      // 발화 확정에 필요한 연속 음성 길이
    int min_silence_ms = 300;     // 세그먼트 종료에 필요한 연속 묵음 길이
    int speech_pad_ms = 100;      // 세그먼트 앞뒤 패딩
    double noise_floor_init_db = -55.0;
    double noise_adapt = 0.05;    // 노이즈 플로어 EMA 계수(0~1)
};

struct Segment {
    double start_ms = 0.0;
    double end_ms = 0.0;
    double duration_ms() const { return end_ms - start_ms; }
};

class Segmenter {
public:
    explicit Segmenter(const VadConfig& cfg);

    // mono int16 샘플 스트림을 누적 입력. 여러 번 나눠 호출 가능.
    void process(const int16_t* samples, size_t n);

    // 스트림 종료 시 호출(미종료 발화를 마감). 이후 segments()가 최종 결과.
    void finish();

    const std::vector<Segment>& segments() const { return segments_; }

    // 마지막으로 관측한 노이즈 플로어(dBFS) — 디버그/관측용.
    double noise_floor_db() const { return noise_db_; }

private:
    void push_frame(const int16_t* frame); // frame_len_ 길이 1프레임 처리
    void close_segment(double end_ms);

    VadConfig cfg_;
    size_t frame_len_ = 0;        // 프레임당 샘플 수
    double frame_dur_ms_ = 0.0;
    int min_speech_frames_ = 0;
    int min_silence_frames_ = 0;

    std::vector<int16_t> carry_;  // 프레임 경계 잔여 샘플
    size_t frame_index_ = 0;      // 처리한 프레임 수(시간축)

    bool in_speech_ = false;
    int speech_run_ = 0;          // 연속 음성 프레임 카운트(진입 판정)
    int silence_run_ = 0;         // 연속 묵음 프레임 카운트(종료 판정)
    double seg_start_ms_ = 0.0;
    double noise_db_ = -55.0;

    std::vector<Segment> segments_;
};

} // namespace asr
