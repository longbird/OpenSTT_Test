// PoC-A CLI: 오디오 파일/raw 스트림에서 발화 세그먼트를 추출해 출력.
//
// 사용:
//   asr_segmenter --input call.wav
//   asr_segmenter --input call.raw --format pcmu --rate 8000
//   asr_segmenter --input call.raw --format pcm16 --rate 8000 --frame-ms 20
//
// 출력: 세그먼트별 JSON 라인 + 요약. 엔진(STT/KWS) 없이 파이프라인 전단만 검증.
#include "g711.hpp"
#include "vad.hpp"
#include "wav_io.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string input;
    std::string format = "auto"; // auto|pcm16|pcmu|pcma
    int rate = 0;                // raw 입력 시 필수
    asr::VadConfig vad;
};

void usage() {
    std::fprintf(stderr,
        "usage: asr_segmenter --input <file> [--format auto|pcm16|pcmu|pcma]\n"
        "                     [--rate N] [--frame-ms N] [--start-db X] [--end-db X]\n"
        "                     [--min-speech-ms N] [--min-silence-ms N] [--pad-ms N]\n");
}

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", name); return ""; }
            return argv[++i];
        };
        if (k == "--input") a.input = next("--input");
        else if (k == "--format") a.format = next("--format");
        else if (k == "--rate") a.rate = std::stoi(next("--rate"));
        else if (k == "--frame-ms") a.vad.frame_ms = std::stoi(next("--frame-ms"));
        else if (k == "--start-db") a.vad.start_threshold_db = std::stod(next("--start-db"));
        else if (k == "--end-db") a.vad.end_threshold_db = std::stod(next("--end-db"));
        else if (k == "--min-speech-ms") a.vad.min_speech_ms = std::stoi(next("--min-speech-ms"));
        else if (k == "--min-silence-ms") a.vad.min_silence_ms = std::stoi(next("--min-silence-ms"));
        else if (k == "--pad-ms") a.vad.speech_pad_ms = std::stoi(next("--pad-ms"));
        else if (k == "-h" || k == "--help") { usage(); return false; }
        else { std::fprintf(stderr, "unknown arg: %s\n", k.c_str()); usage(); return false; }
    }
    if (a.input.empty()) { usage(); return false; }
    return true;
}

bool ends_with(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

// raw 바이트를 지정 포맷에 따라 PCM16 mono로 디코드.
bool decode_raw(const std::string& path, const std::string& fmt,
                std::vector<int16_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "open failed: " + path; return false; }
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    if (fmt == "pcm16") {
        size_t n = raw.size() / 2;
        out.resize(n);
        for (size_t i = 0; i < n; ++i)
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

} // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) return 2;

    std::vector<int16_t> samples;
    int rate = a.rate;
    std::string err;

    bool is_wav = (a.format == "auto" && ends_with(a.input, ".wav")) || a.format == "wav";
    if (is_wav) {
        asr::WavData w;
        if (!asr::read_wav_pcm16(a.input, w, err)) {
            std::fprintf(stderr, "wav read error: %s\n", err.c_str());
            return 1;
        }
        samples = std::move(w.samples);
        rate = w.sample_rate;
    } else {
        std::string fmt = (a.format == "auto") ? "pcm16" : a.format;
        if (rate <= 0) {
            std::fprintf(stderr, "raw input requires --rate\n");
            return 1;
        }
        if (!decode_raw(a.input, fmt, samples, err)) {
            std::fprintf(stderr, "decode error: %s\n", err.c_str());
            return 1;
        }
    }

    a.vad.sample_rate = rate;
    asr::Segmenter seg(a.vad);
    seg.process(samples.data(), samples.size());
    seg.finish();

    double total_ms = 1000.0 * static_cast<double>(samples.size()) / rate;
    double speech_ms = 0.0;
    std::printf("{\"input\":\"%s\",\"sample_rate\":%d,\"duration_ms\":%.1f,"
                "\"frame_ms\":%d,\"noise_floor_db\":%.1f}\n",
                a.input.c_str(), rate, total_ms, a.vad.frame_ms, seg.noise_floor_db());
    int idx = 0;
    for (const auto& s : seg.segments()) {
        // 끝 패딩이 스트림 길이를 넘지 않도록 클램프
        double end_ms = std::min(s.end_ms, total_ms);
        double dur = end_ms - s.start_ms;
        speech_ms += dur;
        std::printf("{\"seg\":%d,\"start_ms\":%.1f,\"end_ms\":%.1f,\"dur_ms\":%.1f}\n",
                    idx++, s.start_ms, end_ms, dur);
    }
    std::printf("{\"segments\":%d,\"speech_ms\":%.1f,\"speech_ratio\":%.3f}\n",
                static_cast<int>(seg.segments().size()), speech_ms,
                total_ms > 0 ? speech_ms / total_ms : 0.0);
    return 0;
}
