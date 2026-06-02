#include "frame_io.hpp"
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace asr {

ssize_t read_fully(int fd, void* buf, size_t n) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t got = 0;
    while (got < n) {
        ssize_t r = ::read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return (got > 0) ? static_cast<ssize_t>(got) : -1;
        }
        if (r == 0) break; // EOF
        got += static_cast<size_t>(r);
    }
    return static_cast<ssize_t>(got);
}

ssize_t write_fully(int fd, const void* buf, size_t n) {
    const auto* p = static_cast<const uint8_t*>(buf);
    size_t sent = 0;
    while (sent < n) {
        // send + MSG_NOSIGNAL: peer 종료 시 SIGPIPE 대신 EPIPE 반환(프로세스 보호).
        ssize_t w = ::send(fd, p + sent, n - sent, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return (sent > 0) ? static_cast<ssize_t>(sent) : -1;
        }
        sent += static_cast<size_t>(w);
    }
    return static_cast<ssize_t>(sent);
}

std::vector<uint8_t> encode_frame(const std::string& payload) {
    uint32_t len = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> out(4 + payload.size());
    out[0] = static_cast<uint8_t>(len & 0xff);
    out[1] = static_cast<uint8_t>((len >> 8) & 0xff);
    out[2] = static_cast<uint8_t>((len >> 16) & 0xff);
    out[3] = static_cast<uint8_t>((len >> 24) & 0xff);
    for (size_t i = 0; i < payload.size(); ++i)
        out[4 + i] = static_cast<uint8_t>(payload[i]);
    return out;
}

bool write_frame(int fd, const std::string& payload) {
    std::vector<uint8_t> frame = encode_frame(payload);
    return write_fully(fd, frame.data(), frame.size()) ==
           static_cast<ssize_t>(frame.size());
}

bool read_frame(int fd, std::string& payload, uint32_t max_len) {
    uint8_t hdr[4];
    if (read_fully(fd, hdr, 4) != 4) return false;
    uint32_t len = static_cast<uint32_t>(hdr[0]) |
                   (static_cast<uint32_t>(hdr[1]) << 8) |
                   (static_cast<uint32_t>(hdr[2]) << 16) |
                   (static_cast<uint32_t>(hdr[3]) << 24);
    if (len > max_len) return false; // 비정상/악의적 길이 방어
    payload.resize(len);
    if (len == 0) return true;
    return read_fully(fd, &payload[0], len) == static_cast<ssize_t>(len);
}

} // namespace asr
