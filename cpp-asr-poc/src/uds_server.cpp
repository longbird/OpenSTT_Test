#include "uds_server.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace asr {

UdsServer::~UdsServer() { close_server(); }

bool UdsServer::listen(const std::string& path, int backlog) {
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        err_ = "socket path too long: " + path;
        return false;
    }
    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        err_ = std::string("socket() failed: ") + std::strerror(errno);
        return false;
    }

    // 스테일 소켓 파일 제거(이전 비정상 종료 대비). 소켓 타입일 때만.
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)) {
        ::unlink(path.c_str());
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        err_ = std::string("bind() failed: ") + std::strerror(errno);
        close_server();
        return false;
    }
    if (::listen(server_fd_, backlog) < 0) {
        err_ = std::string("listen() failed: ") + std::strerror(errno);
        close_server();
        return false;
    }
    path_ = path;
    return true;
}

int UdsServer::accept() {
    if (server_fd_ < 0) return -1;
    int fd;
    do {
        fd = ::accept(server_fd_, nullptr, nullptr);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) err_ = std::string("accept() failed: ") + std::strerror(errno);
    return fd;
}

void UdsServer::close_server() {
    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }
    if (!path_.empty()) {
        ::unlink(path_.c_str());
        path_.clear();
    }
}

} // namespace asr
