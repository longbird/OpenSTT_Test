#include "pcmu_sender.hpp"
#include "frame_io.hpp"
#include "g711.hpp"
#include "uds_client.hpp"

#include <algorithm>
#include <chrono>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace asr {

PcmuStreamSender::PcmuStreamSender(const PcmuSenderConfig& cfg, ResultFn on_result,
                                   StatusFn on_status)
    : cfg_(cfg), on_result_(std::move(on_result)), on_status_(std::move(on_status)),
      ring_(cfg.ring_capacity_8k) {}

PcmuStreamSender::~PcmuStreamSender() { stop(); }

void PcmuStreamSender::notify_status(const std::string& s) {
    if (on_status_) on_status_(s);
}

bool PcmuStreamSender::start() {
    std::string err;
    int fd = -1;
    int delay = cfg_.connect_backoff_ms;
    for (int attempt = 0; attempt <= cfg_.connect_retries; ++attempt) {
        fd = uds_connect(cfg_.socket_path, err);
        if (fd >= 0) break;
        if (attempt < cfg_.connect_retries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            delay = std::min(delay * 2, cfg_.reconnect_backoff_cap_ms);
        }
    }
    if (fd < 0) { err_ = "connect failed: " + err; return false; }

    {
        std::lock_guard<std::mutex> lk(cmu_);
        fd_ = fd;
        conn_alive_ = true;
        gen_ = 1;
    }
    notify_status("connected");
    running_ = true;
    send_thread_ = std::thread(&PcmuStreamSender::send_loop, this);
    recv_thread_ = std::thread(&PcmuStreamSender::recv_loop, this);
    return true;
}

void PcmuStreamSender::feed_pcmu(const uint8_t* ulaw, size_t n) {
    std::vector<int16_t> pcm(n);
    for (size_t i = 0; i < n; ++i) pcm[i] = ulaw_to_pcm16(ulaw[i]);
    ring_.push(pcm.data(), pcm.size()); // 가득 차면 오래된 것 드롭(백프레셔)
}

int PcmuStreamSender::snapshot_fd(uint64_t& gen) {
    std::lock_guard<std::mutex> lk(cmu_);
    gen = gen_;
    return conn_alive_ ? fd_ : -1;
}

void PcmuStreamSender::mark_dead(uint64_t gen) {
    std::lock_guard<std::mutex> lk(cmu_);
    if (conn_alive_ && gen_ == gen) {
        conn_alive_ = false;
        if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); fd_ = -1; }
        ccv_.notify_all(); // 수신 스레드의 wait/블로킹 read 해제
    }
}

// 송신 스레드 전용: 살아있으면 즉시 true, 아니면 백오프 재연결.
bool PcmuStreamSender::ensure_connected() {
    {
        std::lock_guard<std::mutex> lk(cmu_);
        if (conn_alive_) return true;
    }
    notify_status("reconnecting");
    int delay = cfg_.connect_backoff_ms;
    int attempts = 0;
    while (running_.load()) {
        std::string err;
        int fd = uds_connect(cfg_.socket_path, err);
        if (fd >= 0) {
            {
                std::lock_guard<std::mutex> lk(cmu_);
                fd_ = fd;
                conn_alive_ = true;
                ++gen_;
                ccv_.notify_all();
            }
            resampler_ = StreamingResampler8kTo16k(); // 새 스트림: 리샘플 상태 리셋
            reconnects_++;
            notify_status("connected");
            return true;
        }
        if (cfg_.max_reconnects >= 0 && ++attempts > cfg_.max_reconnects) {
            err_ = "reconnect gave up: " + err;
            notify_status("gave_up");
            running_ = false;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        delay = std::min(delay * 2, cfg_.reconnect_backoff_cap_ms);
    }
    return false;
}

void PcmuStreamSender::send_loop() {
    std::vector<int16_t> chunk(cfg_.send_chunk_8k);
    while (running_.load()) {
        if (!ensure_connected()) break;

        size_t got = ring_.pop(chunk.data(), chunk.size());
        if (got == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        std::vector<int16_t> pcm16k = resampler_.process(chunk.data(), got);

        uint64_t gen;
        int fd = snapshot_fd(gen);
        if (fd < 0) continue; // 방금 끊김 → 다음 루프에서 재연결
        ssize_t want = static_cast<ssize_t>(pcm16k.size() * sizeof(int16_t));
        if (write_fully(fd, pcm16k.data(), pcm16k.size() * sizeof(int16_t)) != want) {
            notify_status("disconnected");
            mark_dead(gen); // 재연결 유도
        }
    }
}

void PcmuStreamSender::recv_loop() {
    std::string payload;
    while (running_.load()) {
        // 살아있는 연결을 기다렸다가 fd+세대 스냅샷
        int fd;
        uint64_t gen;
        {
            std::unique_lock<std::mutex> lk(cmu_);
            ccv_.wait(lk, [&] { return !running_.load() || conn_alive_; });
            if (!running_.load()) break;
            fd = fd_;
            gen = gen_;
        }
        // 이 세대 연결이 살아있는 동안 결과 프레임 수신
        while (running_.load()) {
            if (!read_frame(fd, payload)) {
                mark_dead(gen); // 연결 종료 → 바깥 wait 로 돌아가 새 세대 대기
                break;
            }
            if (on_result_) on_result_(payload);
        }
    }
}

void PcmuStreamSender::stop() {
    if (!running_.exchange(false) && !send_thread_.joinable() && !recv_thread_.joinable())
        return;
    running_ = false;
    {
        std::lock_guard<std::mutex> lk(cmu_);
        conn_alive_ = false;
        if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); fd_ = -1; }
        ccv_.notify_all();
    }
    if (send_thread_.joinable()) send_thread_.join();
    if (recv_thread_.joinable()) recv_thread_.join();
}

} // namespace asr
