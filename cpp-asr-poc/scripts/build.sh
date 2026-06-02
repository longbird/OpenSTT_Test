#!/usr/bin/env bash
# 빌드 + 테스트. setup.sh 이후 실행(libvosk/whisper 가 있어야 asr_pipeline 이 생성됨).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT"

cmake -B build -S .
cmake --build build -j

echo
echo "== 테스트 =="
ctest --test-dir build --output-on-failure

echo
echo "생성된 바이너리:"
ls build | grep -E '^asr_|^wav2pcmu|^gen_test_audio' | sed 's/^/  /'
