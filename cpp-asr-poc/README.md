# cpp-asr-poc — 로컬 ASR 파이프라인 PoC

설계 문서 [`docs/local-asr-cpp-design-review.md`](../docs/local-asr-cpp-design-review.md)의
단계적 도입 로드맵을 코드로 검증하기 위한 PoC. **외부 의존성 없이** 빌드된다.

## 현재 단계: PoC-A (오디오 전단 검증)

목표: "PCMU/PCM 디코드 → 프레임 단위 VAD → 발화 세그먼트 추출"을 엔진 없이 검증.
이 단계가 견고해야 이후 KWS/STT 엔진(PoC-B/C)을 붙일 수 있다.

구성 요소:
- `g711` — G.711 μ-law(PCMU)/a-law(PCMA) → PCM16 디코드 (통화 오디오 1차 디코드)
- `wav_io` — 최소 WAV(PCM16) 읽기/쓰기 (실험·검증용)
- `vad` — 에너지 기반 적응형 VAD + 발화 세그먼터
  - 프레임 에너지(dBFS) + 적응형 노이즈 플로어(EMA)
  - 히스테리시스 임계값 + 최소 발화/묵음 길이로 채터링 방지
  - 세그먼트 앞뒤 패딩
  - 추후 WebRTC VAD / Silero VAD로 교체 가능하도록 단순 인터페이스 유지

## 빌드 & 테스트

```bash
cmake -B build -S .
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## 사용 예

```bash
# 합성 테스트 오디오 생성(묵음/톤/묵음/톤/묵음 → 발화 2개)
./build/gen_test_audio /tmp/test.wav 16000

# WAV 세그먼트 추출
./build/asr_segmenter --input /tmp/test.wav

# 전화망 raw 스트림 (8kHz PCMU)
./build/asr_segmenter --input call.pcmu --format pcmu --rate 8000

# raw PCM16
./build/asr_segmenter --input call.raw --format pcm16 --rate 8000
```

### 주요 옵션 (VAD 튜닝)

| 옵션 | 기본 | 설명 |
|---|---|---|
| `--frame-ms` | 20 | 프레임 길이(10/20/30 권장, WebRTC 호환) |
| `--start-db` | 9.0 | 노이즈 플로어 대비 발화 진입 임계(dB) |
| `--end-db` | 6.0 | 발화 이탈 임계(dB, start보다 낮게=히스테리시스) |
| `--min-speech-ms` | 150 | 발화 확정에 필요한 연속 음성 길이 |
| `--min-silence-ms` | 300 | 세그먼트 종료에 필요한 연속 묵음 길이 |
| `--pad-ms` | 100 | 세그먼트 앞뒤 패딩 |

출력은 JSON 라인(세그먼트별 start/end/dur + 요약)이라 후처리/파이프 연동이 쉽다.

## 검증된 동작 (이 환경)

- 단위 테스트 통과: G.711 디코드 sanity, 세그먼트 카운트(2), 순수 무음(0)
- 16kHz / 8kHz WAV, 8kHz PCMU raw 모두에서 발화 2개를 동일하게 추출

## 다음 단계 (미구현)

- **PoC-B**: 발화 세그먼트 → KWS/grammar 제약 디코딩으로 "네/오케이" 동의 감지
  (sherpa-onnx KWS 또는 Vosk 후보) — 모델 다운로드 필요(네트워크 정책 의존)
- **PoC-C**: whisper.cpp를 보조 전사 경로로 병행, 8k→16k anti-alias 리샘플 추가
- **운영화**: UDS 프레이밍(길이 prefix) + 백프레셔 링버퍼 + 워치독

> 참고: 본 디렉토리는 저장소의 Node.js(OpenAI Realtime) 앱과 독립적인 별도 스택의 PoC다.
