// Vosk 그래머 제약 인식기 래퍼 + 동의("네/오케이") 감지 (PoC-B).
//
// 핵심 아이디어: vosk_recognizer_new_grm 에 동의 구문 목록을 JSON 그래머로 주어
// 디코더 탐색공간을 제한한다 → 오탐(FP)↓, 지연↓. 결과 텍스트에서 동의 구문이
// 임계 신뢰도 이상으로 감지되면 consent=true.
#pragma once
#include "consent_match.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace asr {

struct ConsentConfig {
    std::string model_path;                  // Vosk 한국어 모델 디렉토리
    float sample_rate = 16000.0f;            // 모델 입력 레이트(16k)
    std::vector<std::string> phrases = {     // 동의로 간주할 구문(그래머)
        "네", "예", "그래요", "좋아요", "오케이", "올려 주세요", "그렇게 해 주세요"
    };
    double min_confidence = 0.6;             // 단어 평균 conf 임계값
};

struct ConsentEvent {
    bool consent = false;
    std::string text;        // 인식 텍스트
    double confidence = 0.0; // 매칭 단어 평균 conf
    double t_start_ms = 0.0;
    double t_end_ms = 0.0;
};

// libvosk 를 감싸 PCM16(16kHz mono) 스트림을 받아 동의 이벤트를 산출.
class VoskConsentDetector {
public:
    explicit VoskConsentDetector(const ConsentConfig& cfg);
    ~VoskConsentDetector();

    VoskConsentDetector(const VoskConsentDetector&) = delete;
    VoskConsentDetector& operator=(const VoskConsentDetector&) = delete;

    bool ok() const { return rec_ != nullptr; }
    const std::string& error() const { return err_; }

    // 16kHz PCM16 mono 청크 입력. 발화 종료 시 완성 이벤트를 events에 append.
    void accept(const int16_t* samples, size_t n);

    // 스트림 종료 시 마지막 부분을 마감.
    void finish();

    const std::vector<ConsentEvent>& events() const { return events_; }

private:
    void handle_result(const char* json, bool is_final);

    ConsentConfig cfg_;
    std::string grammar_json_;
    std::string err_;
    void* model_ = nullptr; // VoskModel*
    void* rec_ = nullptr;   // VoskRecognizer*
    double consumed_ms_ = 0.0;
    std::vector<ConsentEvent> events_;
};

} // namespace asr
