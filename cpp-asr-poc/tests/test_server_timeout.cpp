// 서버측 유휴 타임아웃 검증: 클라이언트가 연결은 유지한 채 무응답이면,
// 서버가 (EOF가 아니라) 타임아웃으로 세션을 정리하는지. (모델 불필요)
#include "frame_io.hpp"
#include "uds_client.hpp"
#include "uds_server.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
int g_fail = 0;
void check(bool c, const char* m) {
    std::printf("[%s] %s\n", c ? "PASS" : "FAIL", m);
    if (!c) g_fail++;
}
long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

int main() {
    using namespace asr;

    std::string path = "/tmp/asr_to_" + std::to_string(getpid()) + ".sock";
    UdsServer srv;
    check(srv.listen(path), "server listen");

    const int kIdleMs = 600, kPollMs = 100;
    std::atomic<bool> timed_out{false};
    std::atomic<int> frames{0};
    std::atomic<long long> elapsed{0};

    // 서버: main_pipeline 의 유휴 타임아웃 패턴을 그대로 모사
    std::thread server_thread([&] {
        int cfd = srv.accept();
        if (cfd < 0) return;
        long long start = now_ms(), last_rx = start;
        MsgType type;
        std::string payload;
        while (true) {
            int pr = wait_readable(cfd, kPollMs);
            if (pr < 0) break;
            if (pr == 0) {
                if (now_ms() - last_rx > kIdleMs) { timed_out = true; break; }
                continue;
            }
            if (!read_frame(cfd, type, payload)) break; // EOF/에러
            last_rx = now_ms();
            frames++;
        }
        elapsed = now_ms() - start;
        ::close(cfd);
    });

    // 클라이언트: 프레임 3개 전송 후 연결을 유지한 채 침묵(EOF 아님)
    std::string err;
    int cli = uds_connect(path, err);
    check(cli >= 0, "client connected");
    write_frame(cli, MsgType::Hello, "{\"v\":1}");
    write_frame(cli, MsgType::Audio, std::string(640, '\0'));
    write_frame(cli, MsgType::Audio, std::string(640, '\0'));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500)); // 무응답
    ::close(cli);

    server_thread.join();

    char buf[160];
    std::snprintf(buf, sizeof(buf), "server detected idle timeout (not EOF) [timed=%d]",
                  timed_out.load() ? 1 : 0);
    check(timed_out.load(), buf);

    std::snprintf(buf, sizeof(buf), "server received exactly 3 frames (got %d)", frames.load());
    check(frames.load() == 3, buf);

    long long e = elapsed.load();
    std::snprintf(buf, sizeof(buf), "timeout fired in plausible window (~%lldms)", e);
    check(e >= 500 && e <= 1300, buf);

    std::printf("\n%s\n", g_fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_fail == 0 ? 0 : 1;
}
