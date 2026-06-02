// AI 추론 프로세스 엔트리포인트 (운영화 결선): UDS + 링버퍼 + 하이브리드.
//
// 구조(설계 문서 그대로):
//   [메인 백엔드] ──(16k PCM16 raw 스트림)──> UDS ──> [수신 스레드] ──> 링버퍼(백프레셔)
//                                                       │
//                                          [처리 스레드] HybridPipeline
//                                            ├─ Vosk     : 실시간 동의 감지 → 프레임 결과
//                                            └─ whisper  : 세그먼트 전사    → 프레임 결과
//   결과는 [u32 len][JSON] 프레임으로 동일 소켓에 회신.
//
// 입력은 16kHz PCM16 mono 를 전제(8k 통화는 업스트림/PoC-B·C 경로에서 리샘플).
//
// 빌드: libvosk + whisper.cpp 가 모두 있을 때만 생성됨.
#include "asr_vosk.hpp"
#include "asr_whisper.hpp"
#include "frame_io.hpp"
#include "hybrid.hpp"
#include "ring_buffer.hpp"
#include "uds_server.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
        else if (c == '\n') o += "\\n";
        else o.push_back(c);
    }
    return o;
}

std::string consent_json(const asr::ConsentEvent& e) {
    return "{\"type\":\"consent\",\"consent\":" + std::string(e.consent ? "true" : "false") +
           ",\"text\":\"" + json_escape(e.text) + "\",\"confidence\":" +
           std::to_string(e.confidence) + ",\"start_ms\":" + std::to_string((long)e.t_start_ms) +
           ",\"end_ms\":" + std::to_string((long)e.t_end_ms) + "}";
}

std::string transcript_json(const asr::Segment& seg,
                            const std::vector<asr::TranscriptSegment>& ts) {
    std::string text;
    for (const auto& t : ts) text += t.text;
    return "{\"type\":\"transcript\",\"seg_start_ms\":" + std::to_string((long)seg.start_ms) +
           ",\"seg_end_ms\":" + std::to_string((long)seg.end_ms) + ",\"text\":\"" +
           json_escape(text) + "\"}";
}

} // namespace

int main(int argc, char** argv) {
    std::string sock = "/tmp/asr.sock", vosk_model, whisper_model, lang = "ko";
    int ring_ms = 4000; // 링버퍼 용량(ms) @16k

    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (k == "--socket") sock = next();
        else if (k == "--model-vosk") vosk_model = next();
        else if (k == "--model-whisper") whisper_model = next();
        else if (k == "--lang") lang = next();
        else if (k == "--ring-ms") ring_ms = std::stoi(next());
        else if (k == "-h" || k == "--help") {
            std::fprintf(stderr, "usage: asr_pipeline --socket /tmp/asr.sock "
                                 "--model-vosk <dir> --model-whisper <ggml.bin> [--lang ko]\n");
            return 0;
        }
    }
    if (vosk_model.empty() || whisper_model.empty()) {
        std::fprintf(stderr, "--model-vosk and --model-whisper are required\n");
        return 2;
    }

    // 엔진 초기화
    asr::ConsentConfig ccfg;
    ccfg.model_path = vosk_model;
    asr::VoskConsentDetector det(ccfg);
    if (!det.ok()) { std::fprintf(stderr, "vosk init: %s\n", det.error().c_str()); return 1; }

    asr::TranscribeConfig tcfg;
    tcfg.model_path = whisper_model;
    tcfg.language = lang;
    asr::WhisperTranscriber tr(tcfg);
    if (!tr.ok()) { std::fprintf(stderr, "whisper init: %s\n", tr.error().c_str()); return 1; }

    asr::UdsServer srv;
    if (!srv.listen(sock)) { std::fprintf(stderr, "uds: %s\n", srv.error().c_str()); return 1; }
    std::fprintf(stderr, "listening on %s\n", sock.c_str());

    int cfd = srv.accept();
    if (cfd < 0) { std::fprintf(stderr, "accept: %s\n", srv.error().c_str()); return 1; }

    const int rate = 16000;
    asr::AudioRingBuffer ring(static_cast<size_t>(rate) * ring_ms / 1000);
    std::atomic<bool> done{false};

    // 수신 스레드: raw PCM16 → 링버퍼(백프레셔)
    std::thread receiver([&] {
        std::vector<int16_t> buf(rate / 10); // 100ms
        while (true) {
            ssize_t r = ::read(cfd, buf.data(), buf.size() * sizeof(int16_t));
            if (r <= 0) break;
            ring.push(buf.data(), static_cast<size_t>(r) / sizeof(int16_t));
        }
        done = true;
    });

    // 처리 스레드: 링버퍼 → HybridPipeline → 프레임 결과 송신
    size_t consent_emitted = 0;
    asr::VadConfig vad;
    vad.sample_rate = rate;
    asr::HybridPipeline pipe(
        vad,
        [&](const int16_t* s, size_t n) {
            det.accept(s, n);
            const auto& evs = det.events();
            for (; consent_emitted < evs.size(); ++consent_emitted)
                asr::write_frame(cfd, consent_json(evs[consent_emitted]));
        },
        [&](const asr::Segment& seg, const int16_t* p, size_t n) {
            std::vector<int16_t> chunk(p, p + n);
            auto ts = tr.transcribe(chunk.data(), chunk.size());
            asr::write_frame(cfd, transcript_json(seg, ts));
        });

    std::vector<int16_t> popbuf(rate / 5); // 200ms
    while (true) {
        size_t got = ring.pop(popbuf.data(), popbuf.size());
        if (got > 0) {
            pipe.process(popbuf.data(), got);
        } else if (done) {
            break;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    pipe.finish();
    det.finish();
    const auto& evs = det.events();
    for (; consent_emitted < evs.size(); ++consent_emitted)
        asr::write_frame(cfd, consent_json(evs[consent_emitted]));

    auto m = ring.metrics();
    asr::write_frame(cfd, "{\"type\":\"status\",\"state\":\"eos\",\"pushed\":" +
                              std::to_string(m.pushed) + ",\"dropped\":" +
                              std::to_string(m.dropped) + ",\"max_depth\":" +
                              std::to_string(m.max_depth) + "}");
    receiver.join();
    ::close(cfd);
    return 0;
}
