// G.711 (PCMU / μ-law, PCMA / a-law) → linear PCM16 디코드.
// 전화망 통화 오디오의 1차 디코드 단계 (PoC-A).
#pragma once
#include <cstdint>

namespace asr {

// μ-law(PCMU) 1바이트 → int16 선형 PCM
int16_t ulaw_to_pcm16(uint8_t u);

// a-law(PCMA) 1바이트 → int16 선형 PCM
int16_t alaw_to_pcm16(uint8_t a);

// int16 선형 PCM → μ-law(PCMU) 1바이트 (테스트 입력/변환 도구용)
uint8_t pcm16_to_ulaw(int16_t pcm);

} // namespace asr
