# STT 보정/평가/통계 파이프라인 구현 계획

작성일: 2026-05-13

## 목표

이미 녹취된 전화 품질 파일을 대상으로 다음 전체 흐름을 만든다.

```text
녹취 파일 수집
-> 파일/환경 메타 분석
-> 전처리 후보 테스트
-> STT
-> AI 보정
-> 상담 품질 평가
-> 고객 반응/만족도 추정
-> 통계/리포트
```

우선순위는 현재 녹취 파일에서 얻을 수 있는 최선의 전사 품질을 찾는 것이다. 스테레오 녹취, 코덱 변경, 녹취 시스템 변경은 별도 후속 단계로 유지한다.

## 조사 요약

### 1. 고객 반응 기반 만족도 추정

콜센터 speech analytics 제품/방법론은 통화 녹취를 STT로 변환한 뒤 sentiment, emotion, tone, keywords, phrases, behavioral trends를 분석해 고객 경험과 상담 품질을 추정한다. 다만 AI 추정 만족도는 실제 CSAT 설문을 대체하기보다, 설문 미응답 통화의 위험 신호와 코칭 대상을 찾는 보조 지표로 쓰는 것이 안전하다.

추천 지표:

- 고객 감정 변화: 시작/중간/종료의 긍정, 중립, 부정 변화
- 고객 불만 신호: 반복 질문, 부정 표현, 강한 반박, 침묵, 말 끊김
- 고객 노력도 추정: 같은 설명 반복, 이전 안내 재확인, 해결 절차 복잡도
- 해결 수용 신호: "알겠습니다", "네", "확인해볼게요", "감사합니다" 등
- 미해결/재문의 위험: "어디에 전화해요?", "어떻게 해야 돼요?", "다시 확인" 등
- 종료 감정: 통화 종료 직전 고객 반응이 초반 감정보다 CSAT 추정에 더 중요할 수 있음

### 2. 상담 품질 평가 방법론

기본은 weighted QA scorecard 방식으로 간다. COPC CX Standard류의 운영 품질 체계와 콜센터 QA scorecard 방법론은 고객 경험, 프로세스 준수, 컴플라이언스의 균형을 강조한다.

추천 기본 배점:

| 영역 | 배점 | 평가 내용 |
| --- | ---: | --- |
| 시작/확인 | 10 | 인사, 상담 목적 확인, 필요한 본인/업무 확인 |
| 문제 파악 | 20 | 고객 요청 재진술, 추가 질문, 핵심 맥락 파악 |
| 해결 정확도 | 30 | 정확한 안내, 가능/불가 사유, 다음 액션 제시 |
| 커뮤니케이션 | 20 | 경청, 공감, 쉬운 설명, 말 끊김/혼선 최소화 |
| 준수/리스크 | 10 | 개인정보, 필수 고지, 금지 표현, 업무 규정 준수 |
| 마무리 | 10 | 요약, 추가 문의 확인, 종료 인사 |

자동 평가는 먼저 AI가 초안을 만들고, 사람이 보정 가능한 구조로 둔다. 초기에는 점수보다 "근거 문장"을 반드시 함께 남기는 것이 중요하다.

### 3. 보정 품질 판단

정답 전사본이 없는 상태에서는 AI 보정 품질을 완전 자동 판정하기 어렵다. 보정 AI 비교는 다음 기준으로 한다.

- 의미 보존: 없는 정보 추가 금지
- 핵심 업무 용어 정확도: 이월, 취소 처리, 등록, 학기, 교육 통합 등
- 수치/날짜/상태 왜곡 여부
- 누락 여부: 보정 전 문장 중 사라진 문장이 없는지
- 가독성: 문장부호, 띄어쓰기, 화자 흐름
- 환각 위험: 자연스럽지만 원문에 없는 설명 추가 여부

정량 평가가 필요하면 대표 샘플 10~30개를 사람이 정답 전사하고 CER/WER, 키워드 정확도, 의미 왜곡 건수를 함께 측정한다.

## 전체 시스템 설계

### A. 단건 테스트 모드

현재 브라우저 앱을 유지한다.

```text
파일 선택
-> 전처리 옵션 선택
-> STT 실행
-> 전사 결과 표시
-> AI 보정 수동 실행
-> 상담 품질 평가 수동 실행
-> 결과 저장
```

용도:

- 옵션 A/B 테스트
- 보정 AI 비교
- 평가 프롬프트 검증
- 사람이 결과를 즉시 확인

### B. 녹취 파일 배치 모드

대량 파일을 처리하는 별도 작업 큐를 둔다.

```text
폴더 선택/등록
-> 파일 목록 스캔
-> 파일 메타 분석
-> 샘플링 테스트
-> 추천 파이프라인 선택
-> 전체 배치 실행
-> 결과 DB/JSON 저장
-> 통계 대시보드
```

배치 실행 전에는 예상 AI 사용 비용을 먼저 계산하고, 실행 후에는 실제 사용량 기준 비용을 저장한다.

파일 단위 상태:

```text
queued
-> probing
-> preprocessing
-> transcribing
-> correcting
-> evaluating
-> completed
-> failed
```

### C. 자동 추천 흐름

녹취 환경별로 전처리와 보정 AI를 추천하려면 전체 파일을 바로 돌리지 않고, 대표 샘플을 먼저 테스트한다.

```text
녹취 묶음 등록
-> 대표 샘플 3~10개 추출
-> 전처리 후보 실행
-> STT 후보 실행
-> 보정 AI 후보 실행
-> 품질 지표 계산
-> 추천 설정 생성
-> 운영자가 승인
-> 전체 배치 실행
```

추천 후보:

- 전처리: none, loudness normalize, high-pass/low-pass, silence trim
- Realtime noise reduction: off, near_field, far_field
- STT 방식: Realtime, file transcription API
- 보정 AI: Claude, Gemini
- 보정 프롬프트: 보수적 보정, 가독성 보정, 도메인 용어 강화

## AI 사용 비용 산정

작성 기준일: 2026-05-13

공식 단가는 수시로 바뀔 수 있으므로 모델 단가는 설정/DB에서 관리하고, 계산식과 사용량 기록 구조는 코드에 고정한다. 화면에는 USD 기준 비용과 함께 `환율` 설정값을 곱한 원화 예상 비용을 표시한다.

### 1. 단계별 과금 단위

| 단계 | AI 사용 | 기본 과금 기준 | 기록해야 할 사용량 |
| --- | --- | --- | --- |
| 파일 메타 분석 | 없음 | 로컬 `ffprobe`/파일 분석 | 비용 없음 |
| 전처리 | 없음 | 로컬 `ffmpeg` | 비용 없음 |
| STT - Realtime | OpenAI `gpt-realtime-whisper` | 오디오 분당 과금 | `audioDurationSec`, `model`, `sttCostUsd` |
| STT - 파일 전사 | OpenAI `gpt-4o-transcribe` 또는 `gpt-4o-mini-transcribe` | 오디오 분당 예상 과금 | `audioDurationSec`, `model`, `sttCostUsd` |
| AI 보정 | Gemini/Claude | 입력/출력 토큰 과금 | `inputTokens`, `outputTokens`, `correctionCostUsd` |
| 상담 품질 평가 | Gemini/Claude 또는 추후 OpenAI 텍스트 모델 | 입력/출력 토큰 과금 | `inputTokens`, `outputTokens`, `evaluationCostUsd` |
| 통계/리포트 집계 | 보통 없음 | DB 집계 | 비용 없음 |
| 요약 리포트 생성 | 선택 AI | 입력/출력 토큰 과금 | `inputTokens`, `outputTokens`, `reportCostUsd` |

### 2. 현재 후보 모델 단가

2026-05-13 공식 가격 페이지 확인 기준의 기본 단가다. 운영 적용 시에는 코드 상수로 묻지 말고 `pricing_config` 같은 설정 테이블/JSON으로 분리한다.

| 용도 | 모델 | 표준 단가 | 배치 단가 |
| --- | --- | ---: | ---: |
| 실시간 STT | OpenAI `gpt-realtime-whisper` | `$0.017 / min` | 별도 확인 필요 |
| 파일 STT | OpenAI `gpt-4o-transcribe` | 약 `$0.006 / min` | 별도 확인 필요 |
| 저비용 파일 STT | OpenAI `gpt-4o-mini-transcribe` | 약 `$0.003 / min` | 별도 확인 필요 |
| 보정/평가 | Gemini `gemini-2.5-flash` | 입력 `$0.30 / 1M tokens`, 출력 `$2.50 / 1M tokens` | 입력 `$0.15 / 1M tokens`, 출력 `$1.25 / 1M tokens` |
| 저비용 보정/평가 후보 | Gemini `gemini-2.5-flash-lite` | 입력 `$0.10 / 1M tokens`, 출력 `$0.40 / 1M tokens` | 입력 `$0.05 / 1M tokens`, 출력 `$0.20 / 1M tokens` |
| 보정/평가 | Claude Sonnet 4.6 | 입력 `$3.00 / 1M tokens`, 출력 `$15.00 / 1M tokens` | 입력 `$1.50 / 1M tokens`, 출력 `$7.50 / 1M tokens` |

참고: OpenAI Batch API와 Anthropic/Gemini Batch API는 일반적으로 지연 허용 작업에 맞고, 현재 브라우저 단건 Realtime 전사에는 맞지 않는다. 배치 모드에서도 오디오 STT가 Batch API 대상인지 모델별로 별도 확인해야 하므로, 초기에는 STT는 표준 단가로 보수적으로 계산한다.

### 3. 계산식

```text
audio_minutes = ceil(audioDurationSec / 60 * 100) / 100

stt_cost_usd = audio_minutes * stt_price_per_min

token_cost_usd =
  (input_tokens / 1_000_000 * input_price_per_1m) +
  (output_tokens / 1_000_000 * output_price_per_1m)

file_total_cost_usd =
  stt_cost_usd +
  correction_cost_usd +
  evaluation_cost_usd +
  optional_report_cost_usd

batch_total_cost_usd = sum(file_total_cost_usd)
batch_total_krw = batch_total_cost_usd * exchange_rate_krw_per_usd
```

토큰 수를 API 응답에서 받을 수 있으면 실제값을 저장한다. 응답에서 usage가 없거나 제공자별 형식이 다르면 초기 예상값은 `estimatedInputTokens`, `estimatedOutputTokens`로 저장하고, 실제 사용량 확보 후 `actualInputTokens`, `actualOutputTokens`를 채운다.

### 4. 초기 예상값 산정 규칙

정확한 비용은 실제 토큰 사용량으로만 확정할 수 있다. 배치 실행 전 예상은 다음 보수적 기본값으로 시작한다.

| 항목 | 기본 예상 |
| --- | ---: |
| 평균 통화 길이 | 운영자가 입력. 미입력 시 3분 |
| STT 결과 텍스트 입력 토큰 | 통화 1분당 450 tokens |
| 보정 출력 토큰 | 입력의 90% |
| 평가 입력 토큰 | 보정 결과 + 평가 프롬프트, 통화 1분당 550 tokens |
| 평가 출력 토큰 | 파일당 700 tokens |
| 리포트 요약 입력 토큰 | 평가 결과 전체 또는 집계 샘플 기준 |
| 리포트 요약 출력 토큰 | 리포트 1건당 1,500 tokens |

### 5. 파일 1건 예상 예시

가정:

- 통화 길이: 3분
- STT: `gpt-4o-mini-transcribe`
- 보정: `gemini-2.5-flash`
- 평가: `gemini-2.5-flash`
- 환율: `1 USD = 1,400 KRW`

```text
STT = 3 min * $0.003 = $0.0090

보정 입력 = 3 * 450 = 1,350 tokens
보정 출력 = 1,350 * 0.9 = 1,215 tokens
보정 비용 = 1,350/1M*$0.30 + 1,215/1M*$2.50 = 약 $0.0034

평가 입력 = 3 * 550 = 1,650 tokens
평가 출력 = 700 tokens
평가 비용 = 1,650/1M*$0.30 + 700/1M*$2.50 = 약 $0.0022

파일 1건 합계 = 약 $0.0146 = 약 20.4원
```

Realtime STT를 쓰면 같은 3분 파일의 STT 비용만 `3 * $0.017 = $0.051`이므로 파일 1건 합계가 약 `$0.0566`, 약 79.2원으로 올라간다.

### 6. 배치 예상 예시

가정:

- 파일 수: 1,000건
- 평균 통화 길이: 3분
- 보정/평가: `gemini-2.5-flash`
- STT는 표준 단가
- 원화 환산: `1 USD = 1,400 KRW`

| 시나리오 | 파일당 예상 | 1,000건 예상 | 원화 예상 |
| --- | ---: | ---: | ---: |
| 저비용 파일 STT + Gemini 보정/평가 | `$0.0146` | `$14.60` | 약 20,440원 |
| 고품질 파일 STT + Gemini 보정/평가 | `$0.0236` | `$23.60` | 약 33,040원 |
| Realtime STT + Gemini 보정/평가 | `$0.0566` | `$56.60` | 약 79,240원 |
| 저비용 파일 STT + Claude Sonnet 보정/평가 | 약 `$0.0467` | 약 `$46.70` | 약 65,380원 |

배치 API를 보정/평가 단계에 적용하면 Gemini/Claude 토큰 비용은 대략 50% 낮아진다. 다만 STT 비용 비중이 큰 시나리오에서는 전체 절감률이 50%까지 내려가지는 않는다.

### 7. 단가 출처

- OpenAI API Pricing: `gpt-realtime-whisper`, `gpt-4o-transcribe`, `gpt-4o-mini-transcribe`, Batch API 50% 안내
- Google Gemini API Pricing: `gemini-2.5-flash`, `gemini-2.5-flash-lite`, Batch 가격
- Anthropic Claude API Pricing: Claude Sonnet 4.6 표준/Batch 가격

### 8. 구현 요구사항

1. 결과 JSON/DB에 단계별 비용 필드를 저장한다.
2. 배치 실행 전 `파일 수`, `총 오디오 길이`, `모델 조합`, `환율`, `표준/배치 처리 여부`로 예상 비용을 보여준다.
3. 배치 실행 중에는 `estimatedCostUsd`, `actualCostUsd`, `remainingEstimatedCostUsd`를 계속 갱신한다.
4. 파일별 실패/재시도는 재실행 비용이 생길 수 있으므로 `attemptCostUsd`와 `totalCostUsd`를 분리한다.
5. 샘플링 추천 단계는 후보 조합을 여러 번 실행하므로, 전체 배치 전 비용과 별도로 `samplingCostUsd`를 표시한다.
6. 단가 변경에 대비해 `pricingSource`, `pricingCheckedAt`, `pricingVersion`을 함께 저장한다.

## 구현 단계

### Phase 1. 현재 단건 앱 안정화

목표: 단일 파일을 안정적으로 끝까지 처리하고 결과를 저장한다.

작업:

1. 전사 결과 저장 버튼 추가
2. AI 보정 버튼 수동 실행 방식으로 변경
3. Claude/Gemini 보정 결과를 나란히 비교
4. 전사/보정 결과 JSON 다운로드
5. Realtime prompt는 기본 비활성 유지
6. noise reduction 기본값은 Off 유지, 사용자가 켤 수 있게 유지

검증:

- 동일 파일 1개를 끝까지 전사
- Claude/Gemini 보정 각각 실행
- 결과 JSON에 원본 파일명, 옵션, 전사, 보정, 이벤트 로그 포함

### Phase 2. 보정 품질 비교

목표: AI 보정이 실제로 좋아졌는지 사람이 판단 가능한 구조를 만든다.

작업:

1. 보정 전/후 diff 표시
2. 핵심 키워드 유지 여부 자동 표시
3. 문장 누락 의심 표시
4. 보정 AI별 결과 탭 추가
5. 사람이 "좋음/보통/나쁨" 판정 입력

검증:

- 같은 전사 결과에 Claude/Gemini 보정 실행
- 키워드 보존 리포트 생성
- 사람이 최종 선호 AI 선택 가능

### Phase 3. 상담 품질 평가 MVP

목표: 전사 또는 보정 결과로 상담 품질 평가 초안을 만든다.

작업:

1. QA scorecard JSON 스키마 정의
2. 평가 API 추가
3. 평가 결과: 점수, 근거 문장, 개선 제안
4. 고객 반응/만족도 추정 항목 추가
5. 평가 결과 다운로드

초기 평가 항목:

- 문제 파악
- 해결 정확도
- 설명 명확성
- 공감/태도
- 고객 불만 신호
- 종료 시 고객 수용도
- 재문의 위험

검증:

- 같은 통화에 대해 보정 전/후 평가 차이 비교
- 각 점수에 근거 문장이 있는지 확인
- 평가 API 응답에 토큰 사용량과 단계별 비용이 기록되는지 확인

### Phase 4. 녹취 파일 기반 배치 처리

목표: 폴더 단위 대량 처리 기반을 만든다.

작업:

1. 서버에 batch job 모델 추가
2. `recordings/` 또는 지정 폴더 스캔
3. 파일별 상태 저장
4. 동시 처리 수 제한
5. 실패 재시도
6. 결과 JSONL/SQLite 저장
7. 배치 진행률 화면 추가
8. 배치 예상 비용/실제 비용 집계 추가

검증:

- 샘플 5개 처리
- 실패 파일과 성공 파일 분리
- 재실행 시 completed 파일 skip
- 실행 전 예상 비용과 실행 후 실제 비용 차이 표시

### Phase 5. 전처리 추천

목표: 녹취 환경별 최적 옵션을 자동 추천한다.

작업:

1. ffmpeg 기반 파일 probe
2. duration, bitrate, sample rate, channel count 수집
3. 대표 샘플 자동 선택
4. 전처리 후보별 STT 실행
5. 키워드 검출률, 텍스트 길이, 누락 의심, AI 평가 점수 비교
6. 추천 옵션 저장
7. 후보 조합별 샘플링 비용 비교

주의:

- 정답 전사본이 없으면 추천은 휴리스틱이다.
- 대표 샘플에 대해 사람 검토 1회가 필요하다.
- 후보 조합 수가 늘어나면 샘플링 비용이 `샘플 파일 수 * 전처리 후보 수 * STT 후보 수 * 보정 AI 후보 수`에 비례해 증가한다.

### Phase 6. 통계/리포트

목표: 상담 품질과 고객 반응을 집계한다.

통계:

- 상담원별 평균 QA 점수
- 고객 부정 반응 비율
- 재문의 위험 비율
- 해결 정확도 낮은 문의 유형
- 키워드/이슈 트렌드
- 보정 AI별 선호/품질 결과
- 전처리 옵션별 STT 품질 비교

출력:

- 화면 대시보드
- CSV/JSON 다운로드
- 기간/상담원/업체/문의유형 필터

## 추천 구현 순서

가장 쉽고 효과가 큰 순서:

1. Phase 1: 단건 결과 저장 + 보정 수동 실행
2. Phase 2: Claude/Gemini 보정 비교 UI
3. Phase 3: QA scorecard 평가 MVP
4. Phase 4: 배치 처리 기본 큐
5. Phase 5: 전처리 추천 자동화
6. Phase 6: 통계 대시보드

## 당장 다음 작업

다음 구현은 Phase 1로 제한한다.

구체 작업:

1. 전사 완료 후 자동 보정 실행을 끄고, `보정 실행` 버튼으로 변경
2. Claude/Gemini를 수동으로 선택해서 여러 번 보정 가능하게 변경
3. 전사/보정 결과 JSON 저장 버튼 추가
4. 현재 실행 옵션을 결과에 포함

이 단계를 먼저 끝내야 이후 평가/배치/통계가 안정적으로 쌓인다.
