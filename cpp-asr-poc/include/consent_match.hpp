// 동의 구문 그래머 생성 + 텍스트 매칭 (libvosk 비의존, 단위 테스트 가능).
#pragma once
#include <string>
#include <vector>

namespace asr {

// phrases → Vosk 그래머 JSON 배열 문자열. "[unk]" 포함으로 OOV 흡수(FP 억제).
std::string build_grammar_json(const std::vector<std::string>& phrases);

// 인식 텍스트가 동의 구문을 포함하는지(공백 정규화 후 토큰/구문 매칭).
bool text_matches_consent(const std::string& text,
                          const std::vector<std::string>& phrases);

} // namespace asr
