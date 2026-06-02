// UDS 스트림용 입출력 헬퍼 (운영화 골격).
//
// 설계 문서 반영:
//  - 스트림 소켓 read()는 요청 바이트를 한 번에 다 주지 않음 → read_fully 필수.
//  - 오디오 IN은 연속 PCM 스트림(프레이밍 없음), 결과 OUT은 메시지 → 길이 prefix 프레이밍.
//
// 결과 프레임 포맷: [u32 length(LE)][payload bytes]  (length = payload 바이트 수)
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace asr {

// 정확히 n 바이트를 읽을 때까지 반복(EOF/에러 시 중단). 반환: 실제 읽은 바이트.
ssize_t read_fully(int fd, void* buf, size_t n);

// 정확히 n 바이트를 쓸 때까지 반복. 반환: 실제 쓴 바이트.
ssize_t write_fully(int fd, const void* buf, size_t n);

// payload 를 [u32 len][payload] 프레임으로 전송. 성공 시 true.
bool write_frame(int fd, const std::string& payload);

// 한 프레임을 수신해 payload 에 채움. 성공 시 true, 연결 종료/에러 시 false.
bool read_frame(int fd, std::string& payload, uint32_t max_len = 16u * 1024u * 1024u);

// 프레임 바이트열 인코딩(테스트/디버그용, 순수 함수).
std::vector<uint8_t> encode_frame(const std::string& payload);

} // namespace asr
