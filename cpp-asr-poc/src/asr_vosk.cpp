#include "asr_vosk.hpp"
#include "vosk_api.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace asr {

namespace {

// "key":"....." 형태의 문자열 값 추출(간이 파서, 이스케이프 미지원 — Vosk 텍스트엔 충분).
std::string json_str(const std::string& j, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return "";
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    p = j.find('"', p);
    if (p == std::string::npos) return "";
    size_t end = j.find('"', p + 1);
    if (end == std::string::npos) return "";
    return j.substr(p + 1, end - p - 1);
}

// 모든 "conf":<num> 값을 모아 평균. 없으면 has=false.
double avg_conf(const std::string& j, bool& has) {
    double sum = 0.0;
    int cnt = 0;
    size_t p = 0;
    const std::string pat = "\"conf\"";
    while ((p = j.find(pat, p)) != std::string::npos) {
        size_t c = j.find(':', p + pat.size());
        if (c == std::string::npos) break;
        sum += std::strtod(j.c_str() + c + 1, nullptr);
        cnt++;
        p = c + 1;
    }
    has = cnt > 0;
    return cnt > 0 ? sum / cnt : 0.0;
}

// 첫 word의 start, 마지막 word의 end (초). 없으면 -1.
void word_span(const std::string& j, double& start_s, double& end_s) {
    start_s = end_s = -1.0;
    size_t p = j.find("\"start\"");
    if (p != std::string::npos) {
        size_t c = j.find(':', p);
        if (c != std::string::npos) start_s = std::strtod(j.c_str() + c + 1, nullptr);
    }
    size_t last = j.rfind("\"end\"");
    if (last != std::string::npos) {
        size_t c = j.find(':', last);
        if (c != std::string::npos) end_s = std::strtod(j.c_str() + c + 1, nullptr);
    }
}

// 결과 텍스트가 비었는지(공백만인지) 판정용.
bool blank(const std::string& s) {
    for (char c : s) if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
    return true;
}

} // namespace

VoskConsentDetector::VoskConsentDetector(const ConsentConfig& cfg) : cfg_(cfg) {
    vosk_set_log_level(-1);
    grammar_json_ = build_grammar_json(cfg_.phrases);
    model_ = vosk_model_new(cfg_.model_path.c_str());
    if (!model_) {
        err_ = "vosk_model_new failed: " + cfg_.model_path;
        return;
    }
    rec_ = vosk_recognizer_new_grm(static_cast<VoskModel*>(model_),
                                   cfg_.sample_rate, grammar_json_.c_str());
    if (!rec_) {
        err_ = "vosk_recognizer_new_grm failed";
        return;
    }
    vosk_recognizer_set_words(static_cast<VoskRecognizer*>(rec_), 1);
}

VoskConsentDetector::~VoskConsentDetector() {
    if (rec_) vosk_recognizer_free(static_cast<VoskRecognizer*>(rec_));
    if (model_) vosk_model_free(static_cast<VoskModel*>(model_));
}

void VoskConsentDetector::accept(const int16_t* samples, size_t n) {
    if (!rec_) return;
    int r = vosk_recognizer_accept_waveform_s(static_cast<VoskRecognizer*>(rec_),
                                              samples, static_cast<int>(n));
    consumed_ms_ += 1000.0 * static_cast<double>(n) / cfg_.sample_rate;
    if (r == 1) {
        handle_result(vosk_recognizer_result(static_cast<VoskRecognizer*>(rec_)), false);
    }
}

void VoskConsentDetector::finish() {
    if (!rec_) return;
    handle_result(vosk_recognizer_final_result(static_cast<VoskRecognizer*>(rec_)), true);
}

void VoskConsentDetector::handle_result(const char* json, bool /*is_final*/) {
    if (!json) return;
    std::string j(json);
    std::string text = json_str(j, "text");
    if (blank(text)) return;

    bool has_conf = false;
    double conf = avg_conf(j, has_conf);
    double s_s = -1.0, e_s = -1.0;
    word_span(j, s_s, e_s);

    ConsentEvent ev;
    ev.text = text;
    ev.confidence = has_conf ? conf : 1.0;
    ev.t_start_ms = (s_s >= 0) ? s_s * 1000.0 : consumed_ms_;
    ev.t_end_ms = (e_s >= 0) ? e_s * 1000.0 : consumed_ms_;
    ev.consent = text_matches_consent(text, cfg_.phrases) &&
                 ev.confidence >= cfg_.min_confidence;
    events_.push_back(ev);
}

} // namespace asr
