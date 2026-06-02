#include "frame_io.hpp"
#include <cerrno>
#include <cstring>
#include <poll.h>
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
        // send + MSG_NOSIGNAL: peer 종료 시 SIGPIPE 대신 EPIPE (프로세스 보호).
        ssize_t w = ::send(fd, p + sent, n - sent, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return (sent > 0) ? static_cast<ssize_t>(sent) : -1;
        }
        sent += static_cast<size_t>(w);
    }
    return static_cast<ssize_t>(sent);
}

std::vector<uint8_t> encode_frame(MsgType type, const void* payload, size_t n) {
    uint32_t len = static_cast<uint32_t>(1 + n); // type + payload
    std::vector<uint8_t> out(4 + 1 + n);
    out[0] = static_cast<uint8_t>(len & 0xff);
    out[1] = static_cast<uint8_t>((len >> 8) & 0xff);
    out[2] = static_cast<uint8_t>((len >> 16) & 0xff);
    out[3] = static_cast<uint8_t>((len >> 24) & 0xff);
    out[4] = static_cast<uint8_t>(type);
    if (n) std::memcpy(out.data() + 5, payload, n);
    return out;
}

bool write_frame(int fd, MsgType type, const void* payload, size_t n) {
    std::vector<uint8_t> frame = encode_frame(type, payload, n);
    return write_fully(fd, frame.data(), frame.size()) ==
           static_cast<ssize_t>(frame.size());
}

bool write_frame(int fd, MsgType type, const std::string& payload) {
    return write_frame(fd, type, payload.data(), payload.size());
}

bool read_frame(int fd, MsgType& type, std::string& payload, uint32_t max_len) {
    uint8_t hdr[4];
    if (read_fully(fd, hdr, 4) != 4) return false;
    uint32_t len = static_cast<uint32_t>(hdr[0]) |
                   (static_cast<uint32_t>(hdr[1]) << 8) |
                   (static_cast<uint32_t>(hdr[2]) << 16) |
                   (static_cast<uint32_t>(hdr[3]) << 24);
    if (len < 1 || len > max_len) return false; // 최소 type 1바이트, 과대길이 방어
    uint8_t t;
    if (read_fully(fd, &t, 1) != 1) return false;
    type = static_cast<MsgType>(t);
    uint32_t plen = len - 1;
    payload.resize(plen);
    if (plen == 0) return true;
    return read_fully(fd, &payload[0], plen) == static_cast<ssize_t>(plen);
}

int wait_readable(int fd, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int r;
    do { r = ::poll(&pfd, 1, timeout_ms); } while (r < 0 && errno == EINTR);
    if (r < 0) return -1;
    if (r == 0) return 0;
    // POLLHUP/POLLERR 도 read 가 즉시 반환(EOF/에러)하므로 readable 로 취급
    if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) return 1;
    return 0;
}

} // namespace asr
