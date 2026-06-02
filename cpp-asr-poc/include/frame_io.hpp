// 통합 TLV 프레임 프로토콜 (운영화).
//
// 프레임:  [u32 length(LE)] [u8 type] [payload(length-1 bytes)]
//   - length = 1(type) + payload 바이트 수
//   - 오디오는 raw PCM16LE 바이너리, 제어/결과는 JSON (하이브리드)
//   - 양방향 동일 포맷 → 자기기술(self-describing) + 확장 가능
//
// 설계: 스트림 소켓 read()는 부분 수신되므로 read_fully 필수. send 는 MSG_NOSIGNAL.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace asr {

enum class MsgType : uint8_t {
    Hello  = 0x01, // JSON: 핸드셰이크/포맷 협상 {"v":1,"codec":"pcm16","rate":16000,"ch":1,...}
    Audio  = 0x02, // raw PCM16LE
    Ping   = 0x03, // 워치독: {"ts":<ms>} 또는 빈 payload
    Pong   = 0x04, // 워치독 응답: {"ts":<ms>}
    Result = 0x10, // JSON: {"kind":"consent"|"transcript"|"status", ...}
    Bye    = 0x1f, // JSON: {"reason":"..."} 정상 종료
};

// 정확히 n 바이트 read/write (부분 수신/송신 재조립). 반환: 실제 바이트(-1 에러).
ssize_t read_fully(int fd, void* buf, size_t n);
ssize_t write_fully(int fd, const void* buf, size_t n);

// 프레임 바이트열 인코딩(순수 함수, 테스트/디버그용).
std::vector<uint8_t> encode_frame(MsgType type, const void* payload, size_t n);

// 프레임 송신. 성공 시 true. (단일 write 로 헤더+payload 전송)
bool write_frame(int fd, MsgType type, const void* payload, size_t n);
bool write_frame(int fd, MsgType type, const std::string& payload);

// 프레임 1개 수신. 성공 시 true(type/payload 채움), 종료/에러/과대길이 시 false.
bool read_frame(int fd, MsgType& type, std::string& payload,
                uint32_t max_len = 16u * 1024u * 1024u);

} // namespace asr
