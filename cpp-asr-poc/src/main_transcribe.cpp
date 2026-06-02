// PoC-C CLI: whisper.cpp 보조 전사 (통화 로그/감사용).
//
// 파이프라인: 디코드(WAV/PCMU/PCMA/PCM16) → 8k면 16k anti-alias 업샘플
//            → whisper.cpp 전사 → 세그먼트 텍스트(JSON) 출력.
//
// 사용:
//   asr_transcribe --model ggml-small.bin --input call.pcmu --format pcmu --rate 8000
//   asr_transcribe --model ggml-base.bin  --input call.wav --lang ko
#include "asr_whisper.hpp"
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
    } else { err = "raw input requires --format pcm16|pcmu|pcma"; return false; }
    return true;
}
void usage() {
    std::fprintf(stderr,
        "usage: asr_transcribe --model <ggml.bin> --input <file>\n"
        "                      [--format auto|pcm16|pcmu|pcma] [--rate N]\n"
        "                      [--lang ko] [--threads N] [--prompt \"...\"]\n");
}
} // namespace

int main(int argc, char** argv) {
    std::string model, input, format = "auto", lang = "ko", prompt;
    int rate = 0, threads = 4;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (k == "--model") model = next();
        else if (k == "--input") input = next();
        else if (k == "--format") format = next();
        else if (k == "--rate") rate = std::stoi(next());
        else if (k == "--lang") lang = next();
        else if (k == "--threads") threads = std::stoi(next());
        else if (k == "--prompt") prompt = next();
        else if (k == "-h" || k == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", k.c_str()); usage(); return 2; }
    }
    if (model.empty() || input.empty()) { usage(); return 2; }

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

    std::vector<int16_t> pcm16k = asr::resample_to_16k(samples, rate);

    asr::TranscribeConfig cfg;
    cfg.model_path = model;
    cfg.language = lang;
    cfg.n_threads = threads;
    cfg.initial_prompt = prompt;
    asr::WhisperTranscriber tr(cfg);
    if (!tr.ok()) {
        std::fprintf(stderr, "whisper init failed: %s\n", tr.error().c_str());
        return 1;
    }

    auto segs = tr.transcribe(pcm16k.data(), pcm16k.size());
    int idx = 0;
    std::string full;
    for (const auto& s : segs) {
        std::printf("{\"seg\":%d,\"t0_ms\":%.0f,\"t1_ms\":%.0f,\"text\":\"%s\"}\n",
                    idx++, s.t0_ms, s.t1_ms, s.text.c_str());
        full += s.text;
    }
    std::printf("{\"segments\":%d,\"chars\":%zu}\n", idx, full.size());
    return 0;
}
