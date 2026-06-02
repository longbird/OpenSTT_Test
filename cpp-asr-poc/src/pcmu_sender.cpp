#include "pcmu_sender.hpp"
#include "frame_io.hpp"
#include "g711.hpp"
#include "uds_client.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace asr {

namespace {
long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

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
    need_hello_ = true;
    last_rx_ms_ = now_ms();
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
            need_hello_ = true;
            last_rx_ms_ = now_ms();
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
    long long last_ping = now_ms();
    while (running_.load()) {
        if (!ensure_connected()) break;

        uint64_t gen;
        int fd = snapshot_fd(gen);
        if (fd < 0) continue;

        // 새 연결이면 HELLO 핸드셰이크 먼저 전송
        if (need_hello_) {
            std::string hello = "{\"v\":1,\"codec\":\"pcm16\",\"rate\":" +
                                std::to_string(cfg_.sample_rate_out) + ",\"ch\":1}";
            if (!write_frame(fd, MsgType::Hello, hello)) { mark_dead(gen); continue; }
            need_hello_ = false;
            last_ping = now_ms();
        }

        long long now = now_ms();
        // 워치독: RX 타임아웃 → 죽은 연결로 간주하고 재연결
        if (cfg_.rx_timeout_ms > 0 && now - last_rx_ms_.load() > cfg_.rx_timeout_ms) {
            notify_status("timeout");
            mark_dead(gen);
            continue;
        }
        // 주기적 PING
        if (cfg_.ping_interval_ms > 0 && now - last_ping >= cfg_.ping_interval_ms) {
            std::string p = "{\"ts\":" + std::to_string(now) + "}";
            if (!write_frame(fd, MsgType::Ping, p)) { mark_dead(gen); continue; }
            last_ping = now;
        }

        size_t got = ring_.pop(chunk.data(), chunk.size());
        if (got == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        std::vector<int16_t> pcm16k = resampler_.process(chunk.data(), got);
        if (!write_frame(fd, MsgType::Audio, pcm16k.data(),
                         pcm16k.size() * sizeof(int16_t))) {
            notify_status("disconnected");
            mark_dead(gen);
        }
    }
}

void PcmuStreamSender::recv_loop() {
    std::string payload;
    MsgType type;
    while (running_.load()) {
        int fd;
        uint64_t gen;
        {
            std::unique_lock<std::mutex> lk(cmu_);
            ccv_.wait(lk, [&] { return !running_.load() || conn_alive_; });
            if (!running_.load()) break;
            fd = fd_;
            gen = gen_;
        }
        while (running_.load()) {
            if (!read_frame(fd, type, payload)) {
                mark_dead(gen);
                break;
            }
            last_rx_ms_ = now_ms(); // 모든 수신은 연결 생존 신호
            if (type == MsgType::Result && on_result_) on_result_(payload);
            // Pong 등 기타 타입은 생존 갱신만(이미 위에서 처리)
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
