#include "g711.hpp"

namespace asr {

// ITU-T G.711 표준 μ-law 디코드.
int16_t ulaw_to_pcm16(uint8_t u) {
    static const int BIAS = 0x84;
    u = static_cast<uint8_t>(~u);
    int t = ((u & 0x0f) << 3) + BIAS;
    t <<= (u & 0x70) >> 4;
    return static_cast<int16_t>((u & 0x80) ? (BIAS - t) : (t - BIAS));
}

// ITU-T G.711 표준 μ-law 인코드.
uint8_t pcm16_to_ulaw(int16_t pcm) {
    const int CLIP = 32635, BIAS = 0x84;
    int s = pcm;
    int sign = (s < 0) ? 0x80 : 0;
    if (s < 0) s = -s;
    if (s > CLIP) s = CLIP;
    s += BIAS;
    int exponent = 7, mask = 0x4000;
    while (exponent > 0 && !(s & mask)) { mask >>= 1; exponent--; }
    int mantissa = (s >> (exponent + 3)) & 0x0f;
    return static_cast<uint8_t>(~(sign | (exponent << 4) | mantissa));
}

// ITU-T G.711 표준 a-law 디코드.
int16_t alaw_to_pcm16(uint8_t a) {
    a ^= 0x55;
    int t = (a & 0x0f) << 4;
    int seg = (a & 0x70) >> 4;
    switch (seg) {
        case 0:  t += 8;                  break;
        case 1:  t += 0x108;              break;
        default: t += 0x108; t <<= seg - 1; break;
    }
    return static_cast<int16_t>((a & 0x80) ? t : -t);
}

} // namespace asr
