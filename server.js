import "dotenv/config";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import express from "express";
import ffmpegPath from "ffmpeg-static";
import {
  correctTranscriptWithClaude,
  correctTranscriptWithGemini,
  DEFAULT_CLAUDE_MODEL,
  DEFAULT_GEMINI_MODEL,
  evaluateTranscriptQualityWithClaude,
  evaluateTranscriptQualityWithGemini,
} from "./gemini-correction.js";
import {
  getApiKey,
  getApiKeyStatus,
  saveApiKeys,
} from "./key-store.js";
import {
  evaluateTranscriptQualityLocally,
  formatLocalQualityEvaluation,
} from "./public/quality-rules.js";

const app = express();
const port = Number(process.env.PORT || 3000);
const projectRoot = process.cwd();
const sampleRoot = path.resolve(projectRoot, "sample");

app.use(express.static("public", {
  etag: false,
  maxAge: 0,
  setHeaders(res) {
    res.setHeader("Cache-Control", "no-store");
  },
}));
app.use(express.text({ type: "application/sdp", limit: "1mb" }));
app.use(express.json({ limit: "1mb" }));
app.use("/transcode-audio", express.raw({
  type: "application/octet-stream",
  limit: "200mb",
}));
app.use("/inspect-audio", express.raw({
  type: "*/*",
  limit: "200mb",
}));
app.use("/transcribe-file", express.raw({
  type: "*/*",
  limit: "200mb",
}));
app.use("/transcribe-stereo-dialogue", express.raw({
  type: "*/*",
  limit: "200mb",
}));

app.get("/health", async (_req, res) => {
  const keyStatus = await getApiKeyStatus();
  res.json({
    ok: true,
    hasApiKey: keyStatus.openai.configured,
    hasGeminiKey: keyStatus.gemini.configured,
    hasAnthropicKey: keyStatus.anthropic.configured,
  });
});

app.get("/api-keys", async (_req, res) => {
  try {
    res.json(await getApiKeyStatus());
  } catch (error) {
    res.status(500).json({ error: error.message || String(error) });
  }
});

app.post("/api-keys", async (req, res) => {
  try {
    await saveApiKeys({
      openai: req.body?.openai,
      gemini: req.body?.gemini,
      anthropic: req.body?.anthropic,
    });
    res.json(await getApiKeyStatus());
  } catch (error) {
    res.status(500).json({ error: error.message || String(error) });
  }
});

app.post("/correct-transcript", async (req, res) => {
  const transcript = String(req.body?.transcript || "");
  const provider = sanitizeCorrectionProvider(req.body?.provider);
  const startedAt = Date.now();
  console.log(`[correction] request provider=${provider} chars=${transcript.length}`);

  try {
    const corrected = provider === "claude"
      ? await correctTranscriptWithClaude({
        apiKey: await getApiKey("anthropic"),
        model: process.env.CLAUDE_MODEL || DEFAULT_CLAUDE_MODEL,
        transcript,
      })
      : await correctTranscriptWithGemini({
        apiKey: await getApiKey("gemini"),
        model: process.env.GEMINI_MODEL || DEFAULT_GEMINI_MODEL,
        transcript,
      });
    console.log(`[correction] ok provider=${provider} elapsed_ms=${Date.now() - startedAt}`);
    res.json({ corrected });
  } catch (error) {
    console.error(`[correction] failed provider=${provider} elapsed_ms=${Date.now() - startedAt}`, error);
    res.status(error.message?.includes("_API_KEY") ? 500 : 502).json({
      error: error.message || String(error),
    });
  }
});

app.post("/evaluate-quality", async (req, res) => {
  const transcript = String(req.body?.transcript || "");
  const provider = sanitizeCorrectionProvider(req.body?.provider);
  const includeAiNote = req.body?.includeAiNote !== false;
  const startedAt = Date.now();
  console.log(`[quality] request provider=${provider} ai_note=${includeAiNote} chars=${transcript.length}`);

  try {
    const local = evaluateTranscriptQualityLocally(transcript, req.body?.settings);
    let aiNote = "";
    let aiError = "";

    if (includeAiNote) {
      try {
        aiNote = provider === "claude"
          ? await evaluateTranscriptQualityWithClaude({
            apiKey: await getApiKey("anthropic"),
            model: process.env.CLAUDE_MODEL || DEFAULT_CLAUDE_MODEL,
            transcript,
          })
          : await evaluateTranscriptQualityWithGemini({
            apiKey: await getApiKey("gemini"),
            model: process.env.GEMINI_MODEL || DEFAULT_GEMINI_MODEL,
            transcript,
          });
      } catch (error) {
        aiError = error.message || String(error);
      }
    }

    console.log(`[quality] ok provider=${provider} score=${local.totalScore} elapsed_ms=${Date.now() - startedAt}`);
    res.json({
      evaluation: formatLocalQualityEvaluation(local, aiNote || (aiError ? `AI 참고 의견 생성 실패: ${aiError}` : "")),
      local,
      aiNote,
      aiError,
    });
  } catch (error) {
    console.error(`[quality] failed provider=${provider} elapsed_ms=${Date.now() - startedAt}`, error);
    res.status(500).json({
      error: error.message || String(error),
    });
  }
});

app.post("/transcode-audio", async (req, res) => {
  if (!ffmpegPath) {
    res.status(500).json({ error: "ffmpeg binary is not available." });
    return;
  }

  if (!Buffer.isBuffer(req.body) || req.body.length === 0) {
    res.status(400).json({ error: "Expected audio bytes." });
    return;
  }

  try {
    const wav = await transcodeToPcmWav(req.body);
    res.type("audio/wav").send(wav);
  } catch (error) {
    console.error("[transcode] failed", error);
    res.status(422).json({ error: error.message || String(error) });
  }
});

app.post("/inspect-audio", async (req, res) => {
  if (!ffmpegPath) {
    res.status(500).json({ error: "ffmpeg binary is not available." });
    return;
  }

  if (!Buffer.isBuffer(req.body) || req.body.length === 0) {
    res.status(400).json({ error: "Expected audio bytes." });
    return;
  }

  try {
    res.json(await inspectAudioMetadata(req.body));
  } catch (error) {
    console.error("[inspect-audio] failed", error);
    res.status(422).json({ error: error.message || String(error) });
  }
});

app.post("/transcribe-file", async (req, res) => {
  const openAiApiKey = await getApiKey("openai");
  if (!openAiApiKey) {
    res.status(500).json({ error: "OPENAI_API_KEY is not set. Copy .env.example to .env and add your key." });
    return;
  }

  if (!Buffer.isBuffer(req.body) || req.body.length === 0) {
    res.status(400).json({ error: "Expected audio bytes." });
    return;
  }

  const model = sanitizeFileTranscriptionModel(req.query.model) || "gpt-4o-transcribe";
  const language = sanitizeLanguage(req.query.language) || "ko";
  const prompt = String(req.query.prompt || "").trim();
  const requestedName = decodeURIComponent(String(req.get("X-Audio-File-Name") || "audio.wav"));
  const contentType = String(req.get("Content-Type") || contentTypeFor(requestedName));
  const startedAt = Date.now();
  console.log(`[transcribe-file] request model=${model} language=${language} bytes=${req.body.length}`);

  try {
    let result = await requestOpenAiTranscription({
      apiKey: openAiApiKey,
      audioBuffer: req.body,
      fileName: path.basename(requestedName),
      contentType,
      model,
      language,
      prompt,
    });

    let inputMode = "original";
    if (!result.response.ok && shouldRetryWithTranscode(result.payload)) {
      console.warn(`[transcribe-file] original rejected, retrying transcoded: ${result.payload?.error?.message || result.response.status}`);
      const wav = await transcodeToPcmWav(req.body);
      result = await requestOpenAiTranscription({
        apiKey: openAiApiKey,
        audioBuffer: wav,
        fileName: replaceExtension(path.basename(requestedName), ".wav"),
        contentType: "audio/wav",
        model,
        language,
        prompt,
      });
      inputMode = "transcoded";
    }

    console.log(`[transcribe-file] response status=${result.response.status} input=${inputMode} elapsed_ms=${Date.now() - startedAt}`);
    if (!result.response.ok) {
      res.status(result.response.status).json({
        error: result.payload?.error?.message || `OpenAI transcription failed: HTTP ${result.response.status}`,
        inputMode,
      });
      return;
    }
    res.json({ text: result.payload.text || "", model, inputMode });
  } catch (error) {
    console.error(`[transcribe-file] failed elapsed_ms=${Date.now() - startedAt}`, error);
    res.status(502).json({ error: error.message || String(error) });
  }
});

app.post("/transcribe-stereo-dialogue", async (req, res) => {
  const openAiApiKey = await getApiKey("openai");
  if (!openAiApiKey) {
    res.status(500).json({ error: "OPENAI_API_KEY is not set. Copy .env.example to .env and add your key." });
    return;
  }

  if (!Buffer.isBuffer(req.body) || req.body.length === 0) {
    res.status(400).json({ error: "Expected audio bytes." });
    return;
  }

  const language = sanitizeLanguage(req.query.language) || "ko";
  const startedAt = Date.now();
  console.log(`[stereo-dialogue] request language=${language} bytes=${req.body.length}`);

  try {
    const leftWav = await extractChannelToPcmWav(req.body, "left");
    const rightWav = await extractChannelToPcmWav(req.body, "right");
    const [left, right] = await Promise.all([
      requestOpenAiTranscription({
        apiKey: openAiApiKey,
        audioBuffer: leftWav,
        fileName: "left.wav",
        contentType: "audio/wav",
        model: "whisper-1",
        language,
        prompt: "",
        responseFormat: "verbose_json",
        timestampGranularities: ["segment"],
      }),
      requestOpenAiTranscription({
        apiKey: openAiApiKey,
        audioBuffer: rightWav,
        fileName: "right.wav",
        contentType: "audio/wav",
        model: "whisper-1",
        language,
        prompt: "",
        responseFormat: "verbose_json",
        timestampGranularities: ["segment"],
      }),
    ]);

    if (!left.response.ok || !right.response.ok) {
      res.status(502).json({
        error: left.payload?.error?.message || right.payload?.error?.message || "Stereo channel transcription failed.",
      });
      return;
    }

    const segments = [
      ...normalizeTimedSegments(left.payload, "left", "고객"),
      ...normalizeTimedSegments(right.payload, "right", "상담원"),
    ].sort((a, b) => a.start - b.start || a.end - b.end || a.channel.localeCompare(b.channel));

    console.log(`[stereo-dialogue] response segments=${segments.length} elapsed_ms=${Date.now() - startedAt}`);
    res.json({
      model: "whisper-1",
      channelMapping: { left: "고객", right: "상담원" },
      text: segments.map((segment) => `${segment.speaker}: ${segment.text}`).join("\n"),
      segments,
    });
  } catch (error) {
    console.error(`[stereo-dialogue] failed elapsed_ms=${Date.now() - startedAt}`, error);
    res.status(502).json({ error: error.message || String(error) });
  }
});

app.get("/local-audio", (req, res) => {
  const relativePath = String(req.query.path || "");
  const resolvedPath = path.resolve(sampleRoot, relativePath);

  if (!resolvedPath.startsWith(`${sampleRoot}${path.sep}`)) {
    res.status(400).json({ error: "Only files under sample/ can be loaded." });
    return;
  }

  if (!fs.existsSync(resolvedPath)) {
    res.status(404).json({ error: "Audio file not found." });
    return;
  }

  const stat = fs.statSync(resolvedPath);
  if (!stat.isFile()) {
    res.status(404).json({ error: "Audio file not found." });
    return;
  }

  res.type(contentTypeFor(resolvedPath));
  res.setHeader("Content-Length", stat.size);
  res.setHeader("X-Audio-File-Name", encodeURIComponent(path.basename(resolvedPath)));
  fs.createReadStream(resolvedPath).pipe(res);
});

app.post("/session", async (req, res) => {
  const openAiApiKey = await getApiKey("openai");
  if (!openAiApiKey) {
    res.status(500).json({ error: "OPENAI_API_KEY is not set. Copy .env.example to .env and add your key." });
    return;
  }

  if (!req.body || typeof req.body !== "string") {
    res.status(400).json({ error: "Expected SDP offer body." });
    return;
  }

  const language = sanitizeLanguage(req.query.language) || "ko";
  const model = sanitizeModel(req.query.model) || "gpt-realtime-whisper";
  const threshold = clampNumber(req.query.threshold, 0, 1, 0.5);
  const silenceDurationMs = clampNumber(req.query.silence_ms, 100, 3000, 500);
  const prompt = String(req.query.prompt || "").trim();
  const source = sanitizeSource(req.query.source);
  const noiseReduction = sanitizeNoiseReduction(req.query.noise_reduction);
  const startedAt = Date.now();
  console.log(`[session] request source=${source} model=${model} language=${language} noise_reduction=${noiseReduction}`);

  const input = {
    transcription: {
      model,
      language,
      ...(prompt ? { prompt } : {}),
    },
  };

  if (source === "file") {
    input.format = {
      type: "audio/pcm",
      rate: 24000,
    };
    if (noiseReduction !== "off") {
      input.noise_reduction = { type: noiseReduction };
    }
    input.turn_detection = null;
  } else {
    if (noiseReduction !== "off") {
      input.noise_reduction = { type: noiseReduction };
    }
    input.turn_detection = {
      type: "server_vad",
      threshold,
      prefix_padding_ms: 300,
      silence_duration_ms: silenceDurationMs,
    };
  }

  const session = {
    type: "transcription",
    audio: {
      input,
    },
  };

  const form = new FormData();
  form.set("sdp", req.body);
  form.set("session", JSON.stringify(session));

  try {
    const response = await createRealtimeCallWithRetry(form, openAiApiKey);

    const text = await response.text();
    console.log(`[session] response status=${response.status} elapsed_ms=${Date.now() - startedAt}`);
    if (!response.ok) {
      res.status(response.status).type("text/plain").send(text);
      return;
    }

    res.type("application/sdp").send(text);
  } catch (error) {
    console.error(`[session] failed elapsed_ms=${Date.now() - startedAt}`, error);
    const message = error.name === "TimeoutError"
      ? "OpenAI Realtime session creation timed out after 30 seconds."
      : error.message || String(error);
    res.status(504).type("text/plain").send(message);
    return;
  }
});

async function createRealtimeCallWithRetry(form, apiKey) {
  let lastResponse;
  for (let attempt = 1; attempt <= 3; attempt += 1) {
    lastResponse = await fetch("https://api.openai.com/v1/realtime/calls", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${apiKey}`,
      },
      body: form,
      signal: AbortSignal.timeout(30000),
    });

    if (lastResponse.status !== 502 && lastResponse.status !== 503 && lastResponse.status !== 504) {
      return lastResponse;
    }

    console.warn(`[session] retryable_status=${lastResponse.status} attempt=${attempt}`);
    if (attempt < 3) {
      await new Promise((resolve) => setTimeout(resolve, attempt * 1500));
    }
  }

  return lastResponse;
}

function transcodeToPcmWav(inputBuffer) {
  return new Promise((resolve, reject) => {
    const ffmpeg = spawn(ffmpegPath, [
      "-hide_banner",
      "-loglevel",
      "error",
      "-i",
      "pipe:0",
      "-ac",
      "1",
      "-ar",
      "24000",
      "-acodec",
      "pcm_s16le",
      "-f",
      "wav",
      "pipe:1",
    ], {
      stdio: ["pipe", "pipe", "pipe"],
    });

    const stdout = [];
    const stderr = [];

    ffmpeg.stdout.on("data", (chunk) => stdout.push(chunk));
    ffmpeg.stderr.on("data", (chunk) => stderr.push(chunk));
    ffmpeg.on("error", reject);
    ffmpeg.stdin.on("error", () => {
      // ffmpeg may close stdin early after reading enough header data for inspection.
    });
    ffmpeg.on("close", (code) => {
      if (code === 0) {
        resolve(Buffer.concat(stdout));
        return;
      }
      reject(new Error(Buffer.concat(stderr).toString("utf8").trim() || `ffmpeg exited with ${code}`));
    });

    ffmpeg.stdin.end(inputBuffer);
  });
}

function inspectAudioMetadata(inputBuffer) {
  return new Promise((resolve, reject) => {
    const ffmpeg = spawn(ffmpegPath, [
      "-hide_banner",
      "-nostats",
      "-i",
      "pipe:0",
      "-f",
      "null",
      "-",
    ], {
      stdio: ["pipe", "ignore", "pipe"],
    });

    const stderr = [];

    ffmpeg.stderr.on("data", (chunk) => stderr.push(chunk));
    ffmpeg.on("error", reject);
    ffmpeg.on("close", (code) => {
      const output = Buffer.concat(stderr).toString("utf8");
      const metadata = parseFfmpegAudioMetadata(output);
      if (metadata.channels > 0) {
        resolve(metadata);
        return;
      }
      reject(new Error(output.trim() || `ffmpeg inspect exited with ${code}`));
    });

    ffmpeg.stdin.end(inputBuffer);
  });
}

function parseFfmpegAudioMetadata(output) {
  const audioLine = output.split(/\r?\n/).find((line) => line.includes("Audio:")) || "";
  const sampleRate = Number(audioLine.match(/,\s*(\d+)\s*Hz\b/)?.[1] || 0);
  const layoutText = audioLine.match(/,\s*(mono|stereo|[1-9]\d*\s+channels?)\b/i)?.[1] || "";
  const normalizedLayout = layoutText.trim().toLowerCase();
  let channels = 0;
  if (normalizedLayout === "mono") channels = 1;
  if (normalizedLayout === "stereo") channels = 2;
  channels = channels || Number(normalizedLayout.match(/^(\d+)\s+channels?$/)?.[1] || 0);

  return {
    channels,
    channelLayout: normalizedLayout,
    sampleRate,
  };
}

function extractChannelToPcmWav(inputBuffer, channel) {
  const mapChannel = channel === "right" ? "0.0.1" : "0.0.0";
  return new Promise((resolve, reject) => {
    const ffmpeg = spawn(ffmpegPath, [
      "-hide_banner",
      "-loglevel",
      "error",
      "-i",
      "pipe:0",
      "-map_channel",
      mapChannel,
      "-acodec",
      "pcm_s16le",
      "-ar",
      "8000",
      "-ac",
      "1",
      "-f",
      "wav",
      "pipe:1",
    ], {
      stdio: ["pipe", "pipe", "pipe"],
    });

    const stdout = [];
    const stderr = [];

    ffmpeg.stdout.on("data", (chunk) => stdout.push(chunk));
    ffmpeg.stderr.on("data", (chunk) => stderr.push(chunk));
    ffmpeg.on("error", reject);
    ffmpeg.on("close", (code) => {
      if (code === 0) {
        resolve(Buffer.concat(stdout));
        return;
      }
      reject(new Error(Buffer.concat(stderr).toString("utf8").trim() || `ffmpeg channel extract exited with ${code}`));
    });

    ffmpeg.stdin.end(inputBuffer);
  });
}

async function requestOpenAiTranscription({
  apiKey,
  audioBuffer,
  fileName,
  contentType,
  model,
  language,
  prompt,
  responseFormat = "json",
  timestampGranularities = [],
}) {
  const form = new FormData();
  form.set("model", model);
  form.set("language", language);
  form.set("response_format", responseFormat);
  timestampGranularities.forEach((granularity) => {
    form.append("timestamp_granularities[]", granularity);
  });
  if (prompt) form.set("prompt", prompt);
  form.set("file", new File([audioBuffer], fileName, {
    type: contentType || "application/octet-stream",
  }));

  const response = await fetch("https://api.openai.com/v1/audio/transcriptions", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${apiKey}`,
    },
    body: form,
    signal: AbortSignal.timeout(180000),
  });
  const payload = await response.json().catch(() => ({}));
  return { response, payload };
}

function normalizeTimedSegments(payload, channel, speaker) {
  return (payload?.segments || [])
    .map((segment) => ({
      speaker,
      channel,
      start: Number(segment.start || 0),
      end: Number(segment.end || 0),
      text: String(segment.text || "").trim(),
    }))
    .filter((segment) => segment.text);
}

function shouldRetryWithTranscode(payload) {
  const message = payload?.error?.message || "";
  return /format|codec|decode|audio|file/i.test(message);
}

app.listen(port, () => {
  console.log(`Realtime STT test app: http://localhost:${port}`);
});

function sanitizeLanguage(value) {
  if (typeof value !== "string") return "";
  const normalized = value.trim().toLowerCase();
  return /^[a-z]{2}(-[a-z]{2})?$/.test(normalized) ? normalized : "";
}

function sanitizeModel(value) {
  if (typeof value !== "string") return "";
  const allowed = new Set([
    "gpt-realtime-whisper",
    "gpt-4o-transcribe",
    "gpt-4o-mini-transcribe",
    "whisper-1",
  ]);
  return allowed.has(value) ? value : "";
}

function sanitizeFileTranscriptionModel(value) {
  if (typeof value !== "string") return "";
  const allowed = new Set([
    "gpt-4o-transcribe",
    "gpt-4o-mini-transcribe",
    "whisper-1",
  ]);
  return allowed.has(value) ? value : "";
}

function sanitizeSource(value) {
  return value === "file" ? "file" : "mic";
}

function sanitizeCorrectionProvider(value) {
  return value === "claude" ? "claude" : "gemini";
}

function sanitizeNoiseReduction(value) {
  if (value === "near_field" || value === "far_field") return value;
  return "off";
}

function contentTypeFor(filePath) {
  const extension = path.extname(filePath).toLowerCase();
  if (extension === ".mp3") return "audio/mpeg";
  if (extension === ".wav") return "audio/wav";
  if (extension === ".m4a") return "audio/mp4";
  if (extension === ".ogg") return "audio/ogg";
  if (extension === ".flac") return "audio/flac";
  return "application/octet-stream";
}

function replaceExtension(fileName, extension) {
  const parsed = path.parse(fileName);
  return `${parsed.name}${extension}`;
}

function clampNumber(value, min, max, fallback) {
  const number = Number(value);
  if (!Number.isFinite(number)) return fallback;
  return Math.min(max, Math.max(min, number));
}
