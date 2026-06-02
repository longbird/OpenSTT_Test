// 최소 WAV(RIFF) 입출력 — PCM16 mono 전용. PoC 실험/검증용.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace asr {

struct WavData {
    int sample_rate = 0;
    int channels = 0;
    std::vector<int16_t> samples; // 인터리브 제거된 mono로 강제(다채널이면 0번 채널)
};

// PCM16 WAV 읽기. 성공 시 true. 실패 시 err에 사유 기록.
bool read_wav_pcm16(const std::string& path, WavData& out, std::string& err);

// PCM16 mono WAV 쓰기(테스트 오디오 생성용).
bool write_wav_pcm16_mono(const std::string& path, int sample_rate,
                          const std::vector<int16_t>& samples, std::string& err);

} // namespace asr
