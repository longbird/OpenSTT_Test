// 송신측 데모 CLI: PCMU 파일을 실시간 페이스로 UDS(asr_pipeline)에 흘려보낸다.
//
// 메인 백엔드가 전화망에서 받은 PCMU 를 AI 추론 프로세스로 밀어넣는 흐름을 모사.
// 결과 프레임(consent/transcript/status)은 수신되는 대로 출력.
//
// 사용:
//   asr_feed --socket /tmp/asr.sock --input call.pcmu [--packet-ms 20] [--realtime]
//   (입력은 8kHz μ-law raw. 20ms=160바이트 패킷 단위로 공급)
#include "pcmu_sender.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    std::string sock = "/tmp/asr.sock", input;
    int packet_ms = 20;
    bool realtime = false;

    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (k == "--socket") sock = next();
        else if (k == "--input") input = next();
        else if (k == "--packet-ms") packet_ms = std::stoi(next());
        else if (k == "--realtime") realtime = true;
        else if (k == "-h" || k == "--help") {
            std::fprintf(stderr, "usage: asr_feed --socket <path> --input <pcmu> "
                                 "[--packet-ms 20] [--realtime]\n");
            return 0;
        }
    }
    if (input.empty()) { std::fprintf(stderr, "--input <pcmu> required\n"); return 2; }

    std::ifstream f(input, std::ios::binary);
    if (!f) { std::fprintf(stderr, "open failed: %s\n", input.c_str()); return 1; }
    std::vector<uint8_t> ulaw((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    std::atomic<int> results{0};
    asr::PcmuSenderConfig cfg;
    cfg.socket_path = sock;
    asr::PcmuStreamSender sender(cfg, [&](const std::string& json) {
        std::printf("%s\n", json.c_str());
        std::fflush(stdout);
        results++;
    });

    if (!sender.start()) {
        std::fprintf(stderr, "sender start failed: %s\n", sender.error().c_str());
        return 1;
    }

    const size_t pkt = static_cast<size_t>(8000 * packet_ms / 1000); // 8k μ-law bytes/packet
    for (size_t off = 0; off < ulaw.size() && sender.running(); off += pkt) {
        size_t n = std::min(pkt, ulaw.size() - off);
        sender.feed_pcmu(ulaw.data() + off, n);
        if (realtime) std::this_thread::sleep_for(std::chrono::milliseconds(packet_ms));
    }

    // 잔여 오디오가 추론될 시간을 잠시 준 뒤 종료
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    sender.stop();

    auto m = sender.ring_metrics();
    std::fprintf(stderr,
        "[feed] sent_pcmu_bytes=%zu results=%d ring{pushed=%llu dropped=%llu max_depth=%zu}\n",
        ulaw.size(), results.load(),
        (unsigned long long)m.pushed, (unsigned long long)m.dropped, m.max_depth);
    return 0;
}
