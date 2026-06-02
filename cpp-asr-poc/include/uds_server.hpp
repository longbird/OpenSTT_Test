// Unix Domain Socket 서버 (운영화 골격).
//
// 설계 문서 반영: TCP 루프백 대비 오버헤드↓. 오디오 IN(연속 PCM 스트림)을 받고
// 결과 OUT(길이 prefix 프레임)을 돌려주는 양방향 채널. 스테일 소켓 정리 포함.
#pragma once
#include <string>

namespace asr {

class UdsServer {
public:
    UdsServer() = default;
    ~UdsServer();

    UdsServer(const UdsServer&) = delete;
    UdsServer& operator=(const UdsServer&) = delete;

    // path 에 바인드 후 listen. 기존 스테일 소켓 파일은 정리. 성공 시 true.
    bool listen(const std::string& path, int backlog = 1);

    // 클라이언트 연결을 수락해 fd 반환. 실패 시 -1.
    int accept();

    int fd() const { return server_fd_; }
    const std::string& error() const { return err_; }

    void close_server();

private:
    int server_fd_ = -1;
    std::string path_;
    std::string err_;
};

} // namespace asr
