// PCMU 스트림 송신 메커니즘 (메인 백엔드 측).
//
// 역할: 전화망에서 도착하는 8kHz G.711 μ-law(PCMU) 패킷을 받아
//       (1) PCM16 디코드 → (2) 8k→16k 스트리밍 리샘플 → (3) UDS 로 raw 송신.
//       결과 프레임(consent/transcript/status)은 콜백으로 전달.
//
// 스레드 모델:
//   - feed_pcmu(): 전화망 콜백에서 호출. 디코드 후 링버퍼에 push(비블로킹, 백프레셔).
//   - 송신 스레드: 링버퍼 → 스트리밍 리샘플 → UDS send.
//   - 수신 스레드: UDS 결과 프레임 → on_result 콜백.
//
// 설계 의도: 전화망 콜백은 절대 블로킹되지 않는다(큐에 넣고 즉시 반환).
// 추론/네트워크가 느리면 링버퍼가 가장 오래된 오디오를 버려 지연 누적을 막는다.
#pragma once
#include "ring_buffer.hpp"
#include "streaming_resampler.hpp"
#include "uds_client.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace asr {

struct PcmuSenderConfig {
    std::string socket_path = "/tmp/asr.sock";
    size_t ring_capacity_8k = 8000 * 4; // 8kHz 기준 약 4초
    size_t send_chunk_8k = 160;          // 송신 단위(8k 샘플): 20ms
    int connect_retries = 4;
    int connect_backoff_ms = 200;
};

class PcmuStreamSender {
public:
    using ResultFn = std::function<void(const std::string& json)>;

    explicit PcmuStreamSender(const PcmuSenderConfig& cfg, ResultFn on_result = nullptr);
    ~PcmuStreamSender();

    PcmuStreamSender(const PcmuStreamSender&) = delete;
    PcmuStreamSender& operator=(const PcmuStreamSender&) = delete;

    // UDS 연결 후 송신/수신 스레드 시작. 성공 시 true.
    bool start();

    // 전화망 콜백에서 호출: PCMU(μ-law) 바이트를 공급(비블로킹).
    void feed_pcmu(const uint8_t* ulaw, size_t n);

    // 스트림 종료 후 스레드 정리.
    void stop();

    bool running() const { return running_.load(); }
    const std::string& error() const { return err_; }
    RingMetrics ring_metrics() const { return ring_.metrics(); }

private:
    void send_loop();
    void recv_loop();

    PcmuSenderConfig cfg_;
    ResultFn on_result_;
    UdsClient client_;
    AudioRingBuffer ring_;              // 8kHz PCM16 백프레셔 큐
    StreamingResampler8kTo16k resampler_; // 송신 스레드 전용
    std::thread send_thread_;
    std::thread recv_thread_;
    std::atomic<bool> running_{false};
    std::string err_;
};

} // namespace asr
