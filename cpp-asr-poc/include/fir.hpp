// 공유 FIR 설계 헬퍼.
#pragma once
#include <vector>

namespace asr {

// windowed-sinc 저역통과 FIR. fc = 정규화 차단주파수(cycles/sample), num_taps 홀수 권장.
// DC 이득 1로 정규화.
std::vector<double> make_lowpass_fir(double fc, int num_taps);

} // namespace asr
