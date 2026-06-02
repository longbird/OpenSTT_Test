// 송신 메커니즘 검증:
//  1) 스트리밍 리샘플러 연속성(청크 스트림 ≈ 블록 처리, 워밍업 제외)
//  2) PcmuStreamSender → (페이크 서버) end-to-end 바이트 경로 + 결과 프레임 수신
#include "frame_io.hpp"
#include "pcmu_sender.hpp"
#include "resample.hpp"
#include "streaming_resampler.hpp"
#include "uds_server.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
int g_fail = 0;
void check(bool c, const char* m) {
    std::printf("[%s] %s\n", c ? "PASS" : "FAIL", m);
    if (!c) g_fail++;
}

// 표준 G.711 μ-law 인코드(테스트 입력 생성용)
uint8_t ulaw_encode(int s) {
    const int CLIP = 32635, BIAS = 0x84;
    int sign = (s < 0) ? 0x80 : 0;
    if (s < 0) s = -s;
    if (s > CLIP) s = CLIP;
    s += BIAS;
    int exponent = 7, mask = 0x4000;
    while (exponent > 0 && !(s & mask)) { mask >>= 1; exponent--; }
    int mantissa = (s >> (exponent + 3)) & 0x0f;
    return static_cast<uint8_t>(~(sign | (exponent << 4) | mantissa));
}
} // namespace

int main() {
    using namespace asr;

    // 1) 스트리밍 리샘플러 연속성
    {
        int rate = 8000;
        std::vector<int16_t> sig;
        for (int i = 0; i < 4000; ++i)
            sig.push_back(static_cast<int16_t>(8000 * std::sin(2 * M_PI * 1000.0 * i / rate)));

        std::vector<int16_t> block = resample_to_16k(sig, 8000); // 기준(블록)

        StreamingResampler8kTo16k rs;
        std::vector<int16_t> streamed;
        for (size_t off = 0; off < sig.size(); off += 160) { // 20ms 패킷
            size_t n = std::min<size_t>(160, sig.size() - off);
            auto o = rs.process(sig.data() + off, n);
            streamed.insert(streamed.end(), o.begin(), o.end());
        }
        check(streamed.size() == block.size(), "streamed output length == block output length");

        // 워밍업(앞 100샘플) 제외 중간 구간 비교(평균 절대 오차 작아야)
        double err = 0.0; int cnt = 0;
        for (size_t i = 200; i + 200 < std::min(streamed.size(), block.size()); ++i) {
            err += std::abs(streamed[i] - block[i]); cnt++;
        }
        double mae = cnt ? err / cnt : 1e9;
        char buf[128]; std::snprintf(buf, sizeof(buf), "streaming matches block (MAE=%.1f)", mae);
        check(mae < 50.0, buf);
    }

    // 2) PcmuStreamSender → 페이크 서버 end-to-end
    {
        std::string path = "/tmp/asr_send_" + std::to_string(getpid()) + ".sock";
        UdsServer srv;
        check(srv.listen(path), "fake server listen");

        std::atomic<long> recv_samples{0};
        std::atomic<bool> srv_done{false};
        std::thread server_thread([&] {
            int cfd = srv.accept();
            if (cfd < 0) return;
            MsgType type;
            std::string payload;
            bool sent_ack = false;
            while (read_frame(cfd, type, payload)) {
                if (type == MsgType::Audio) {
                    recv_samples += payload.size() / sizeof(int16_t);
                    // 실서버처럼 스트리밍 중 결과 프레임 회신(첫 오디오 수신 시 1회)
                    if (!sent_ack) {
                        write_frame(cfd, MsgType::Result, "{\"kind\":\"status\",\"state\":\"ack\"}");
                        sent_ack = true;
                    }
                } else if (type == MsgType::Ping) {
                    write_frame(cfd, MsgType::Pong, payload);
                }
            }
            ::close(cfd);
            srv_done = true;
        });

        std::atomic<int> got_results{0};
        PcmuSenderConfig cfg;
        cfg.socket_path = path;
        PcmuStreamSender sender(cfg, [&](const std::string&) { got_results++; });
        check(sender.start(), "sender connected & started");

        // 8k μ-law 0.5초(4000 샘플) 공급 → 16k 송신이면 ~8000 샘플 수신 기대
        const int N = 4000;
        std::vector<uint8_t> ul(N);
        for (int i = 0; i < N; ++i)
            ul[i] = ulaw_encode(static_cast<int>(6000 * std::sin(2 * M_PI * 440.0 * i / 8000)));
        for (int off = 0; off < N; off += 160)
            sender.feed_pcmu(ul.data() + off, std::min(160, N - off));

        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // 드레인 대기
        sender.stop();      // 송신 종료(소켓 닫힘 → 서버 read EOF)
        server_thread.join();

        long rs = recv_samples.load();
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "server received ~2x samples (got=%ld, expect ~8000)", rs);
        check(rs > 7000 && rs < 9000, buf);
        check(got_results.load() >= 1, "client received result frame(s)");
    }

    std::printf("\n%s\n", g_fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_fail == 0 ? 0 : 1;
}
