// AI 추론 프로세스 엔트리포인트 (운영화 결선): UDS + TLV 프로토콜 + 다중 통화 수락.
//
// 모델(Vosk/whisper)은 1회 로드 후 재사용하고, 통화(세션)마다 인식기 상태/링버퍼/
// 파이프라인만 리셋한 뒤 accept 로 돌아가 다음 클라이언트를 받는다.
//
// 프로토콜(TLV): [u32 len][u8 type][payload]
//   수신: HELLO / AUDIO(raw PCM16 16k) / PING / BYE
//   송신: RESULT(JSON consent/transcript/status) / PONG
//
// 종료: SIGINT/SIGTERM 시 listen 소켓을 shutdown 하여 accept 를 깨우고 깔끔히 종료.
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
#include <csignal>
#include <cstdio>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};
int g_server_fd = -1;

void on_signal(int) {
    g_stop = true;
    if (g_server_fd >= 0) ::shutdown(g_server_fd, SHUT_RDWR); // accept() 깨우기
}

long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

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

// 한 통화(세션) 처리. 클라이언트 종료/타임아웃/종료신호 시 반환.
void run_session(int cfd, asr::VoskConsentDetector& det, asr::WhisperTranscriber& tr,
                 int rate, int ring_ms, int rx_timeout_ms, int poll_ms) {
    std::mutex wmu;
    auto send_frame = [&](asr::MsgType t, const std::string& payload) {
        std::lock_guard<std::mutex> lk(wmu);
        return asr::write_frame(cfd, t, payload);
    };

    asr::AudioRingBuffer ring(static_cast<size_t>(rate) * ring_ms / 1000);
    std::atomic<bool> done{false};

    std::thread receiver([&] {
        asr::MsgType type;
        std::string payload;
        long long last_rx = now_ms();
        while (!g_stop.load()) {
            int pr = asr::wait_readable(cfd, poll_ms);
            if (pr < 0) break;
            if (pr == 0) { // 유휴 점검
                if (rx_timeout_ms > 0 && now_ms() - last_rx > rx_timeout_ms) {
                    std::fprintf(stderr, "[server] client idle > %dms, closing session\n",
                                 rx_timeout_ms);
                    break;
                }
                continue;
            }
            if (!asr::read_frame(cfd, type, payload)) break; // EOF/에러
            last_rx = now_ms();
            if (type == asr::MsgType::Audio) {
                size_t n = payload.size() / sizeof(int16_t);
                if (n) ring.push(reinterpret_cast<const int16_t*>(payload.data()), n);
            } else if (type == asr::MsgType::Ping) {
                send_frame(asr::MsgType::Pong, payload);
            } else if (type == asr::MsgType::Hello) {
                std::fprintf(stderr, "[hello] %s\n", payload.c_str());
            } else if (type == asr::MsgType::Bye) {
                break;
            }
        }
        done = true;
    });

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
    while (!g_stop.load()) {
        size_t got = ring.pop(popbuf.data(), popbuf.size());
        if (got > 0) pipe.process(popbuf.data(), got);
        else if (done.load()) break;
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

    // 수신 스레드 정리(블로킹 깨우기)
    done = true;
    ::shutdown(cfd, SHUT_RDWR);
    receiver.join();
}

} // namespace

int main(int argc, char** argv) {
    std::string sock = "/tmp/asr.sock", vosk_model, whisper_model, lang = "ko";
    int ring_ms = 4000, rx_timeout_ms = 6000, poll_ms = 500;

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

    // 모델 1회 로드(통화 간 재사용)
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
    if (!srv.listen(sock, 4)) { std::fprintf(stderr, "uds: %s\n", srv.error().c_str()); return 1; }
    g_server_fd = srv.fd();

    struct sigaction sa {};
    sa.sa_handler = on_signal; // SA_RESTART 미설정 → accept 가 깨어남
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    std::fprintf(stderr, "listening on %s (multi-call). Ctrl-C to stop.\n", sock.c_str());

    const int rate = 16000;
    int calls = 0;
    while (!g_stop.load()) {
        int cfd = srv.accept();
        if (cfd < 0) break; // 종료 신호(shutdown) 또는 에러
        std::fprintf(stderr, "[server] call #%d accepted\n", ++calls);
        det.reset(); // 통화별 인식기 상태 격리
        run_session(cfd, det, tr, rate, ring_ms, rx_timeout_ms, poll_ms);
        ::close(cfd);
        std::fprintf(stderr, "[server] call #%d ended\n", calls);
    }
    std::fprintf(stderr, "[server] shutting down after %d call(s)\n", calls);
    return 0;
}
