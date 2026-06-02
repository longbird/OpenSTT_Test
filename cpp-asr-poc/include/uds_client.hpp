// UDS 클라이언트 (송신측: 메인 백엔드 → AI 추론 프로세스).
//
// 연결 재시도(지수 백오프) + 끊김 시 재연결. 오디오는 raw PCM16 스트림으로 송신,
// 결과는 길이 prefix 프레임으로 수신.
#pragma once
#include <string>

namespace asr {

class UdsClient {
public:
    UdsClient() = default;
    ~UdsClient();

    UdsClient(const UdsClient&) = delete;
    UdsClient& operator=(const UdsClient&) = delete;

    // path 에 연결. 실패 시 backoff(2,4,8,...ms*base)로 retries 회 재시도.
    bool connect(const std::string& path, int retries = 4, int backoff_base_ms = 200);

    bool connected() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    const std::string& error() const { return err_; }

    // 16k PCM16 mono 샘플을 write_fully 로 송신. 실패(소켓 끊김) 시 false.
    bool send_pcm16(const int16_t* samples, size_t n);

    // 결과 프레임 1개 수신(블로킹). 성공 시 true, 종료/에러 시 false.
    bool recv_result(std::string& payload);

    void close();

private:
    int fd_ = -1;
    std::string path_;
    std::string err_;
};

} // namespace asr
