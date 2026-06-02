// PCMU 스트림 송신 메커니즘 (메인 백엔드 측) — 스트림 중 재연결 지원.
//
// 역할: 전화망 8kHz μ-law(PCMU) → PCM16 디코드 → 8k→16k 스트리밍 리샘플 → UDS 송신.
//       결과 프레임(consent/transcript/status)은 on_result 콜백으로 전달.
//
// 스레드 모델:
//   - feed_pcmu(): 전화망 콜백. 디코드 후 링버퍼 push(비블로킹, 백프레셔).
//   - 송신 스레드: 링버퍼 → 스트리밍 리샘플 → UDS send. **재연결 소유자.**
//   - 수신 스레드: UDS 결과 프레임 → on_result. generation 변경을 따라 새 연결로 전환.
//
// 재연결: 송신 스레드만 (re)connect 한다(이중 연결 방지). 연결/세대(generation)는
// 뮤텍스+조건변수로 보호하고, 끊긴 fd 는 shutdown 으로 수신 스레드의 블로킹 read 를 깨운다.
// 상태 변화(connected/disconnected/reconnecting/gave_up)는 on_status 로 통지 →
// 상위에서 graceful degradation(상담원 수동 확인 폴백) 가능.
#pragma once
#include "ring_buffer.hpp"
#include "streaming_resampler.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace asr {

struct PcmuSenderConfig {
    std::string socket_path = "/tmp/asr.sock";
    size_t ring_capacity_8k = 8000 * 4; // 8kHz 기준 약 4초
    size_t send_chunk_8k = 160;          // 송신 단위(8k 샘플): 20ms
    int connect_retries = 4;             // 최초 연결 재시도 횟수
    int connect_backoff_ms = 200;        // 백오프 시작값
    int reconnect_backoff_cap_ms = 2000; // 재연결 백오프 상한
    int max_reconnects = -1;             // -1 = 무제한
};

class PcmuStreamSender {
public:
    using ResultFn = std::function<void(const std::string& json)>;
    using StatusFn = std::function<void(const std::string& state)>;

    explicit PcmuStreamSender(const PcmuSenderConfig& cfg, ResultFn on_result = nullptr,
                              StatusFn on_status = nullptr);
    ~PcmuStreamSender();

    PcmuStreamSender(const PcmuStreamSender&) = delete;
    PcmuStreamSender& operator=(const PcmuStreamSender&) = delete;

    // 최초 연결 후 송신/수신 스레드 시작. 성공 시 true.
    bool start();

    // 전화망 콜백에서 호출: PCMU(μ-law) 바이트 공급(비블로킹).
    void feed_pcmu(const uint8_t* ulaw, size_t n);

    void stop();

    bool running() const { return running_.load(); }
    const std::string& error() const { return err_; }
    RingMetrics ring_metrics() const { return ring_.metrics(); }
    int reconnects() const { return reconnects_.load(); }

private:
    void send_loop();
    void recv_loop();

    // 연결 상태(송/수신 공유) — cmu_ 로 보호.
    bool ensure_connected();           // 송신 스레드 전용: (재)연결, 중지 시 false
    void mark_dead(uint64_t gen);      // 해당 세대 연결을 죽음 처리(shutdown+close)
    int  snapshot_fd(uint64_t& gen);   // 현재 fd+세대 스냅샷(없으면 -1)
    void notify_status(const std::string& s);

    PcmuSenderConfig cfg_;
    ResultFn on_result_;
    StatusFn on_status_;
    AudioRingBuffer ring_;
    StreamingResampler8kTo16k resampler_; // 송신 스레드 전용

    std::mutex cmu_;
    std::condition_variable ccv_;
    int fd_ = -1;
    uint64_t gen_ = 0;
    bool conn_alive_ = false;

    std::thread send_thread_;
    std::thread recv_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> reconnects_{0};
    std::string err_;
};

} // namespace asr
