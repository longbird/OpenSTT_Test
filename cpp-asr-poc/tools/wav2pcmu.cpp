// WAV(PCM16, 8kHz mono) → PCMU(G.711 μ-law) raw 변환 도구.
//
// asr_feed 입력용 PCMU 파일을 만든다. 입력은 8kHz mono PCM16 WAV 여야 한다
// (전화망과 동일 대역). 다른 포맷이면 ffmpeg 로 먼저 변환:
//   ffmpeg -i in.any -ar 8000 -ac 1 -f mulaw out.pcmu   ← 이 도구 없이도 PCMU 생성 가능
//
// 사용: wav2pcmu in_8k_mono.wav out.pcmu
#include "g711.hpp"
#include "wav_io.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: wav2pcmu <in_8k_mono.wav> <out.pcmu>\n");
        return 2;
    }
    asr::WavData w;
    std::string err;
    if (!asr::read_wav_pcm16(argv[1], w, err)) {
        std::fprintf(stderr, "wav read error: %s\n", err.c_str());
        return 1;
    }
    if (w.sample_rate != 8000) {
        std::fprintf(stderr,
            "error: input is %d Hz; need 8000 Hz mono.\n"
            "  convert first, e.g.: ffmpeg -i %s -ar 8000 -ac 1 out8k.wav\n",
            w.sample_rate, argv[1]);
        return 1;
    }
    std::vector<uint8_t> ulaw(w.samples.size());
    for (size_t i = 0; i < w.samples.size(); ++i)
        ulaw[i] = asr::pcm16_to_ulaw(w.samples[i]);

    std::ofstream f(argv[2], std::ios::binary);
    if (!f) { std::fprintf(stderr, "open for write failed: %s\n", argv[2]); return 1; }
    f.write(reinterpret_cast<const char*>(ulaw.data()), ulaw.size());
    std::printf("wrote %s (%zu PCMU bytes, %.2f s @8kHz)\n",
                argv[2], ulaw.size(), static_cast<double>(ulaw.size()) / 8000.0);
    return 0;
}
