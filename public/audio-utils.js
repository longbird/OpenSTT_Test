export function buildSessionQuery({ model, language, threshold, silenceMs, prompt, source, noiseReduction }) {
  return new URLSearchParams({
    model,
    language: language.trim() || "ko",
    threshold,
    silence_ms: silenceMs,
    prompt: prompt.trim(),
    source,
    noise_reduction: noiseReduction,
  });
}

export function audioBufferToMonoPcm16Base64Chunks(
  audioBuffer,
  { targetSampleRate = 24000, chunkMs = 100 } = {},
) {
  const mono = mixToMono(audioBuffer);
  const resampled = resampleLinear(mono, audioBuffer.sampleRate, targetSampleRate);
  const chunkFrames = Math.max(1, Math.round((targetSampleRate * chunkMs) / 1000));
  const chunks = [];

  for (let offset = 0; offset < resampled.length; offset += chunkFrames) {
    const frame = resampled.subarray(offset, Math.min(offset + chunkFrames, resampled.length));
    chunks.push({
      audio: pcm16ToBase64(frame),
      durationMs: (frame.length / targetSampleRate) * 1000,
      frames: frame.length,
    });
  }

  return chunks;
}

export function formatProgressLabel(step, percent) {
  const safePercent = Math.max(0, Math.min(100, Math.round(Number(percent) || 0)));
  return `${step} ${safePercent}%`;
}

export function buildAudioDecodeErrorMessage(error) {
  const message = error?.message || String(error);
  if (/Unable to decode audio data|EncodingError|decode/i.test(message)) {
    return "브라우저에서 바로 디코딩할 수 없는 오디오 형식입니다. 서버 변환을 시도했지만 실패했습니다. GSM/ADPCM 계열 WAV는 ffmpeg 변환 경로가 필요합니다.";
  }
  return message;
}

export function selectFileTranscriptionModel(model) {
  if (model === "gpt-4o-mini-transcribe" || model === "whisper-1") return model;
  return "gpt-4o-transcribe";
}

export function splitTranscriptForDisplay(transcript) {
  const text = String(transcript || "").replace(/\s+/g, " ").trim();
  if (!text) return [];

  const sentences = text.match(/[^.!?。！？]+[.!?。！？]+|[^.!?。！？]+$/g)
    ?.map((part) => part.trim())
    .filter(Boolean) || [];
  const lines = [];
  let current = "";

  for (const sentence of sentences) {
    const next = current ? `${current} ${sentence}` : sentence;
    if (isShortAcknowledgement(current) && next.length <= 36) {
      current = next;
      continue;
    }
    if (current) lines.push(current);
    current = sentence;
  }

  if (current) lines.push(current);
  return lines;
}

function mixToMono(audioBuffer) {
  const channelCount = audioBuffer.numberOfChannels;
  const length = audioBuffer.getChannelData(0).length;
  const output = new Float32Array(length);

  for (let channel = 0; channel < channelCount; channel += 1) {
    const data = audioBuffer.getChannelData(channel);
    for (let i = 0; i < length; i += 1) {
      output[i] += data[i] / channelCount;
    }
  }

  return output;
}

function isShortAcknowledgement(text) {
  return /^(네|네네|예|아|음|어|안녕하세요|알겠습니다)[.!?。！？]?$/.test(String(text || "").trim());
}

function resampleLinear(input, sourceSampleRate, targetSampleRate) {
  if (sourceSampleRate === targetSampleRate) {
    return input;
  }

  const ratio = sourceSampleRate / targetSampleRate;
  const outputLength = Math.max(1, Math.round(input.length / ratio));
  const output = new Float32Array(outputLength);

  for (let i = 0; i < outputLength; i += 1) {
    const sourceIndex = i * ratio;
    const leftIndex = Math.floor(sourceIndex);
    const rightIndex = Math.min(leftIndex + 1, input.length - 1);
    const weight = sourceIndex - leftIndex;
    output[i] = input[leftIndex] * (1 - weight) + input[rightIndex] * weight;
  }

  return output;
}

function pcm16ToBase64(samples) {
  const bytes = new Uint8Array(samples.length * 2);
  const view = new DataView(bytes.buffer);

  for (let i = 0; i < samples.length; i += 1) {
    const sample = Math.max(-1, Math.min(1, samples[i]));
    const pcm = sample < 0 ? Math.round(sample * 0x8000) : Math.round(sample * 0x7fff);
    view.setInt16(i * 2, pcm, true);
  }

  return bytesToBase64(bytes);
}

function bytesToBase64(bytes) {
  if (typeof Buffer !== "undefined") {
    return Buffer.from(bytes).toString("base64");
  }

  let binary = "";
  const batchSize = 0x8000;
  for (let i = 0; i < bytes.length; i += batchSize) {
    const batch = bytes.subarray(i, i + batchSize);
    binary += String.fromCharCode(...batch);
  }
  return btoa(binary);
}
