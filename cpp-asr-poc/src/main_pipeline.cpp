// AI 추론 프로세스 엔트리포인트 (운영화 결선): UDS + TLV 프로토콜 + 링버퍼 + 하이브리드.
//
// 프로토콜(통합 TLV 프레임): [u32 len][u8 type][payload]
//   수신(IN):  HELLO(JSON 협상) / AUDIO(raw PCM16 16k) / PING / BYE
//   송신(OUT): RESULT(JSON: consent/transcript/status) / PONG
//
// 구조:
//   [메인 백엔드] ──프레임──> UDS ──> 수신 스레드(프레임 디코드)
//                                       ├ AUDIO → 링버퍼(백프레셔)
//                                       └ PING  → PONG 회신
//   처리 스레드: 링버퍼 → HybridPipeline (Vosk 실시간 동의 + whisper 세그먼트 전사)
//               → RESULT 프레임 회신
//   * 두 스레드가 같은 fd 에 쓰므로 write 는 뮤텍스로 직렬화.
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
#include <mutex>
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
    return "{\"kind\":\"consent\",\"consent\":" + std::string(e.consent ? "true" : "false") +
           ",\"text\":\"" + json_escape(e.text) + "\",\"confidence\":" +
           std::to_string(e.confidence) + ",\"start_ms\":" + std::to_string((long)e.t_start_ms) +
           ",\"end_ms\":" + std::to_string((long)e.t_end_ms) + "}";
}

std::string transcript_json(const asr::Segment& seg,
                            const std::vector<asr::TranscriptSegment>& ts) {
    std::string text;
    for (const auto& t : ts) text += t.text;
    return "{\"kind\":\"transcript\",\"seg_start_ms\":" + std::to_string((long)seg.start_ms) +
           ",\"seg_end_ms\":" + std::to_string((long)seg.end_ms) + ",\"text\":\"" +
           json_escape(text) + "\"}";
}

} // namespace

int main(int argc, char** argv) {
    std::string sock = "/tmp/asr.sock", vosk_model, whisper_model, lang = "ko";
    int ring_ms = 4000;
    int rx_timeout_ms = 6000; // 이 시간 내 프레임 없으면 무응답 클라이언트로 간주(0=비활성)
    int poll_ms = 500;        // 유휴 확인 주기

    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (k == "--socket") sock = next();
        else if (k == "--model-vosk") vosk_model = next();
        else if (k == "--model-whisper") whisper_model = next();
        else if (k == "--lang") lang = next();
        else if (k == "--ring-ms") ring_ms = std::stoi(next());
        else if (k == "--rx-timeout-ms") rx_timeout_ms = std::stoi(next());
        else if (k == "-h" || k == "--help") {
            std::fprintf(stderr, "usage: asr_pipeline --socket /tmp/asr.sock "
                                 "--model-vosk <dir> --model-whisper <ggml.bin> [--lang ko] "
                                 "[--rx-timeout-ms 6000]\n");
            return 0;
        }
    }
    if (vosk_model.empty() || whisper_model.empty()) {
        std::fprintf(stderr, "--model-vosk and --model-whisper are required\n");
        return 2;
    }

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

    // 두 스레드가 같은 fd 에 쓰므로 송신은 뮤텍스로 직렬화.
    std::mutex wmu;
    auto send_frame = [&](asr::MsgType t, const std::string& payload) {
        std::lock_guard<std::mutex> lk(wmu);
        return asr::write_frame(cfd, t, payload);
    };

    const int rate = 16000;
    asr::AudioRingBuffer ring(static_cast<size_t>(rate) * ring_ms / 1000);
    std::atomic<bool> done{false};

    // 수신 스레드: 프레임 디코드 → AUDIO는 링버퍼, PING은 PONG 회신.
    // poll 로 유휴를 감시해, rx_timeout 내 프레임이 없으면 무응답으로 보고 세션 정리.
    std::atomic<bool> timed_out{false};
    std::thread receiver([&] {
        asr::MsgType type;
        std::string payload;
        long long last_rx = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        auto now_ms = [] {
            return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        };
        while (true) {
            int pr = asr::wait_readable(cfd, poll_ms);
            if (pr < 0) break;
            if (pr == 0) { // 타임아웃: 유휴 점검
                if (rx_timeout_ms > 0 && now_ms() - last_rx > rx_timeout_ms) {
                    timed_out = true;
                    std::fprintf(stderr, "[server] client idle > %dms, closing session\n",
                                 rx_timeout_ms);
                    break;
                }
                continue;
            }
            if (!asr::read_frame(cfd, type, payload)) break;
            last_rx = now_ms();
            switch (type) {
                case asr::MsgType::Audio: {
                    size_t n = payload.size() / sizeof(int16_t);
                    if (n) ring.push(reinterpret_cast<const int16_t*>(payload.data()), n);
                    break;
                }
                case asr::MsgType::Ping:
                    send_frame(asr::MsgType::Pong, payload); // ts 에코
                    break;
                case asr::MsgType::Hello:
                    std::fprintf(stderr, "[hello] %s\n", payload.c_str());
                    break;
                case asr::MsgType::Bye:
                    done = true;
                    break;
                default:
                    break;
            }
            if (done) break;
        }
        done = true;
    });

    // 처리 스레드: 링버퍼 → HybridPipeline → RESULT 프레임
    size_t consent_emitted = 0;
    asr::VadConfig vad;
    vad.sample_rate = rate;
    asr::HybridPipeline pipe(
        vad,
        [&](const int16_t* s, size_t n) {
            det.accept(s, n);
            const auto& evs = det.events();
            for (; consent_emitted < evs.size(); ++consent_emitted)
                send_frame(asr::MsgType::Result, consent_json(evs[consent_emitted]));
        },
        [&](const asr::Segment& seg, const int16_t* p, size_t n) {
            std::vector<int16_t> chunk(p, p + n);
            auto ts = tr.transcribe(chunk.data(), chunk.size());
            send_frame(asr::MsgType::Result, transcript_json(seg, ts));
        });

    std::vector<int16_t> popbuf(rate / 5); // 200ms
    while (true) {
        size_t got = ring.pop(popbuf.data(), popbuf.size());
        if (got > 0) pipe.process(popbuf.data(), got);
        else if (done) break;
        else std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    pipe.finish();
    det.finish();
    const auto& evs = det.events();
    for (; consent_emitted < evs.size(); ++consent_emitted)
        send_frame(asr::MsgType::Result, consent_json(evs[consent_emitted]));

    auto m = ring.metrics();
    send_frame(asr::MsgType::Result,
               "{\"kind\":\"status\",\"state\":\"eos\",\"pushed\":" + std::to_string(m.pushed) +
                   ",\"dropped\":" + std::to_string(m.dropped) + ",\"max_depth\":" +
                   std::to_string(m.max_depth) + "}");
    send_frame(asr::MsgType::Bye, "{\"reason\":\"eos\"}");

    receiver.join();
    ::close(cfd);
    return 0;
}
