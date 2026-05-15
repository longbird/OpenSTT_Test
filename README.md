# OpenAI Realtime STT Test

브라우저 마이크 입력을 OpenAI Realtime transcription session으로 보내고 `gpt-realtime-whisper` 전사 델타와 완료 문장을 확인하는 최소 테스트 앱입니다.

## 실행

```powershell
Copy-Item .env.example .env
# .env 파일에 OPENAI_API_KEY 입력
npm install
npm start
```

브라우저에서 `http://localhost:3000`을 열고 마이크 권한을 허용한 뒤 `시작`을 누릅니다.

파일로 테스트하려면 `입력`을 `파일`로 바꾸고 오디오 파일을 선택한 뒤 `시작`을 누릅니다. 브라우저가 파일을 24kHz mono PCM16으로 변환하고, 100ms 단위 chunk를 실제 오디오 길이에 맞춰 Realtime 세션으로 전송합니다.

## 기본값

- 모델: `gpt-realtime-whisper`
- 언어 힌트: `ko`
- VAD: `server_vad`
- 묵음 종료: `500ms`

## 참고

공식 문서 기준 `gpt-realtime-whisper`는 실시간 전사용 모델이며, 라이브 오디오에서 transcript delta를 받는 용도입니다. 파일 전사나 델타가 필요 없는 요청-응답형 STT는 `gpt-4o-transcribe` 또는 `gpt-4o-mini-transcribe`도 비교 대상입니다.
