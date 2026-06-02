// PoC-B CLI: 통화 오디오에서 동의("네/오케이") 감지.
//
// 파이프라인: 디코드(WAV/PCMU/PCMA/PCM16) → 8k면 16k로 anti-alias 업샘플
//            → Vosk 그래머 제약 인식기 → 동의 이벤트(JSON) 출력.
//
// 사용:
//   asr_consent --model <vosk-ko-dir> --input call.pcmu --format pcmu --rate 8000
//   asr_consent --model <vosk-ko-dir> --input call.wav
#include "asr_vosk.hpp"
#include "g711.hpp"
#include "resample.hpp"
#include "wav_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool ends_with(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

bool decode_raw(const std::string& path, const std::string& fmt,
                std::vector<int16_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "open failed: " + path; return false; }
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    if (fmt == "pcm16") {
        out.resize(raw.size() / 2);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<int16_t>(raw[i * 2] | (raw[i * 2 + 1] << 8));
    } else if (fmt == "pcmu") {
        out.resize(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) out[i] = asr::ulaw_to_pcm16(raw[i]);
    } else if (fmt == "pcma") {
        out.resize(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) out[i] = asr::alaw_to_pcm16(raw[i]);
    } else {
        err = "raw input requires --format pcm16|pcmu|pcma"; return false;
    }
    return true;
}

void usage() {
    std::fprintf(stderr,
        "usage: asr_consent --model <vosk-ko-dir> --input <file>\n"
        "                   [--format auto|pcm16|pcmu|pcma] [--rate N]\n"
        "                   [--min-conf X] [--chunk-ms N]\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string model, input, format = "auto";
    int rate = 0, chunk_ms = 100;
    double min_conf = 0.6;

    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (k == "--model") model = next();
        else if (k == "--input") input = next();
        else if (k == "--format") format = next();
        else if (k == "--rate") rate = std::stoi(next());
        else if (k == "--min-conf") min_conf = std::stod(next());
        else if (k == "--chunk-ms") chunk_ms = std::stoi(next());
        else if (k == "-h" || k == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", k.c_str()); usage(); return 2; }
    }
    if (model.empty() || input.empty()) { usage(); return 2; }

    // 1) 디코드
    std::vector<int16_t> samples;
    std::string err;
    bool is_wav = (format == "auto" && ends_with(input, ".wav")) || format == "wav";
    if (is_wav) {
        asr::WavData w;
        if (!asr::read_wav_pcm16(input, w, err)) {
            std::fprintf(stderr, "wav read error: %s\n", err.c_str()); return 1;
        }
        samples = std::move(w.samples);
        rate = w.sample_rate;
    } else {
        std::string fmt = (format == "auto") ? "pcm16" : format;
        if (rate <= 0) { std::fprintf(stderr, "raw input requires --rate\n"); return 1; }
        if (!decode_raw(input, fmt, samples, err)) {
            std::fprintf(stderr, "decode error: %s\n", err.c_str()); return 1;
        }
    }

    // 2) 16kHz 정규화(anti-alias 업샘플)
    std::vector<int16_t> pcm16k = asr::resample_to_16k(samples, rate);

    // 3) Vosk 그래머 제약 인식기
    asr::ConsentConfig cfg;
    cfg.model_path = model;
    cfg.min_confidence = min_conf;
    asr::VoskConsentDetector det(cfg);
    if (!det.ok()) {
        std::fprintf(stderr, "detector init failed: %s\n", det.error().c_str());
        return 1;
    }

    // 4) 청크 단위 스트리밍 입력(실시간 모사)
    size_t chunk = static_cast<size_t>(16000.0 * chunk_ms / 1000.0);
    if (chunk == 0) chunk = 1600;
    for (size_t off = 0; off < pcm16k.size(); off += chunk) {
        size_t n = std::min(chunk, pcm16k.size() - off);
        det.accept(pcm16k.data() + off, n);
    }
    det.finish();

    // 5) 출력
    bool any = false;
    int idx = 0;
    for (const auto& ev : det.events()) {
        if (ev.consent) any = true;
        std::printf("{\"event\":%d,\"consent\":%s,\"text\":\"%s\","
                    "\"confidence\":%.3f,\"start_ms\":%.0f,\"end_ms\":%.0f}\n",
                    idx++, ev.consent ? "true" : "false", ev.text.c_str(),
                    ev.confidence, ev.t_start_ms, ev.t_end_ms);
    }
    std::printf("{\"consent_detected\":%s,\"events\":%d}\n",
                any ? "true" : "false", idx);
    return 0;
}
