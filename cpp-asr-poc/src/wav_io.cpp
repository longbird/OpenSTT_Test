#include "wav_io.hpp"
#include <cstring>
#include <fstream>

namespace asr {

namespace {
uint32_t rd_u32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}
uint16_t rd_u16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
void wr_u32(std::ofstream& f, uint32_t v) {
    unsigned char b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    f.write(reinterpret_cast<char*>(b), 4);
}
void wr_u16(std::ofstream& f, uint16_t v) {
    unsigned char b[2] = {uint8_t(v), uint8_t(v >> 8)};
    f.write(reinterpret_cast<char*>(b), 2);
}
} // namespace

bool read_wav_pcm16(const std::string& path, WavData& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "open failed: " + path; return false; }
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    if (buf.size() < 44 || std::memcmp(buf.data(), "RIFF", 4) != 0 ||
        std::memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        err = "not a RIFF/WAVE file"; return false;
    }

    int channels = 0, sample_rate = 0, bits = 0, fmt = 0;
    size_t data_off = 0, data_len = 0;
    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const unsigned char* ck = buf.data() + pos;
        uint32_t ck_size = rd_u32(ck + 4);
        const unsigned char* body = ck + 8;
        if (std::memcmp(ck, "fmt ", 4) == 0 && ck_size >= 16) {
            fmt = rd_u16(body);
            channels = rd_u16(body + 2);
            sample_rate = static_cast<int>(rd_u32(body + 4));
            bits = rd_u16(body + 14);
        } else if (std::memcmp(ck, "data", 4) == 0) {
            data_off = pos + 8;
            data_len = ck_size;
        }
        pos += 8 + ck_size + (ck_size & 1); // 청크는 2바이트 정렬
    }

    if (fmt != 1 || bits != 16 || channels < 1) {
        err = "unsupported WAV (need PCM16, fmt=1)"; return false;
    }
    if (data_off == 0 || data_off + data_len > buf.size()) {
        if (data_off != 0) data_len = buf.size() - data_off; // 관대하게 처리
        else { err = "no data chunk"; return false; }
    }

    out.sample_rate = sample_rate;
    out.channels = channels;
    out.samples.clear();
    size_t total = data_len / 2;
    const unsigned char* d = buf.data() + data_off;
    if (channels == 1) {
        out.samples.reserve(total);
        for (size_t i = 0; i < total; ++i)
            out.samples.push_back(static_cast<int16_t>(rd_u16(d + i * 2)));
    } else {
        // 다채널이면 0번 채널만 추출(통화는 mono 전제)
        size_t frames = total / channels;
        out.samples.reserve(frames);
        for (size_t i = 0; i < frames; ++i)
            out.samples.push_back(static_cast<int16_t>(rd_u16(d + (i * channels) * 2)));
        out.channels = 1;
    }
    return true;
}

bool write_wav_pcm16_mono(const std::string& path, int sample_rate,
                          const std::vector<int16_t>& samples, std::string& err) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { err = "open for write failed: " + path; return false; }
    uint32_t data_bytes = static_cast<uint32_t>(samples.size() * 2);
    uint32_t byte_rate = static_cast<uint32_t>(sample_rate) * 2;
    f.write("RIFF", 4);
    wr_u32(f, 36 + data_bytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    wr_u32(f, 16);
    wr_u16(f, 1);                 // PCM
    wr_u16(f, 1);                 // mono
    wr_u32(f, static_cast<uint32_t>(sample_rate));
    wr_u32(f, byte_rate);
    wr_u16(f, 2);                 // block align
    wr_u16(f, 16);                // bits
    f.write("data", 4);
    wr_u32(f, data_bytes);
    f.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
    return static_cast<bool>(f);
}

} // namespace asr
