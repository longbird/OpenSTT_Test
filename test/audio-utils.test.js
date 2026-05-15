import assert from "node:assert/strict";
import test from "node:test";

import {
  audioBufferToMonoPcm16Base64Chunks,
  buildAudioDecodeErrorMessage,
  buildSessionQuery,
  formatProgressLabel,
  selectFileTranscriptionModel,
  splitTranscriptForDisplay,
} from "../public/audio-utils.js";

test("audioBufferToMonoPcm16Base64Chunks resamples mono audio to 24 kHz PCM16 chunks", () => {
  const source = makeAudioBuffer({
    sampleRate: 48000,
    channels: [[-1, -0.5, 0, 0.5, 1, 0.25, -0.25, 0]],
  });

  const chunks = audioBufferToMonoPcm16Base64Chunks(source, {
    targetSampleRate: 24000,
    chunkMs: 1,
  });

  assert.equal(chunks.length, 1);
  assert.equal(chunks[0].durationMs, 1000 / 24000 * 4);
  assert.equal(Buffer.from(chunks[0].audio, "base64").length, 8);
});

test("audioBufferToMonoPcm16Base64Chunks averages stereo channels before encoding", () => {
  const source = makeAudioBuffer({
    sampleRate: 24000,
    channels: [
      [1, 1],
      [-1, 0],
    ],
  });

  const chunks = audioBufferToMonoPcm16Base64Chunks(source, {
    targetSampleRate: 24000,
    chunkMs: 20,
  });
  const pcm = Buffer.from(chunks[0].audio, "base64");

  assert.equal(pcm.readInt16LE(0), 0);
  assert.equal(pcm.readInt16LE(2), 16384);
});

test("buildSessionQuery marks file sessions for manual audio commits", () => {
  const query = buildSessionQuery({
    model: "gpt-realtime-whisper",
    language: "ko",
    threshold: "0.5",
    silenceMs: "500",
    prompt: "PBX",
    source: "file",
    noiseReduction: "far_field",
  });

  assert.equal(query.get("source"), "file");
  assert.equal(query.get("model"), "gpt-realtime-whisper");
  assert.equal(query.get("prompt"), "PBX");
  assert.equal(query.get("noise_reduction"), "far_field");
});

test("formatProgressLabel shows step and percent for long-running work", () => {
  assert.equal(formatProgressLabel("샘플 로드", 37), "샘플 로드 37%");
  assert.equal(formatProgressLabel("파일 전송", 100), "파일 전송 100%");
});

test("buildAudioDecodeErrorMessage explains unsupported browser audio codecs", () => {
  const message = buildAudioDecodeErrorMessage(new DOMException("Unable to decode audio data"));

  assert.match(message, /브라우저에서 바로 디코딩할 수 없는 오디오 형식/);
  assert.match(message, /서버 변환/);
});

test("selectFileTranscriptionModel keeps file transcription models and maps realtime-only defaults", () => {
  assert.equal(selectFileTranscriptionModel("gpt-4o-transcribe"), "gpt-4o-transcribe");
  assert.equal(selectFileTranscriptionModel("gpt-4o-mini-transcribe"), "gpt-4o-mini-transcribe");
  assert.equal(selectFileTranscriptionModel("gpt-realtime-whisper"), "gpt-4o-transcribe");
});

test("splitTranscriptForDisplay splits file API paragraphs into readable utterances", () => {
  assert.deepEqual(
    splitTranscriptForDisplay("안녕하세요. 네. 기사님 운행 끝나시면 통화하겠습니다. 조심히 들어가세요."),
    [
      "안녕하세요. 네.",
      "기사님 운행 끝나시면 통화하겠습니다.",
      "조심히 들어가세요.",
    ],
  );
});

function makeAudioBuffer({ sampleRate, channels }) {
  return {
    sampleRate,
    numberOfChannels: channels.length,
    duration: channels[0].length / sampleRate,
    getChannelData(index) {
      return Float32Array.from(channels[index]);
    },
  };
}
