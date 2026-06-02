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

## PoC-B: Vosk 그래머 제약 동의 감지

엔진 선정 결과 **Vosk(Kaldi 기반)** 를 1차 채택했다. 근거:
- 한국어 모델이 공식 제공되어 즉시 착수 가능(sherpa-onnx KWS는 한국어 사전학습 모델이 없어 직접 학습 필요).
- **그래머 제약 디코딩**(`vosk_recognizer_new_grm`)으로 탐색공간을 동의 구문으로 한정
  → **오탐(FP)↓, 지연↓**. 동의 오탐이 요금 분쟁과 직결되는 이 시나리오에 최적.

구성 요소:
- `resample` — 8kHz(통화) → 16kHz **anti-alias 업샘플**(windowed-sinc FIR, 폴리페이즈)
- `consent_match` — 동의 구문 → 그래머 JSON 생성 + 텍스트 매칭(libvosk 비의존, 단위 테스트됨)
- `asr_vosk` — libvosk 래퍼: 그래머 인식기 + 결과 JSON 파싱 + 동의 이벤트 산출
- `asr_consent` — PoC-B CLI: 디코드 → 16k 리샘플 → Vosk 그래머 인식 → 동의 JSON

### 의존성 배치 (third_party)

대용량 바이너리/모델은 저장소에 커밋하지 않는다(.gitignore). 직접 작성한 `vosk_api.h`만 추적.

```
third_party/vosk/
├── vosk_api.h     # (커밋됨) 사용 API 선언, vosk 0.3.45 기준
└── libvosk.so     # (커밋 제외) 아래 방법으로 확보
```

**libvosk.so 확보** — PyPI 휠에서 추출(헤더는 위에 이미 있음):
```bash
cd third_party && pip download vosk --no-deps -d /tmp/voskwhl
unzip -o /tmp/voskwhl/vosk-*.whl -d /tmp/voskx
cp /tmp/voskx/vosk/libvosk.so vosk/
```

**한국어 모델 확보** — `vosk-model-small-ko-0.22` (alphacephei.com):
```bash
# 예: 모델을 third_party/models/ 아래에 배치
mkdir -p third_party/models && cd third_party/models
curl -LO https://alphacephei.com/vosk/models/vosk-model-small-ko-0.22.zip
unzip vosk-model-small-ko-0.22.zip
```

> ⚠️ **네트워크 정책 주의**: 일부 실행 환경은 `alphacephei.com` / `huggingface.co` 를
> 차단(`host_not_allowed`)한다. 이 경우 모델을 받을 수 없으므로, 해당 호스트를 환경
> allowlist에 추가하거나 모델 디렉토리를 수동으로 배치해야 한다. (libvosk 는 PyPI에서
> 받으므로 보통 영향 없음.)

### 빌드 & 실행 (PoC-B)

`libvosk.so` 가 있으면 `asr_consent` 타겟이 자동 빌드된다(없으면 경고 후 스킵).

```bash
cmake -B build -S . && cmake --build build -j
# 통화 raw(8kHz PCMU) → 동의 감지
./build/asr_consent --model third_party/models/vosk-model-small-ko-0.22 \
                    --input call.pcmu --format pcmu --rate 8000
# WAV
./build/asr_consent --model <ko-model-dir> --input call.wav --min-conf 0.6
```

출력(예시):
```
{"event":0,"consent":true,"text":"네","confidence":0.92,"start_ms":900,"end_ms":1100}
{"consent_detected":true,"events":1}
```

동의 구문 목록과 임계값은 `ConsentConfig`(`include/asr_vosk.hpp`)에서 조정한다.

### 현재 검증 상태

- 빌드/링크: `asr_consent` 가 libvosk 와 정상 링크되고 실행 시 Vosk 런타임을 호출함(확인).
- 단위 테스트: 리샘플러(8k→16k 길이 2배·주파수 보존·진폭 유지) + 동의 매칭/그래머 통과.
- **end-to-end 미검증**: 한국어 모델 호스트가 본 환경의 네트워크 정책에 차단되어 실제
  음성→동의 감지 실행은 모델 확보 후 가능.

## PoC-C: whisper.cpp 보조 전사

역할 분리: **실시간 동의 판단은 PoC-B(Vosk)** 가 담당하고, whisper 는 **오프 경로**에서
통화 전체 전사를 만들어 로그/감사/분쟁 대비에 쓴다(실시간 판단엔 미관여).

구성 요소:
- `asr_whisper` — whisper.cpp 래퍼: 16k PCM16 → float32 변환 → `whisper_full` → 세그먼트 텍스트
- `asr_transcribe` — PoC-C CLI: 디코드 → 16k 리샘플 → whisper 전사 → JSON

설계 반영:
- `no_context=true` 로 세그먼트 간 문맥 이월을 끊어 환각을 억제.
- `initial_prompt` 는 **기본 비움**(바이어싱은 무음/짧은 발화에서 환각·반복 유발 위험).
  실험 시에만 `--prompt` 로 주입.

### 의존성 배치 & 빌드

whisper.cpp 소스가 있으면 `asr_transcribe` 타겟이 자동 빌드된다(없으면 경고 후 스킵).

```bash
git clone https://github.com/ggml-org/whisper.cpp third_party/whisper.cpp   # (커밋 제외)
cmake -B build -S . && cmake --build build -j
```

**모델(ggml-*.bin) 확보** — whisper.cpp 의 `models/download-ggml.sh` 또는 huggingface:
```bash
# 예) base/small 한국어 실시간 가능 모델
third_party/whisper.cpp/models/download-ggml.sh small
```
> ⚠️ whisper ggml 모델 호스트(`huggingface.co`, `ggml.ggerganov.com`)도 네트워크 정책에
> 차단될 수 있다(본 환경에서 확인됨). 차단 시 해당 호스트 allowlist 추가 또는 수동 배치 필요.

### 실행

```bash
./build/asr_transcribe --model third_party/whisper.cpp/models/ggml-small.bin \
                       --input call.pcmu --format pcmu --rate 8000 --lang ko
```

### 현재 검증 상태

- 빌드/링크: whisper+ggml 정적 빌드 후 `asr_transcribe` 가 libwhisper 와 링크되고
  실행 시 whisper 런타임을 호출함(확인). 모델 부재 시 graceful 에러.
- **end-to-end 미검증**: ggml 모델 호스트가 네트워크 정책에 차단되어 실제 전사는 모델 확보 후.

## 운영화 골격 + 하이브리드 통합

설계 문서의 프로세스 분리/IPC/이중 경로를 코드로 묶었다. (모델 비의존 부분은 모두 단위 테스트됨)

구성 요소:
- `ring_buffer` — 고정 크기 오디오 링버퍼 + **백프레셔(drop-oldest)** + 메트릭(pushed/dropped/max_depth)
- `frame_io` — **read_fully**(partial read 재조립) + **길이 prefix 프레이밍**(`[u32 len][payload]`)
- `uds_server` — Unix Domain Socket bind/listen/accept + 스테일 소켓 정리
- `hybrid` — 오케스트레이터: 모든 청크 → 실시간 동의(Vosk), 닫힌 세그먼트 → 보조 전사(whisper).
  엔진을 `std::function` 으로 주입(페이크로 테스트 가능)
- `asr_pipeline` — 실엔진 결선 CLI: UDS 수신 스레드 → 링버퍼 → 처리 스레드(HybridPipeline) →
  동의/전사/상태를 프레임으로 회신 (Vosk + whisper 둘 다 있을 때만 빌드)

데이터 흐름:
```
[메인 백엔드] ──(16k PCM16 raw)──> UDS ──> 수신스레드 → 링버퍼(백프레셔)
                                                │
                                  처리스레드  HybridPipeline
                                    ├─ Vosk    : 실시간 동의 → {"type":"consent",...}
                                    └─ whisper : 세그먼트 전사 → {"type":"transcript",...}
                          ← 결과는 [u32 len][JSON] 프레임으로 동일 소켓 회신
```

실행(모델 필요):
```bash
./build/asr_pipeline --socket /tmp/asr.sock \
                     --model-vosk    third_party/models/vosk-model-small-ko-0.22 \
                     --model-whisper third_party/whisper.cpp/models/ggml-small.bin --lang ko
# 메인 백엔드(클라이언트)는 /tmp/asr.sock 에 16kHz PCM16 raw 스트림을 흘리고,
# 같은 소켓에서 길이 prefix JSON 프레임(consent/transcript/status)을 읽는다.
```
> 입력은 16kHz PCM16 mono 전제. 8kHz 통화는 업스트림 또는 PoC-B/-C 경로의 리샘플로 변환.

### 검증 상태 (운영화)

- 단위 테스트 통과: 링버퍼 drop-oldest·메트릭, 프레이밍 왕복(부분 read 재조립),
  **실제 UDS bind/connect/오디오 송신/프레임 회신**, 하이브리드 라우팅(동의 전량 공급 + 세그먼트 전사).
- `asr_pipeline` 은 libvosk + libwhisper 와 정상 링크(컴파일·링크 검증). 실행 e2e 는 모델 확보 후.

## 송신측 메커니즘 (PCMU → UDS)

메인 백엔드가 전화망 **8kHz G.711 μ-law(PCMU)** 스트림을 AI 추론 프로세스로 밀어넣는 부분.

설계 결정:
- **클라이언트가 디코드+리샘플 담당** → 서버는 항상 16kHz PCM16 단일 포맷만 처리(균일).
  (원 설계 문서의 "Main Process: PCMU 디코딩 후 16kHz PCM 추출" 의도와 일치)
- 와이어 포맷: 오디오 IN = **raw PCM16LE 연속 스트림**(프레이밍 없음), 결과 OUT = **길이 prefix 프레임**.
- **스트리밍(상태 유지) 리샘플러**: FIR 딜레이라인을 청크 간 유지 → 20ms 패킷 연속 입력에도
  경계 아티팩트 없음(블록 처리와 MAE≈0 확인).
- **백프레셔**: 전화망 콜백은 절대 블로킹하지 않음(디코드 후 링버퍼에 push, 가득 차면 drop-oldest).
- **SIGPIPE 안전**: 프레이밍 송신은 `send(MSG_NOSIGNAL)`; 스레드 종료는 `shutdown(SHUT_RDWR)`로
  블로킹 read 를 깨움.
- **연결 복원력**: 초기 연결은 지수 백오프 재시도. (스트림 중 재연결/폴백은 운영화 항목 참고)

구성 요소:
- `streaming_resampler` — 8k→16k 상태 유지 업샘플(x2, anti-alias FIR)
- `uds_client` — 연결(백오프 재시도) + raw PCM16 송신 + 결과 프레임 수신
- `pcmu_sender` — `feed_pcmu()`(비블로킹) + 송신/수신 스레드 + 백프레셔 링버퍼
- `asr_feed` — 송신 데모 CLI: PCMU 파일을 실시간 페이스로 스트리밍

스레드 모델:
```
전화망 콜백 ─feed_pcmu()→ [μ-law 디코드] → 링버퍼(8k, drop-oldest)
                                              │
                            [송신 스레드] 스트리밍 리샘플(8k→16k) → UDS write_fully
                            [수신 스레드] UDS read_frame → on_result 콜백(consent/transcript/status)
```

실행:
```bash
# 서버(asr_pipeline)가 /tmp/asr.sock 에서 대기 중일 때
./build/asr_feed --socket /tmp/asr.sock --input call.pcmu --realtime
```

검증(이 환경):
- 스트리밍 리샘플러 연속성: 블록 처리 대비 **MAE=0.0**.
- `asr_feed` end-to-end: 15200 PCMU 바이트 → 서버가 **30400개 16k 샘플**(정확히 2배) 수신,
  결과 프레임 정상 수신, 링버퍼 드롭 0.
- 단위 테스트 `pcmu_sender`: 리샘플 연속성 + 송신→서버 바이트 경로 + 결과 프레임 수신.

## 다음 단계

- 한국어 모델(Vosk/whisper) 확보 후 **end-to-end + FP/FN·지연 측정**
- 전처리(AGC/NS) on/off A·B 측정
- 스트림 중 **재연결**(generation 카운터로 송/수신 스레드 안전 교체) + AI 다운 시
  graceful degradation(상담원 수동 확인 폴백) + 워치독/헬스체크

> 참고: 본 디렉토리는 저장소의 Node.js(OpenAI Realtime) 앱과 독립적인 별도 스택의 PoC다.
