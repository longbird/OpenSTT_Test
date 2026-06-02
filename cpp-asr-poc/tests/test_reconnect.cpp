// 스트림 중 재연결 검증: 서버가 연결을 끊었다가 다시 받으면,
// 송신기가 재연결하여 오디오 송신/결과 수신을 이어가는지.
#include "frame_io.hpp"
#include "pcmu_sender.hpp"
#include "uds_server.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
int g_fail = 0;
void check(bool c, const char* m) {
    std::printf("[%s] %s\n", c ? "PASS" : "FAIL", m);
    if (!c) g_fail++;
}
} // namespace

int main() {
    using namespace asr;

    std::string path = "/tmp/asr_recon_" + std::to_string(getpid()) + ".sock";
    UdsServer srv;
    check(srv.listen(path, 4), "server listen");

    std::atomic<long> recv1{0}, recv2{0};
    std::atomic<int> accepts{0};

    std::thread server_thread([&] {
        std::vector<int16_t> b(1600);
        // 연결 1: 일부 수신 후 ack, 그리고 강제 종료(크래시 모사)
        int c1 = srv.accept();
        if (c1 < 0) return;
        accepts++;
        ssize_t r = ::read(c1, b.data(), b.size() * sizeof(int16_t));
        if (r > 0) recv1 += r / (ssize_t)sizeof(int16_t);
        write_frame(c1, "{\"type\":\"status\",\"state\":\"ack1\"}");
        ::shutdown(c1, SHUT_RDWR);
        ::close(c1); // 연결 끊김

        // 연결 2: 재연결을 수락하고 EOF까지 수신
        int c2 = srv.accept();
        if (c2 < 0) return;
        accepts++;
        write_frame(c2, "{\"type\":\"status\",\"state\":\"ack2\"}");
        while (true) {
            ssize_t rr = ::read(c2, b.data(), b.size() * sizeof(int16_t));
            if (rr <= 0) break;
            recv2 += rr / (ssize_t)sizeof(int16_t);
        }
        ::close(c2);
    });

    std::mutex smu;
    std::vector<std::string> states;
    std::atomic<int> results{0};

    PcmuSenderConfig cfg;
    cfg.socket_path = path;
    cfg.connect_backoff_ms = 50; // 테스트 가속
    cfg.reconnect_backoff_cap_ms = 200;
    PcmuStreamSender sender(
        cfg,
        [&](const std::string&) { results++; },
        [&](const std::string& s) { std::lock_guard<std::mutex> lk(smu); states.push_back(s); });

    check(sender.start(), "sender started");

    // 끊김을 가로질러 ~1.5초간 계속 공급
    std::vector<uint8_t> pkt(160, 0xFF);
    for (int t = 0; t < 150 && sender.running(); ++t) {
        // 톤처럼 보이도록 약간 변형
        for (size_t i = 0; i < pkt.size(); ++i)
            pkt[i] = static_cast<uint8_t>(0x80 + ((t + i) % 60));
        sender.feed_pcmu(pkt.data(), pkt.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sender.stop();
    server_thread.join();

    char buf[160];
    std::snprintf(buf, sizeof(buf), "server accepted twice (got %d)", accepts.load());
    check(accepts.load() == 2, buf);

    std::snprintf(buf, sizeof(buf), "reconnect happened (reconnects=%d)", sender.reconnects());
    check(sender.reconnects() >= 1, buf);

    std::snprintf(buf, sizeof(buf), "server received audio after reconnect (recv2=%ld)",
                  recv2.load());
    check(recv2.load() > 0, buf);

    // 상태 시퀀스에 connected/reconnecting 포함
    {
        std::lock_guard<std::mutex> lk(smu);
        int connected = 0;
        bool reconnecting = false;
        for (auto& s : states) {
            if (s == "connected") connected++;
            if (s == "reconnecting") reconnecting = true;
        }
        std::snprintf(buf, sizeof(buf), "status has >=2 connected & reconnecting (conn=%d, re=%d)",
                      connected, reconnecting ? 1 : 0);
        check(connected >= 2 && reconnecting, buf);
    }

    std::snprintf(buf, sizeof(buf), "results received across reconnect (results=%d)",
                  results.load());
    check(results.load() >= 1, buf);

    std::printf("\n%s\n", g_fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_fail == 0 ? 0 : 1;
}
