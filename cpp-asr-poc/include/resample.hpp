// 8kHz(통화) → 16kHz 업샘플 (Vosk 한국어 모델 입력용).
//
// 단순 zero-stuffing이 아니라 windowed-sinc FIR 저역통과로 anti-alias 처리한다.
// 설계 문서의 "업샘플은 polyphase/anti-alias로" 권고를 반영.
#pragma once
#include <cstdint>
#include <vector>

namespace asr {

// in_rate → 16000 으로 변환한 PCM16 mono 반환.
//  - 16000: 그대로 복사
//  - 8000 : 2배 업샘플 + 4kHz 저역통과 FIR
//  - 그 외: 선형보간 폴백(PoC 한정)
std::vector<int16_t> resample_to_16k(const std::vector<int16_t>& in, int in_rate);

} // namespace asr
