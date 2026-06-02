#!/usr/bin/env bash
# 로컬 end-to-end 데모: asr_pipeline(서버) 띄우고 asr_feed(클라이언트)로 PCMU 흘려보내
# 동의/전사 결과를 출력. 모델 필요(setup.sh 먼저).
#
# 사용:
#   ./scripts/run_demo.sh [입력.pcmu]
#   입력 생략 시 합성 톤(발화 대용)으로 파이프라인 배관만 점검(전사는 의미 없음).
#   실제 동의/전사 확인은 한국어 음성 PCMU 를 넣어야 함:
#     ffmpeg -i 녹음.wav -ar 8000 -ac 1 -f mulaw sample.pcmu
#     또는  ./build/wav2pcmu 8k_mono.wav sample.pcmu
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT"

VOSK_MODEL_NAME="${VOSK_MODEL_NAME:-vosk-model-small-ko-0.22}"
WHISPER_SIZE="${WHISPER_SIZE:-small}"
VOSK_MODEL="third_party/models/$VOSK_MODEL_NAME"
WHISPER_MODEL="third_party/whisper.cpp/models/ggml-$WHISPER_SIZE.bin"
SOCK="/tmp/asr_demo.sock"

[ -x build/asr_pipeline ] || { echo "build/asr_pipeline 없음 → ./scripts/build.sh 먼저 (libvosk+whisper 필요)"; exit 1; }
[ -d "$VOSK_MODEL" ] || { echo "Vosk 모델 없음: $VOSK_MODEL → ./scripts/setup.sh"; exit 1; }
[ -f "$WHISPER_MODEL" ] || { echo "whisper 모델 없음: $WHISPER_MODEL → ./scripts/setup.sh"; exit 1; }

INPUT="${1:-}"
if [ -z "$INPUT" ]; then
  echo "[demo] 입력 생략 → 합성 톤 PCMU 생성(배관 점검용, 전사 결과는 무의미)"
  ./build/gen_test_audio /tmp/demo_tone.wav 8000 >/dev/null
  ./build/wav2pcmu /tmp/demo_tone.wav /tmp/demo_tone.pcmu >/dev/null
  INPUT=/tmp/demo_tone.pcmu
fi
echo "[demo] 입력: $INPUT"

LOG="$(mktemp)"
echo "[demo] asr_pipeline 시작 (로그: $LOG)"
./build/asr_pipeline --socket "$SOCK" --model-vosk "$VOSK_MODEL" \
  --model-whisper "$WHISPER_MODEL" --lang ko 2>"$LOG" &
PIPE_PID=$!
trap 'kill -INT $PIPE_PID 2>/dev/null || true; wait $PIPE_PID 2>/dev/null || true' EXIT

# listen 대기
for _ in $(seq 1 100); do grep -q "listening on" "$LOG" 2>/dev/null && break; sleep 0.1; done

echo "[demo] ---- 결과 프레임 ----"
./build/asr_feed --socket "$SOCK" --input "$INPUT" --realtime
echo "[demo] ----------------------"

kill -INT $PIPE_PID 2>/dev/null || true
wait $PIPE_PID 2>/dev/null || true
trap - EXIT
echo "[demo] 서버 로그:"; sed 's/^/    /' "$LOG"; rm -f "$LOG"
