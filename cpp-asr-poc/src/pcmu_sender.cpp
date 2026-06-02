#include "pcmu_sender.hpp"
#include "g711.hpp"

#include <chrono>
#include <vector>

namespace asr {

PcmuStreamSender::PcmuStreamSender(const PcmuSenderConfig& cfg, ResultFn on_result)
    : cfg_(cfg), on_result_(std::move(on_result)), ring_(cfg.ring_capacity_8k) {}

PcmuStreamSender::~PcmuStreamSender() { stop(); }

bool PcmuStreamSender::start() {
    if (!client_.connect(cfg_.socket_path, cfg_.connect_retries, cfg_.connect_backoff_ms)) {
        err_ = "connect failed: " + client_.error();
        return false;
    }
    running_ = true;
    send_thread_ = std::thread(&PcmuStreamSender::send_loop, this);
    recv_thread_ = std::thread(&PcmuStreamSender::recv_loop, this);
    return true;
}

void PcmuStreamSender::feed_pcmu(const uint8_t* ulaw, size_t n) {
    // 전화망 콜백: μ-law → PCM16(8k) 디코드 후 링버퍼에 push(비블로킹).
    std::vector<int16_t> pcm(n);
    for (size_t i = 0; i < n; ++i) pcm[i] = ulaw_to_pcm16(ulaw[i]);
    ring_.push(pcm.data(), pcm.size()); // 가득 차면 오래된 것 드롭(백프레셔)
}

void PcmuStreamSender::send_loop() {
    std::vector<int16_t> chunk(cfg_.send_chunk_8k);
    while (running_.load()) {
        size_t got = ring_.pop(chunk.data(), chunk.size());
        if (got == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        std::vector<int16_t> pcm16k = resampler_.process(chunk.data(), got);
        if (!client_.send_pcm16(pcm16k.data(), pcm16k.size())) {
            err_ = "send failed: " + client_.error();
            running_ = false; // 연결 끊김 → 종료(상위에서 재시작/폴백 처리)
            break;
        }
    }
}

void PcmuStreamSender::recv_loop() {
    std::string payload;
    while (running_.load()) {
        if (!client_.recv_result(payload)) break; // 소켓 종료/에러
        if (on_result_) on_result_(payload);
    }
}

void PcmuStreamSender::stop() {
    if (!running_.exchange(false) && !send_thread_.joinable() && !recv_thread_.joinable())
        return;
    running_ = false;
    client_.close(); // recv_loop 의 블로킹 read 해제
    if (send_thread_.joinable()) send_thread_.join();
    if (recv_thread_.joinable()) recv_thread_.join();
}

} // namespace asr
