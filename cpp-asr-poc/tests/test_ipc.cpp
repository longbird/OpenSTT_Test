// 운영화 골격 검증: 링버퍼(백프레셔) + 프레이밍 + UDS 왕복(실제 소켓).
#include "frame_io.hpp"
#include "ring_buffer.hpp"
#include "uds_server.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
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

    // 1) 링버퍼 백프레셔(drop-oldest)
    {
        AudioRingBuffer rb(10);
        std::vector<int16_t> a(6);
        for (int i = 0; i < 6; ++i) a[i] = static_cast<int16_t>(i); // 0..5
        size_t d1 = rb.push(a.data(), 6);
        check(d1 == 0 && rb.size() == 6, "push 6 into cap10: no drop");

        std::vector<int16_t> b(6);
        for (int i = 0; i < 6; ++i) b[i] = static_cast<int16_t>(100 + i); // 100..105
        size_t d2 = rb.push(b.data(), 6); // 총 12 > 10 → 오래된 2개 드롭
        check(d2 == 2 && rb.size() == 10, "overflow drops oldest 2, size==cap");

        std::vector<int16_t> out(10);
        size_t got = rb.pop(out.data(), 10);
        // 남은 것: 2,3,4,5,100..105 (0,1 드롭)
        bool ok = got == 10 && out[0] == 2 && out[1] == 3 && out[4] == 100 && out[9] == 105;
        check(ok, "pop returns newest-preserved order after drop");

        auto m = rb.metrics();
        check(m.pushed == 12 && m.dropped == 2 && m.popped == 10 && m.max_depth == 10,
              "ring metrics correct");
    }

    // 1b) push 가 용량 초과: 마지막 cap 개만 보존
    {
        AudioRingBuffer rb(4);
        std::vector<int16_t> big(10);
        for (int i = 0; i < 10; ++i) big[i] = static_cast<int16_t>(i);
        size_t d = rb.push(big.data(), 10);
        std::vector<int16_t> out(4);
        size_t got = rb.pop(out.data(), 4);
        bool ok = d == 6 && got == 4 && out[0] == 6 && out[3] == 9; // 마지막 4개(6,7,8,9)
        check(ok, "oversized push keeps last capacity samples");
    }

    // 2) 프레이밍 왕복(socketpair) + 부분 read 재조립
    {
        int sv[2];
        check(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair created");
        std::string big(100000, 'x');
        for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>('A' + (i % 26));
        std::thread writer([&] {
            write_frame(sv[1], MsgType::Hello, "hello");
            write_frame(sv[1], MsgType::Audio, big);
        });
        MsgType t1, t2;
        std::string p1, p2;
        bool r1 = read_frame(sv[0], t1, p1);
        bool r2 = read_frame(sv[0], t2, p2);
        writer.join();
        check(r1 && t1 == MsgType::Hello && p1 == "hello", "small typed frame roundtrip");
        check(r2 && t2 == MsgType::Audio && p2 == big && p2.size() == 100000,
              "large frame reassembled across reads with type");
        close(sv[0]); close(sv[1]);
    }

    // 3) UDS 서버 end-to-end: 오디오 IN(raw) → 결과 OUT(frame)
    {
        std::string path = "/tmp/asr_test_" + std::to_string(getpid()) + ".sock";
        UdsServer srv;
        bool listening = srv.listen(path);
        check(listening, "uds server listen");

        const int kSamples = 320; // 16k, 20ms
        std::thread server_thread([&] {
            int cfd = srv.accept();
            if (cfd < 0) return;
            std::vector<int16_t> audio(kSamples);
            ssize_t got = read_fully(cfd, audio.data(), kSamples * sizeof(int16_t));
            std::string msg = "{\"recv_samples\":" +
                              std::to_string(got / (ssize_t)sizeof(int16_t)) + "}";
            write_frame(cfd, MsgType::Result, msg);
            close(cfd);
        });

        // 클라이언트 연결
        int cli = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        bool connected = connect(cli, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
        check(connected, "client connected to uds");

        std::vector<int16_t> audio(kSamples, 7);
        write_fully(cli, audio.data(), kSamples * sizeof(int16_t));
        MsgType rtype;
        std::string reply;
        bool got_reply = read_frame(cli, rtype, reply);
        check(got_reply && rtype == MsgType::Result && reply == "{\"recv_samples\":320}",
              "server received audio and framed reply back");
        close(cli);
        server_thread.join();
    }

    std::printf("\n%s\n", g_fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_fail == 0 ? 0 : 1;
}
