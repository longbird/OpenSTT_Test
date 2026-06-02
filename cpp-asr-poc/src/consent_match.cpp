#include "consent_match.hpp"
#include <sstream>

namespace asr {

namespace {
std::string normalize_ws(const std::string& s) {
    std::string out;
    bool prev_sp = true;
    for (char ch : s) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (!prev_sp) { out.push_back(' '); prev_sp = true; }
        } else {
            out.push_back(ch);
            prev_sp = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
} // namespace

std::string build_grammar_json(const std::vector<std::string>& phrases) {
    std::ostringstream os;
    os << "[";
    for (const auto& p : phrases) os << "\"" << p << "\", ";
    os << "\"[unk]\"]"; // 동의 외 발화는 [unk]로 흡수
    return os.str();
}

bool text_matches_consent(const std::string& text,
                          const std::vector<std::string>& phrases) {
    std::string t = normalize_ws(text);
    if (t.empty()) return false;

    std::vector<std::string> toks;
    {
        std::istringstream is(t);
        std::string w;
        while (is >> w) toks.push_back(w);
    }
    for (const auto& ph : phrases) {
        std::string p = normalize_ws(ph);
        if (p.empty()) continue;
        if (p.find(' ') == std::string::npos) {
            for (const auto& w : toks) if (w == p) return true; // 단어 정확 매칭
        } else if (t.find(p) != std::string::npos) {
            return true;                                        // 구문 포함
        }
    }
    return false;
}

} // namespace asr
