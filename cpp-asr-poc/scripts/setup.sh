#!/usr/bin/env bash
# 로컬(Ubuntu/WSL2) 의존성 준비: libvosk + whisper.cpp + 한국어/whisper 모델.
# 이 환경과 달리 로컬에서는 모델 호스트 접근이 가능하다는 전제.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT"

VOSK_MODEL_NAME="${VOSK_MODEL_NAME:-vosk-model-small-ko-0.22}"
WHISPER_SIZE="${WHISPER_SIZE:-small}"   # base(빠름) | small(정확) 권장

echo "== [1/4] 빌드 도구 확인 =="
for t in cmake g++ git python3 unzip curl; do
  command -v "$t" >/dev/null 2>&1 || { echo "  '$t' 없음. 설치: sudo apt-get install -y build-essential cmake git python3 unzip curl"; exit 1; }
done
echo "  ok"

echo "== [2/4] libvosk.so (PyPI 휠에서 추출) =="
if [ -f third_party/vosk/libvosk.so ]; then
  echo "  이미 있음: third_party/vosk/libvosk.so"
else
  tmp="$(mktemp -d)"
  url="$(curl -fsSL https://pypi.org/pypi/vosk/json | python3 -c "import sys,json;d=json.load(sys.stdin);print([f['url'] for f in d['urls'] if 'x86_64' in f['filename'] and 'manylinux' in f['filename']][0])")"
  echo "  다운로드: $url"
  curl -fL --retry 3 -o "$tmp/vosk.whl" "$url"
  unzip -o -q "$tmp/vosk.whl" -d "$tmp/x"
  mkdir -p third_party/vosk
  cp "$tmp/x/vosk/libvosk.so" third_party/vosk/
  rm -rf "$tmp"
  echo "  -> third_party/vosk/libvosk.so (헤더 vosk_api.h 는 리포에 포함)"
fi

echo "== [3/4] whisper.cpp 소스 + ggml-$WHISPER_SIZE 모델 =="
if [ ! -f third_party/whisper.cpp/CMakeLists.txt ]; then
  echo "  clone whisper.cpp ..."
  git clone --depth 1 https://github.com/ggml-org/whisper.cpp third_party/whisper.cpp
else
  echo "  이미 있음: third_party/whisper.cpp"
fi
if [ -f "third_party/whisper.cpp/models/ggml-$WHISPER_SIZE.bin" ]; then
  echo "  이미 있음: ggml-$WHISPER_SIZE.bin"
else
  echo "  다운로드: ggml-$WHISPER_SIZE.bin (시간 걸릴 수 있음)"
  ( cd third_party/whisper.cpp && bash ./models/download-ggml.sh "$WHISPER_SIZE" )
fi

echo "== [4/4] Vosk 한국어 모델: $VOSK_MODEL_NAME =="
if [ -d "third_party/models/$VOSK_MODEL_NAME" ]; then
  echo "  이미 있음: third_party/models/$VOSK_MODEL_NAME"
else
  mkdir -p third_party/models
  echo "  다운로드: https://alphacephei.com/vosk/models/$VOSK_MODEL_NAME.zip"
  curl -fL --retry 3 -o "third_party/models/$VOSK_MODEL_NAME.zip" \
    "https://alphacephei.com/vosk/models/$VOSK_MODEL_NAME.zip"
  ( cd third_party/models && unzip -o -q "$VOSK_MODEL_NAME.zip" && rm -f "$VOSK_MODEL_NAME.zip" )
  echo "  -> third_party/models/$VOSK_MODEL_NAME"
fi

echo
echo "✅ 셋업 완료. 다음: ./scripts/build.sh  그리고  ./scripts/run_demo.sh"
