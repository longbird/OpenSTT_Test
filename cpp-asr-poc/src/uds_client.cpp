#include "uds_client.hpp"
#include "frame_io.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace asr {

int uds_connect(const std::string& path, std::string& err) {
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        err = "socket path too long: " + path;
        return -1;
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { err = std::string("socket(): ") + std::strerror(errno); return -1; }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        err = std::string("connect(): ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    return fd;
}

UdsClient::~UdsClient() { close(); }

bool UdsClient::connect(const std::string& path, int retries, int backoff_base_ms) {
    path_ = path;
    int delay = backoff_base_ms;
    for (int attempt = 0; attempt <= retries; ++attempt) {
        int fd = uds_connect(path, err_);
        if (fd >= 0) { fd_ = fd; return true; }
        if (attempt < retries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            delay *= 2; // 지수 백오프
        }
    }
    return false;
}

bool UdsClient::send_pcm16(const int16_t* samples, size_t n) {
    if (fd_ < 0) return false;
    ssize_t want = static_cast<ssize_t>(n * sizeof(int16_t));
    if (write_fully(fd_, samples, n * sizeof(int16_t)) != want) {
        err_ = "send_pcm16: socket write failed (peer closed?)";
        close();
        return false;
    }
    return true;
}

bool UdsClient::recv_result(std::string& payload) {
    if (fd_ < 0) return false;
    return read_frame(fd_, payload);
}

void UdsClient::close() {
    if (fd_ >= 0) {
        // shutdown 으로 다른 스레드의 블로킹 read/write 를 깨운다.
        // (close 단독으로는 블로킹된 read 가 깨어나지 않을 수 있음)
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace asr
