// Vosk C API 선언 (PoC-B에서 사용하는 부분만).
// 공식 헤더는 네트워크 정책상 받을 수 없어, 배포 wheel의 cffi 선언과
// libvosk.so 의 export 심볼을 근거로 동일 시그니처를 직접 선언한다.
// 라이선스: Vosk(Apache-2.0). 심볼/시그니처는 vosk-api 0.3.45 기준.
#ifndef VOSK_API_H
#define VOSK_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VoskModel VoskModel;
typedef struct VoskRecognizer VoskRecognizer;

// 모델 로드/해제
VoskModel *vosk_model_new(const char *model_path);
void vosk_model_free(VoskModel *model);

// 인식기 생성: 일반 / 그래머 제약(grammar = JSON 구문 배열 문자열)
VoskRecognizer *vosk_recognizer_new(VoskModel *model, float sample_rate);
VoskRecognizer *vosk_recognizer_new_grm(VoskModel *model, float sample_rate,
                                        const char *grammar);
void vosk_recognizer_free(VoskRecognizer *recognizer);
void vosk_recognizer_reset(VoskRecognizer *recognizer);

// 단어별 신뢰도(conf) 출력 활성화
void vosk_recognizer_set_words(VoskRecognizer *recognizer, int words);

// PCM16(mono) 입력. _s 변형은 length = 샘플(short) 개수.
// 반환 1: 발화 종료(완성 결과), 0: 부분 결과 누적, -1: 오류
int vosk_recognizer_accept_waveform_s(VoskRecognizer *recognizer,
                                      const short *data, int length);

// JSON 결과 문자열(내부 버퍼 소유, 다음 호출까지 유효)
const char *vosk_recognizer_result(VoskRecognizer *recognizer);
const char *vosk_recognizer_final_result(VoskRecognizer *recognizer);

// 로그 레벨(-1 = 침묵)
void vosk_set_log_level(int level);

#ifdef __cplusplus
}
#endif

#endif // VOSK_API_H
