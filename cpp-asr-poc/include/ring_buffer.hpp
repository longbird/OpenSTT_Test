// 고정 크기 오디오 링버퍼 + 백프레셔(drop-oldest) (운영화 골격).
//
// 설계 문서 반영: 추론이 느려져 버퍼가 차면 "오래된 오디오를 버린다"(실시간 우선).
// 무한 큐로 인한 메모리 폭증·지연 누적을 방지하고, 드롭율/최대깊이를 메트릭으로 노출.
//
// 스레드 모델: 단일 생산자(수신 스레드) - 단일 소비자(처리 스레드). 내부 뮤텍스로
// 보호한다(PoC 수준에서 정확성 우선; 필요 시 lock-free SPSC로 교체 가능).
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace asr {

struct RingMetrics {
    uint64_t pushed = 0;   // 누적 입력 샘플
    uint64_t popped = 0;   // 누적 소비 샘플
    uint64_t dropped = 0;  // 백프레셔로 버린 샘플
    size_t max_depth = 0;  // 관측된 최대 점유량
};

class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t capacity_samples);

    // 샘플 추가. 공간이 부족하면 가장 오래된 샘플을 버려 최신을 보존.
    // 반환: 이번 호출에서 버린 샘플 수.
    size_t push(const int16_t* data, size_t n);

    // 최대 max_n 샘플을 out 에 채워 반환(있는 만큼). 반환: 실제 채운 수.
    size_t pop(int16_t* out, size_t max_n);

    size_t size() const;       // 현재 점유 샘플 수
    size_t capacity() const { return cap_; }
    RingMetrics metrics() const;

private:
    mutable std::mutex mu_;
    std::vector<int16_t> buf_;
    size_t cap_ = 0;
    size_t head_ = 0; // 다음 읽기 위치
    size_t count_ = 0;
    RingMetrics m_;
};

} // namespace asr
