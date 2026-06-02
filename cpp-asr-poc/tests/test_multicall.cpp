// 다중 통화 수락 루프 검증(모델 불필요): 하나의 listen 소켓에서 두 통화를 순차 처리하고,
// 통화마다 세션 리셋이 일어나며, 종료 신호(listen shutdown)로 accept 가 깨어나는지.
#include "frame_io.hpp"
#include "uds_client.hpp"
#include "uds_server.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
int g_fail = 0;
void check(bool c, const char* m) {
    std::printf("[%s] %s\n", c ? "PASS" : "FAIL", m);
    if (!c) g_fail++;
}
} // namespace

int main() {
    using namespace asr;

    std::string path = "/tmp/asr_multi_" + std::to_string(getpid()) + ".sock";
    UdsServer srv;
    check(srv.listen(path, 4), "server listen");

    std::atomic<bool> stop{false};
    std::atomic<int> sessions{0}, resets{0}, audio_frames{0};

    // 서버: asr_pipeline 의 다중 통화 루프 패턴(모델 부분만 제거)
    std::thread server_thread([&] {
        while (!stop.load()) {
            int cfd = srv.accept();
            if (cfd < 0) break; // 종료 신호(shutdown) 또는 에러
            resets++;           // det.reset() 자리(통화별 상태 격리)
            MsgType t;
            std::string p;
            while (!stop.load()) {
                int pr = wait_readable(cfd, 100);
                if (pr < 0) break;
                if (pr == 0) continue;
                if (!read_frame(cfd, t, p)) break; // EOF
                if (t == MsgType::Audio) audio_frames++;
                else if (t == MsgType::Bye) break;
            }
            ::close(cfd);
            sessions++;
        }
    });

    // 클라이언트: 두 통화를 순차로 진행
    for (int call = 1; call <= 2; ++call) {
        std::string err;
        int fd = uds_connect(path, err);
        check(fd >= 0, "client connected for a call");
        write_frame(fd, MsgType::Hello, "{\"v\":1}");
        write_frame(fd, MsgType::Audio, std::string(640, '\0'));
        write_frame(fd, MsgType::Audio, std::string(640, '\0'));
        write_frame(fd, MsgType::Bye, "{\"reason\":\"hangup\"}");
        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(150)); // 서버가 세션 마감할 시간
    }

    // 종료 신호: listen 소켓 shutdown → accept 깨어남
    stop = true;
    ::shutdown(srv.fd(), SHUT_RDWR);
    server_thread.join();

    char buf[160];
    std::snprintf(buf, sizeof(buf), "server handled exactly 2 calls (got %d)", sessions.load());
    check(sessions.load() == 2, buf);

    std::snprintf(buf, sizeof(buf), "per-call reset ran twice (resets=%d)", resets.load());
    check(resets.load() == 2, buf);

    std::snprintf(buf, sizeof(buf), "audio frames received across both calls (got %d)",
                  audio_frames.load());
    check(audio_frames.load() == 4, buf);

    std::printf("\n%s\n", g_fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_fail == 0 ? 0 : 1;
}
