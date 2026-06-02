#include "ring_buffer.hpp"
#include <algorithm>

namespace asr {

AudioRingBuffer::AudioRingBuffer(size_t capacity_samples)
    : buf_(capacity_samples ? capacity_samples : 1),
      cap_(capacity_samples ? capacity_samples : 1) {}

size_t AudioRingBuffer::push(const int16_t* data, size_t n) {
    std::lock_guard<std::mutex> lk(mu_);
    size_t dropped_now = 0;

    // 입력이 용량보다 크면 뒤쪽(최신) cap_ 개만 의미 있음.
    if (n >= cap_) {
        dropped_now = count_ + (n - cap_);
        size_t off = n - cap_;
        for (size_t i = 0; i < cap_; ++i) buf_[i] = data[off + i];
        head_ = 0;
        count_ = cap_;
        m_.pushed += n;
        m_.dropped += dropped_now;
        m_.max_depth = std::max(m_.max_depth, count_);
        return dropped_now;
    }

    // 공간 부족분만큼 오래된 샘플을 버림(head 전진).
    size_t free_space = cap_ - count_;
    if (n > free_space) {
        size_t need = n - free_space;
        head_ = (head_ + need) % cap_;
        count_ -= need;
        dropped_now = need;
    }

    size_t tail = (head_ + count_) % cap_;
    for (size_t i = 0; i < n; ++i) {
        buf_[tail] = data[i];
        tail = (tail + 1) % cap_;
    }
    count_ += n;

    m_.pushed += n;
    m_.dropped += dropped_now;
    m_.max_depth = std::max(m_.max_depth, count_);
    return dropped_now;
}

size_t AudioRingBuffer::pop(int16_t* out, size_t max_n) {
    std::lock_guard<std::mutex> lk(mu_);
    size_t got = std::min(max_n, count_);
    for (size_t i = 0; i < got; ++i) {
        out[i] = buf_[head_];
        head_ = (head_ + 1) % cap_;
    }
    count_ -= got;
    m_.popped += got;
    return got;
}

size_t AudioRingBuffer::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return count_;
}

RingMetrics AudioRingBuffer::metrics() const {
    std::lock_guard<std::mutex> lk(mu_);
    return m_;
}

} // namespace asr
