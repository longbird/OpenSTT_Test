import {
  audioBufferToMonoPcm16Base64Chunks,
  buildAudioDecodeErrorMessage,
  buildSessionQuery,
  formatProgressLabel,
  selectFileTranscriptionModel,
  splitTranscriptForDisplay,
} from "./audio-utils.js";
import {
  buildResultExport,
  downloadJsonFile,
} from "./result-utils.js";
import { buildSessionFetchErrorMessage } from "./session-utils.js";
import {
  buildTranscriptMessage,
  parseTranscriptLine,
} from "./transcript-chat-utils.js";
import {
  DEFAULT_QUALITY_RULE_SETTINGS,
  evaluateTranscriptQualityLocally,
  formatLocalQualityEvaluation,
  getQualityRuleCatalog,
  normalizeQualityRuleSettings,
} from "./quality-rules.js";

const startButton = document.querySelector("#start");
const stopButton = document.querySelector("#stop");
const statusEl = document.querySelector("#status");
const partialEl = document.querySelector("#partial");
const finalsEl = document.querySelector("#finals");
const correctionEl = document.querySelector("#correction");
const qualityEvaluationEl = document.querySelector("#qualityEvaluation");
const eventsPanelEl = document.querySelector("#eventsPanel");
const partialPanelEl = document.querySelector("#partialPanel");
const finalsPanelEl = document.querySelector("#finalsPanel");
const correctionPanelEl = document.querySelector("#correctionPanel");
const qualityPanelEl = document.querySelector("#qualityPanel");
const comparisonPanelEl = document.querySelector("#comparisonPanel");
const eventsEl = document.querySelector("#events");
const thresholdEl = document.querySelector("#threshold");
const thresholdValueEl = document.querySelector("#thresholdValue");
const sourceEl = document.querySelector("#source");
const correctionProviderEl = document.querySelector("#correctionProvider");
const noiseReductionEl = document.querySelector("#noiseReduction");
const fileControlEl = document.querySelector("#fileControl");
const fileModeControlEl = document.querySelector("#fileModeControl");
const transcriptionModeEl = document.querySelector("#transcriptionMode");
const audioFileEl = document.querySelector("#audioFile");
const fileStatusEl = document.querySelector("#fileStatus");
const fileNameEl = document.querySelector("#fileName");
const fileMetaEl = document.querySelector("#fileMeta");
const fileProgressEl = document.querySelector("#fileProgress");
const progressTextEl = document.querySelector("#progressText");
const sampleControlEl = document.querySelector("#sampleControl");
const samplePathEl = document.querySelector("#samplePath");
const loadSampleButton = document.querySelector("#loadSample");
const compareNoiseButton = document.querySelector("#compareNoise");
const correctTranscriptButton = document.querySelector("#correctTranscript");
const evaluateQualityButton = document.querySelector("#evaluateQuality");
const saveResultButton = document.querySelector("#saveResult");
const comparisonEl = document.querySelector("#comparison");
const openaiKeyEl = document.querySelector("#openaiKey");
const anthropicKeyEl = document.querySelector("#anthropicKey");
const geminiKeyEl = document.querySelector("#geminiKey");
const saveApiKeysButton = document.querySelector("#saveApiKeys");
const apiKeyStatusEl = document.querySelector("#apiKeyStatus");
const qualityRuleSettingsEl = document.querySelector("#qualityRuleSettings");
const qualityRuleTotalEl = document.querySelector("#qualityRuleTotal");
const resetQualityRulesButton = document.querySelector("#resetQualityRules");

let pc;
let dc;
let micStream;
let fileAudioContext;
let fileSilenceSource;
let fileSilenceStream;
let selectedServerAudio;
let runId = 0;
let partialsByItem = new Map();
let lastProgress = { step: "", percent: 0 };
let isStarting = false;
let isNoiseComparing = false;
let runCompletionResolve;
let correctionResults = {};
let qualityEvaluationResult = "";
let pendingAudioInspection = Promise.resolve();
const QUALITY_RULE_SETTINGS_KEY = "openai-stt-quality-rule-settings";
let qualityRuleSettings = loadQualityRuleSettings();

window.addEventListener("error", (event) => {
  setStatus("오류", "error");
  logEvent(`window.error: ${event.message}`);
});

window.addEventListener("unhandledrejection", (event) => {
  setStatus("오류", "error");
  logEvent(`unhandledrejection: ${event.reason?.message || event.reason || "unknown"}`);
});

thresholdEl.addEventListener("input", () => {
  thresholdValueEl.textContent = Number(thresholdEl.value).toFixed(2);
});

startButton.addEventListener("click", start);
stopButton.addEventListener("click", stop);
sourceEl.addEventListener("change", syncSourceControls);
audioFileEl.addEventListener("change", () => {
  pendingAudioInspection = syncFileMeta();
});
loadSampleButton.addEventListener("click", loadServerSample);
compareNoiseButton.addEventListener("click", compareNoiseReduction);
correctTranscriptButton.addEventListener("click", requestAiCorrection);
evaluateQualityButton.addEventListener("click", requestQualityEvaluation);
saveResultButton.addEventListener("click", saveResultJson);
saveApiKeysButton.addEventListener("click", saveApiKeys);
resetQualityRulesButton.addEventListener("click", resetQualityRuleSettings);
document.querySelectorAll("[data-target-panel]").forEach((button) => {
  button.addEventListener("click", () => {
    const panel = document.querySelector(`#${button.dataset.targetPanel}`);
    setPanelExpanded(panel, panel?.classList.contains("is-collapsed"));
  });
});

syncSourceControls();
collapseWorkPanels();
renderQualityRuleSettings();
loadApiKeyStatus();

async function start() {
  if (isStarting) return;
  return startTranscription();
}

async function startTranscription() {
  isStarting = true;
  startButton.disabled = true;
  stopButton.disabled = false;
  setStatus("시작 요청");
  logEvent("start.request");

  const source = sourceEl.value;
  let file = selectedServerAudio || audioFileEl.files?.[0];
  if (source === "file" && !file) {
    setStatus("파일 필요", "error");
    logEvent("오디오 파일을 선택하세요.");
    isStarting = false;
    startButton.disabled = false;
    stopButton.disabled = true;
    return;
  }
  if (source === "file") {
    await pendingAudioInspection;
    file = selectedServerAudio || audioFileEl.files?.[0];
  }

  const currentRunId = runId + 1;
  runId = currentRunId;
  setStatus("연결 중");
  partialEl.textContent = "";
  finalsEl.innerHTML = "";
  correctionEl.textContent = "";
  qualityEvaluationEl.textContent = "";
  eventsEl.textContent = "";
  showTranscriptionPanels();
  partialsByItem = new Map();
  correctionResults = {};
  qualityEvaluationResult = "";
  correctTranscriptButton.disabled = true;
  evaluateQualityButton.disabled = true;
  saveResultButton.disabled = true;
  const runCompletion = new Promise((resolve) => {
    runCompletionResolve = resolve;
  });

  try {
    if (source === "file" && transcriptionModeEl.value !== "realtime") {
      await transcribeFileApi(file);
      return runCompletion;
    }

    pc = new RTCPeerConnection();
    dc = pc.createDataChannel("oai-events");
    dc.addEventListener("open", () => {
      setStatus("전사 중", "live");
      logEvent("data_channel.open");
    });
    dc.addEventListener("message", handleRealtimeEvent);

    if (source === "mic") {
      micStream = await navigator.mediaDevices.getUserMedia({
        audio: {
          channelCount: 1,
          echoCancellation: true,
          noiseSuppression: true,
          autoGainControl: true,
        },
      });
      micStream.getAudioTracks().forEach((track) => pc.addTrack(track, micStream));
    } else {
      addSilentAudioTrackForFileMode();
    }

    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);

    setStatus("세션 생성");
    logEvent("session.create.start");
    const answerSdp = await createSession(offer.sdp, source);
    logEvent("session.create.ok");
    await pc.setRemoteDescription({ type: "answer", sdp: answerSdp });

    if (source === "file") {
      setStatus("채널 대기");
      await waitForDataChannelOpen(dc);
      await streamFileRealtime(file, currentRunId);
    }
    return runCompletion;
  } catch (error) {
    setStatus("오류", "error");
    logEvent(error.stack || error.message || String(error));
    stop();
    if (isNoiseComparing) {
      throw error;
    }
  } finally {
    isStarting = false;
  }
}

async function transcribeFileApi(file) {
  const mode = transcriptionModeEl.value;
  setStatus(mode === "stereo_dialogue" ? "스테레오 화자분리" : "파일 전사 API", "live");
  showProgress("파일 업로드", 0);
  const model = selectFileTranscriptionModel(document.querySelector("#model").value);
  const arrayBuffer = await file.arrayBuffer();
  const endpoint = mode === "stereo_dialogue" ? "/transcribe-stereo-dialogue" : "/transcribe-file";
  logEvent(`file_api.transcribe.start: mode=${mode} endpoint=${endpoint} model=${model} file=${file.name}`);
  const response = await fetch(`${endpoint}?${buildFileTranscriptionQuery(model).toString()}`, {
    method: "POST",
    headers: {
      "Content-Type": file.type || "application/octet-stream",
      "X-Audio-File-Name": encodeURIComponent(file.name || "audio.wav"),
    },
    body: arrayBuffer,
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload.error || `File transcription failed: HTTP ${response.status}`);
  }

  if (mode === "stereo_dialogue" && !Array.isArray(payload.segments)) {
    throw new Error("스테레오 화자분리 결과에 segments가 없습니다. 파일 전사 API 경로로 실행됐는지 이벤트 로그를 확인하세요.");
  }

  if (Array.isArray(payload.segments)) {
    payload.segments.forEach((segment) => {
      appendFinalMessage(buildTranscriptMessage(segment));
    });
  } else {
    splitTranscriptForDisplay(payload.text || "").forEach((line) => appendFinal(line));
  }
  showProgress("전사 완료", 100);
  setStatus("전사 완료", "live");
  showTranscriptionCompletePanels();
  correctTranscriptButton.disabled = !collectFinalTranscript();
  evaluateQualityButton.disabled = !collectFinalTranscript();
  saveResultButton.disabled = !collectFinalTranscript();
  startButton.disabled = false;
  stopButton.disabled = true;
  logEvent(`file_api.transcribe.ok: mode=${mode} endpoint=${endpoint} segments=${payload.segments?.length || 0} ${payload.text?.length || 0} chars input=${payload.inputMode || "unknown"}`);
  runCompletionResolve?.(collectFinalTranscript());
}

function buildFileTranscriptionQuery(model) {
  return new URLSearchParams({
    model,
    language: document.querySelector("#language").value.trim() || "ko",
    prompt: document.querySelector("#prompt").value.trim(),
  });
}

async function createSession(sdp, source) {
  const params = buildSessionQuery({
    model: document.querySelector("#model").value,
    language: document.querySelector("#language").value.trim() || "ko",
    threshold: document.querySelector("#threshold").value,
    silenceMs: document.querySelector("#silence").value,
    prompt: document.querySelector("#prompt").value.trim(),
    source,
    noiseReduction: noiseReductionEl.value,
  });

  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 35000);
  let response;
  try {
    response = await fetch(`/session?${params.toString()}`, {
      method: "POST",
      headers: { "Content-Type": "application/sdp" },
      body: sdp,
      signal: controller.signal,
    });
  } catch (error) {
    if (error.name === "AbortError") {
      throw new Error("세션 생성 시간 초과: OpenAI Realtime 세션 응답이 35초 안에 오지 않았습니다.");
    }
    throw new Error(buildSessionFetchErrorMessage(error));
  } finally {
    clearTimeout(timeout);
  }

  const text = await response.text();
  if (!response.ok) {
    throw new Error(text || `Session creation failed: HTTP ${response.status}`);
  }
  return text;
}

function handleRealtimeEvent(message) {
  const event = JSON.parse(message.data);

  if (event.type === "conversation.item.input_audio_transcription.delta") {
    const itemId = event.item_id || "unknown";
    const next = `${partialsByItem.get(itemId) || ""}${event.delta || ""}`;
    partialsByItem.set(itemId, next);
    renderPartial();
    return;
  }

  if (event.type === "conversation.item.input_audio_transcription.completed") {
    const itemId = event.item_id || "unknown";
    partialsByItem.delete(itemId);
    appendFinal(event.transcript || "");
    renderPartial();
    if (lastProgress.percent === 100) {
      setStatus("완료", "live");
      progressTextEl.textContent = formatProgressLabel("완료", 100);
      showTranscriptionCompletePanels();
      startButton.disabled = false;
      stopButton.disabled = true;
      runCompletionResolve?.(collectFinalTranscript());
      runCompletionResolve = undefined;
      closeActiveConnection();
      correctTranscriptButton.disabled = false;
      evaluateQualityButton.disabled = false;
      saveResultButton.disabled = false;
    }
    logEvent(`${event.type}: ${event.transcript || ""}`);
    return;
  }

  if (event.type === "error") {
    setStatus("오류", "error");
    logEvent(JSON.stringify(event, null, 2));
    return;
  }

  if (event.type?.includes("speech_started") || event.type?.includes("speech_stopped")) {
    logEvent(event.type);
  }
}

function addSilentAudioTrackForFileMode() {
  fileAudioContext = new AudioContext();
  const destination = fileAudioContext.createMediaStreamDestination();
  const gain = fileAudioContext.createGain();
  gain.gain.value = 0;

  fileSilenceSource = new ConstantSourceNode(fileAudioContext, { offset: 0 });
  fileSilenceSource.connect(gain).connect(destination);
  fileSilenceSource.start();

  fileSilenceStream = destination.stream;
  fileSilenceStream.getAudioTracks().forEach((track) => pc.addTrack(track, fileSilenceStream));
}

async function streamFileRealtime(file, currentRunId) {
  setStatus("파일 디코딩");
  showProgress("파일 디코딩", 0);
  const audioContext = new AudioContext();
  let arrayBuffer = await file.arrayBuffer();
  let audioBuffer;
  try {
    audioBuffer = await audioContext.decodeAudioData(arrayBuffer.slice(0));
  } catch (error) {
    logEvent(`file.decode.browser_failed: ${error.message || error}`);
    setStatus("서버 변환");
    showProgress("서버 변환", 0);
    arrayBuffer = await transcodeAudioForBrowser(arrayBuffer);
    showProgress("서버 변환", 100);
    try {
      audioBuffer = await audioContext.decodeAudioData(arrayBuffer.slice(0));
    } catch (transcodedError) {
      throw new Error(buildAudioDecodeErrorMessage(transcodedError));
    }
  } finally {
    await audioContext.close();
  }
  showProgress("파일 디코딩", 100);

  const chunks = audioBufferToMonoPcm16Base64Chunks(audioBuffer, {
    targetSampleRate: 24000,
    chunkMs: 100,
  });
  const commitEveryMs = 15000;
  let elapsedSinceCommitMs = 0;

  fileNameEl.textContent = file.name;
  fileMetaEl.textContent = `${audioBuffer.duration.toFixed(1)}초, ${chunks.length}개 chunk`;
  showProgress("파일 전송", 0);
  setStatus("파일 전송 중", "live");
  logEvent(`file.stream.start: ${file.name}`);

  for (let index = 0; index < chunks.length; index += 1) {
    if (currentRunId !== runId || !dc || dc.readyState !== "open") return;
    dc.send(JSON.stringify({
      type: "input_audio_buffer.append",
      audio: chunks[index].audio,
    }));
    elapsedSinceCommitMs += chunks[index].durationMs;
    showProgress("파일 전송", ((index + 1) / chunks.length) * 100);
    if (elapsedSinceCommitMs >= commitEveryMs && index < chunks.length - 1) {
      dc.send(JSON.stringify({ type: "input_audio_buffer.commit" }));
      logEvent(`file.stream.commit.partial: ${Math.round(((index + 1) / chunks.length) * 100)}%`);
      elapsedSinceCommitMs = 0;
    }
    await sleep(chunks[index].durationMs);
  }

  if (currentRunId !== runId || !dc || dc.readyState !== "open") return;
  dc.send(JSON.stringify({ type: "input_audio_buffer.commit" }));
  showProgress("분석 요청", 100);
  setStatus("분석 대기", "live");
  logEvent("file.stream.commit");
}

async function transcodeAudioForBrowser(arrayBuffer) {
  const response = await fetch("/transcode-audio", {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: arrayBuffer,
  });
  if (!response.ok) {
    const payload = await response.json().catch(() => ({}));
    throw new Error(payload.error || `Audio transcode failed: HTTP ${response.status}`);
  }
  return response.arrayBuffer();
}

function waitForDataChannelOpen(channel) {
  if (channel.readyState === "open") return Promise.resolve();
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error("Data channel open timeout. WebRTC channel did not open within 20 seconds.")), 20000);
    channel.addEventListener("open", () => {
      clearTimeout(timeout);
      resolve();
    }, { once: true });
  });
}

function syncSourceControls() {
  const isFile = sourceEl.value === "file";
  fileControlEl.classList.toggle("hidden", !isFile);
  fileModeControlEl.classList.toggle("hidden", !isFile);
  sampleControlEl.classList.add("hidden");
  sampleControlEl.hidden = true;
  fileStatusEl.hidden = !isFile || !(selectedServerAudio || audioFileEl.files?.[0]);
}

async function syncFileMeta() {
  selectedServerAudio = undefined;
  const file = audioFileEl.files?.[0];
  if (!file) {
    fileNameEl.textContent = "파일 없음";
    fileMetaEl.textContent = "";
    progressTextEl.textContent = "";
    fileStatusEl.hidden = true;
    return;
  }
  fileNameEl.textContent = file.name;
  fileMetaEl.textContent = formatBytes(file.size);
  showProgress("파일 선택", 0);
  fileStatusEl.hidden = sourceEl.value !== "file";
  logEvent(`file.selected: ${file.name}`);
  await applyAutomaticTranscriptionMode(file);
}

async function applyAutomaticTranscriptionMode(file) {
  if (!file || sourceEl.value !== "file") return;

  showProgress("채널 확인", 0);
  logEvent(`audio.inspect.start: ${file.name}`);

  try {
    const metadata = await inspectAudioMetadata(file);
    const nextMode = metadata.channels >= 2 ? "stereo_dialogue" : "file_api";
    transcriptionModeEl.value = nextMode;
    const modeLabel = nextMode === "stereo_dialogue" ? "스테레오 화자분리" : "파일 전사 API";
    showProgress(`${metadata.channels}채널 감지`, 100);
    logEvent(`audio.inspect.ok: channels=${metadata.channels} layout=${metadata.channelLayout || "unknown"} auto_mode=${modeLabel}`);
  } catch (error) {
    showProgress("채널 확인 실패", 0);
    logEvent(`audio.inspect.error: ${error.message || error}`);
  }
}

async function inspectAudioMetadata(file) {
  const arrayBuffer = await file.arrayBuffer();
  const response = await fetch("/inspect-audio", {
    method: "POST",
    headers: {
      "Content-Type": file.type || "application/octet-stream",
      "X-Audio-File-Name": encodeURIComponent(file.name || "audio"),
    },
    body: arrayBuffer,
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload.error || `Audio inspect failed: HTTP ${response.status}`);
  }
  return payload;
}

async function loadServerSample(options = {}) {
  const samplePath = samplePathEl.value.trim();
  if (!samplePath) {
    setStatus("경로 필요", "error");
    logEvent("서버 샘플 경로를 입력하세요.");
    return false;
  }

  setStatus("샘플 로드");
  loadSampleButton.disabled = true;
  loadSampleButton.textContent = "로드 중";
  fileNameEl.textContent = "서버 샘플";
  fileMetaEl.textContent = samplePath;
  showProgress("샘플 요청", 0);
  setProgressIndeterminate(true);
  logEvent(`${options.auto ? "sample.auto_load.start" : "sample.load.start"}: ${samplePath}`);
  try {
    const { blob, fileName } = await loadAudioBlobWithProgress(
      `/local-audio?path=${encodeURIComponent(samplePath)}`,
      samplePath,
    );
    selectedServerAudio = {
      name: fileName,
      size: blob.size,
      type: blob.type,
      arrayBuffer: () => blob.arrayBuffer(),
    };
    audioFileEl.value = "";
    fileNameEl.textContent = fileName;
    fileMetaEl.textContent = `${formatBytes(blob.size)} / sample`;
    showProgress("샘플 준비", 100);
    setStatus("샘플 준비");
    logEvent(`sample.loaded: ${samplePath}`);
    pendingAudioInspection = applyAutomaticTranscriptionMode(selectedServerAudio);
    await pendingAudioInspection;
    return true;
  } catch (error) {
    setProgressIndeterminate(false);
    setStatus("오류", "error");
    logEvent(error.stack || error.message || String(error));
    return false;
  } finally {
    setProgressIndeterminate(false);
    loadSampleButton.disabled = false;
    loadSampleButton.textContent = "샘플 불러오기";
  }
}

function loadAudioBlobWithProgress(url, samplePath) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open("GET", url, true);
    xhr.responseType = "blob";

    xhr.onprogress = (event) => {
      if (event.lengthComputable) {
        showProgress("샘플 로드", (event.loaded / event.total) * 100);
      } else {
        progressTextEl.textContent = "샘플 로드 중";
      }
    };

    xhr.onload = () => {
      if (xhr.status < 200 || xhr.status >= 300) {
        reject(new Error(xhr.response?.text || `Sample load failed: HTTP ${xhr.status}`));
        return;
      }

      const encodedName = xhr.getResponseHeader("X-Audio-File-Name");
      resolve({
        blob: xhr.response,
        fileName: encodedName ? decodeURIComponent(encodedName) : samplePath.split("/").pop(),
      });
    };

    xhr.onerror = () => reject(new Error("Sample load network error."));
    xhr.onabort = () => reject(new Error("Sample load aborted."));
    xhr.send();
  });
}

function renderPartial() {
  partialEl.textContent = Array.from(partialsByItem.values()).join("\n");
}

function appendFinal(text) {
  const message = parseTranscriptLine(text);
  appendTranscriptMessage(finalsEl, message, { storeTranscript: true });
}

function appendFinalMessage(message) {
  appendTranscriptMessage(finalsEl, message, { storeTranscript: true });
}

function appendTranscriptMessage(container, message, options = {}) {
  if (!message.text) return;
  const item = document.createElement("article");
  item.className = `chat-message ${message.role}`;
  if (options.storeTranscript) {
    item.dataset.transcriptText = renderTranscriptLine(message);
  }

  const meta = document.createElement("div");
  meta.className = "chat-meta";
  meta.textContent = message.time ? `${message.label} · ${message.time}` : message.label;

  const bubble = document.createElement("div");
  bubble.className = "chat-bubble";
  bubble.textContent = message.text;

  item.append(meta, bubble);
  container.appendChild(item);
  container.scrollTop = container.scrollHeight;
}

function renderTranscriptMessages(container, transcript, options = {}) {
  container.innerHTML = "";
  splitTranscriptLines(transcript).forEach((line) => {
    appendTranscriptMessage(container, parseTranscriptLine(line), options);
  });
}

function splitTranscriptLines(transcript) {
  return String(transcript || "")
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
}

async function requestAiCorrection() {
  const transcript = collectFinalTranscript();
  if (!transcript) return;

  const provider = correctionProviderEl.value;
  const label = provider === "claude" ? "Claude Sonnet 4.6" : "Gemini";
  setStatus("AI 보정 중", "live");
  setPanelExpanded(correctionPanelEl, true);
  correctTranscriptButton.disabled = true;
  renderCorrectionResults();
  appendTranscriptMessage(correctionEl, {
    role: "unknown",
    label,
    time: "",
    text: "보정 중...",
  });
  logEvent(`correction.start: ${provider}`);

  try {
    const response = await fetch("/correct-transcript", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ transcript, provider }),
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      throw new Error(payload.error || `AI correction failed: HTTP ${response.status}`);
    }
    correctionResults[provider] = payload.corrected || "";
    renderCorrectionResults();
    setStatus("보정 완료", "live");
    saveResultButton.disabled = false;
    logEvent(`correction.ok: ${provider}`);
  } catch (error) {
    setStatus("보정 오류", "error");
    renderCorrectionResults();
    appendTranscriptMessage(correctionEl, {
      role: "unknown",
      label,
      time: "",
      text: error.message || String(error),
    });
    logEvent(`correction.error: ${error.message || error}`);
  } finally {
    correctTranscriptButton.disabled = false;
  }
}

async function requestQualityEvaluation() {
  const transcript = collectFinalTranscript();
  if (!transcript) return;

  const provider = correctionProviderEl.value;
  const label = provider === "claude" ? "Claude Sonnet 4.6" : "Gemini";
  setStatus("품질 평가 중", "live");
  setPanelExpanded(qualityPanelEl, true);
  evaluateQualityButton.disabled = true;
  const localEvaluation = evaluateTranscriptQualityLocally(transcript, qualityRuleSettings);
  qualityEvaluationResult = formatLocalQualityEvaluation(localEvaluation);
  qualityEvaluationEl.textContent = `${qualityEvaluationResult}\n\n[${label}] AI 참고 의견 생성 중...`;
  logEvent(`quality.start: ${provider}`);

  try {
    const response = await fetch("/evaluate-quality", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        transcript,
        provider,
        includeAiNote: true,
        settings: qualityRuleSettings,
      }),
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      throw new Error(payload.error || `Quality evaluation failed: HTTP ${response.status}`);
    }
    qualityEvaluationResult = payload.evaluation || "";
    qualityEvaluationEl.textContent = qualityEvaluationResult;
    setStatus("평가 완료", "live");
    saveResultButton.disabled = false;
    logEvent(`quality.ok: ${provider}`);
  } catch (error) {
    setStatus("평가 오류", "error");
    qualityEvaluationEl.textContent = `[${label}] ${error.message || error}`;
    logEvent(`quality.error: ${error.message || error}`);
  } finally {
    evaluateQualityButton.disabled = false;
  }
}

async function loadApiKeyStatus() {
  try {
    const response = await fetch("/api-keys");
    const payload = await response.json();
    if (!response.ok) {
      throw new Error(payload.error || `API key status failed: HTTP ${response.status}`);
    }
    renderApiKeyStatus(payload);
  } catch (error) {
    apiKeyStatusEl.textContent = `키 상태 확인 실패: ${error.message || error}`;
  }
}

async function saveApiKeys() {
  saveApiKeysButton.disabled = true;
  apiKeyStatusEl.textContent = "암호화 저장 중...";

  try {
    const response = await fetch("/api-keys", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        openai: openaiKeyEl.value,
        anthropic: anthropicKeyEl.value,
        gemini: geminiKeyEl.value,
      }),
    });
    const payload = await response.json();
    if (!response.ok) {
      throw new Error(payload.error || `API key save failed: HTTP ${response.status}`);
    }
    openaiKeyEl.value = "";
    anthropicKeyEl.value = "";
    geminiKeyEl.value = "";
    renderApiKeyStatus(payload, "저장 완료");
    logEvent("api_keys.saved");
  } catch (error) {
    apiKeyStatusEl.textContent = `저장 실패: ${error.message || error}`;
    logEvent(`api_keys.error: ${error.message || error}`);
  } finally {
    saveApiKeysButton.disabled = false;
  }
}

function renderApiKeyStatus(status, prefix = "") {
  const labels = {
    openai: "OpenAI",
    anthropic: "Claude",
    gemini: "Gemini",
  };
  const sourceLabels = {
    stored: "암호화 저장됨",
    env: ".env 사용 중",
    missing: "없음",
  };
  const text = ["openai", "anthropic", "gemini"]
    .map((name) => `${labels[name]}: ${sourceLabels[status?.[name]?.source] || "없음"}`)
    .join(" / ");
  apiKeyStatusEl.textContent = prefix ? `${prefix} - ${text}` : text;
}

function loadQualityRuleSettings() {
  try {
    const saved = JSON.parse(localStorage.getItem(QUALITY_RULE_SETTINGS_KEY) || "null");
    return normalizeQualityRuleSettings(saved || DEFAULT_QUALITY_RULE_SETTINGS);
  } catch {
    return normalizeQualityRuleSettings(DEFAULT_QUALITY_RULE_SETTINGS);
  }
}

function saveQualityRuleSettings() {
  qualityRuleSettings = normalizeQualityRuleSettings(qualityRuleSettings);
  localStorage.setItem(QUALITY_RULE_SETTINGS_KEY, JSON.stringify(qualityRuleSettings));
}

function resetQualityRuleSettings() {
  qualityRuleSettings = normalizeQualityRuleSettings(DEFAULT_QUALITY_RULE_SETTINGS);
  saveQualityRuleSettings();
  renderQualityRuleSettings();
}

function renderQualityRuleSettings() {
  const catalog = getQualityRuleCatalog();
  qualityRuleSettingsEl.innerHTML = "";

  Object.entries(catalog).forEach(([, category]) => {
    const categoryEl = document.createElement("section");
    categoryEl.className = "quality-rule-category";

    const title = document.createElement("div");
    title.className = "quality-rule-category-title";
    title.textContent = category.label;
    categoryEl.appendChild(title);

    Object.values(category.items).forEach((rule) => {
      categoryEl.appendChild(createQualityRuleRow(rule));
    });

    qualityRuleSettingsEl.appendChild(categoryEl);
  });

  updateQualityRuleTotal();
}

function createQualityRuleRow(rule) {
  const setting = qualityRuleSettings.rules[rule.id] || {
    enabled: true,
    score: rule.defaultScore,
  };
  const row = document.createElement("div");
  row.className = "quality-rule-row";

  const label = document.createElement("label");
  const checkbox = document.createElement("input");
  checkbox.type = "checkbox";
  checkbox.checked = setting.enabled;
  checkbox.addEventListener("change", () => {
    updateQualityRule(rule.id, { enabled: checkbox.checked });
  });
  const labelText = document.createElement("span");
  labelText.textContent = rule.label;
  label.append(checkbox, labelText);

  const score = document.createElement("input");
  score.type = "number";
  score.min = "0";
  score.max = "100";
  score.step = "1";
  score.value = setting.score;
  score.setAttribute("aria-label", `${rule.label} 배점`);
  score.addEventListener("input", () => {
    updateQualityRule(rule.id, { score: score.value });
  });

  row.append(label, score);
  return row;
}

function updateQualityRule(ruleId, patch) {
  qualityRuleSettings = normalizeQualityRuleSettings({
    ...qualityRuleSettings,
    rules: {
      ...qualityRuleSettings.rules,
      [ruleId]: {
        ...qualityRuleSettings.rules[ruleId],
        ...patch,
      },
    },
  });
  saveQualityRuleSettings();
  updateQualityRuleTotal();
}

function updateQualityRuleTotal() {
  const total = Object.values(qualityRuleSettings.rules)
    .filter((rule) => rule.enabled)
    .reduce((sum, rule) => sum + Number(rule.score || 0), 0);
  qualityRuleTotalEl.textContent = `활성 총 배점 ${total}`;
}

function setPanelExpanded(panel, expanded) {
  if (!panel) return;
  panel.classList.toggle("is-collapsed", !expanded);
  const button = panel.querySelector("[data-target-panel]");
  if (button) {
    button.setAttribute("aria-expanded", String(expanded));
    button.textContent = expanded ? "접기" : "펼치기";
  }
}

function collapseWorkPanels() {
  [
    eventsPanelEl,
    partialPanelEl,
    finalsPanelEl,
    correctionPanelEl,
    qualityPanelEl,
    comparisonPanelEl,
  ].forEach((panel) => setPanelExpanded(panel, false));
}

function showTranscriptionPanels() {
  setPanelExpanded(eventsPanelEl, true);
  setPanelExpanded(partialPanelEl, true);
  setPanelExpanded(finalsPanelEl, true);
  setPanelExpanded(correctionPanelEl, false);
  setPanelExpanded(qualityPanelEl, false);
}

function showTranscriptionCompletePanels() {
  setPanelExpanded(eventsPanelEl, false);
  setPanelExpanded(partialPanelEl, false);
  setPanelExpanded(finalsPanelEl, true);
}

function renderCorrectionResults() {
  const labels = {
    claude: "Claude Sonnet 4.6",
    gemini: "Gemini",
  };
  correctionEl.innerHTML = "";
  Object.entries(correctionResults).forEach(([provider, text]) => {
    appendGroupLabel(correctionEl, labels[provider] || provider);
    renderTranscriptMessages(correctionEl, text);
  });
}

function appendGroupLabel(container, label) {
  const item = document.createElement("div");
  item.className = "transcript-group-label";
  item.textContent = label;
  container.appendChild(item);
}

function collectFinalTranscript() {
  return Array.from(finalsEl.querySelectorAll("[data-transcript-text]"))
    .map((item) => item.dataset.transcriptText.trim())
    .filter(Boolean)
    .join("\n");
}

function renderTranscriptLine(message) {
  const prefix = message.time ? `[${message.time}] ${message.label}:` : `${message.label}:`;
  return message.role === "unknown" ? message.text : `${prefix} ${message.text}`;
}

function saveResultJson() {
  try {
    const data = buildResultExport({
      createdAt: new Date().toISOString(),
      file: {
        name: fileNameEl.textContent,
        size: selectedServerAudio?.size || audioFileEl.files?.[0]?.size || 0,
      },
      options: {
        source: sourceEl.value,
        transcriptionMode: transcriptionModeEl.value,
        model: document.querySelector("#model").value,
        language: document.querySelector("#language").value.trim() || "ko",
        noiseReduction: noiseReductionEl.value,
        prompt: document.querySelector("#prompt").value.trim(),
        correctionProvider: correctionProviderEl.value,
      },
      transcript: collectFinalTranscript(),
      corrections: correctionResults,
      qualityEvaluation: qualityEvaluationResult,
      qualityRuleSettings,
      comparison: comparisonEl.textContent,
      events: eventsEl.textContent,
    });
    const baseName = (data.file.name || "stt-result").replace(/\.[^.]+$/, "");
    downloadJsonFile({
      documentRef: document,
      objectUrlFactory: URL.createObjectURL.bind(URL),
      revokeObjectUrl: URL.revokeObjectURL.bind(URL),
      filename: `${baseName}-stt-result.json`,
      data,
    });
    logEvent("result.export.ok");
  } catch (error) {
    setStatus("저장 오류", "error");
    logEvent(`result.export.error: ${error.message || error}`);
  }
}

function stop() {
  runId += 1;
  dc?.close();
  pc?.close();
  micStream?.getTracks().forEach((track) => track.stop());
  fileSilenceStream?.getTracks().forEach((track) => track.stop());
  fileSilenceSource?.stop();
  fileAudioContext?.close();
  clearConnectionRefs();
  startButton.disabled = false;
  stopButton.disabled = true;
  if (!statusEl.classList.contains("error")) {
    if (lastProgress.step && lastProgress.percent > 0 && lastProgress.percent < 100) {
      progressTextEl.textContent = formatProgressLabel("중지됨", lastProgress.percent);
    }
    setStatus("중지됨");
  }
  runCompletionResolve?.(collectFinalTranscript());
  runCompletionResolve = undefined;
}

function closeActiveConnection() {
  dc?.close();
  pc?.close();
  micStream?.getTracks().forEach((track) => track.stop());
  fileSilenceStream?.getTracks().forEach((track) => track.stop());
  try {
    fileSilenceSource?.stop();
  } catch {
    // Already stopped.
  }
  fileAudioContext?.close();
  clearConnectionRefs();
}

function clearConnectionRefs() {
  dc = undefined;
  pc = undefined;
  micStream = undefined;
  fileAudioContext = undefined;
  fileSilenceSource = undefined;
  fileSilenceStream = undefined;
}

function setStatus(text, state = "") {
  statusEl.textContent = text;
  statusEl.className = `status ${state}`.trim();
}

function logEvent(text) {
  const stamp = new Date().toLocaleTimeString();
  eventsEl.textContent = `[${stamp}] ${text}\n${eventsEl.textContent}`;
}

function showProgress(step, percent) {
  fileStatusEl.hidden = false;
  const safePercent = Math.max(0, Math.min(100, Math.round(Number(percent) || 0)));
  lastProgress = { step, percent: safePercent };
  setProgressIndeterminate(false);
  fileProgressEl.value = safePercent;
  progressTextEl.textContent = formatProgressLabel(step, safePercent);
}

function setProgressIndeterminate(isIndeterminate) {
  if (isIndeterminate) {
    fileProgressEl.removeAttribute("value");
  } else if (!fileProgressEl.hasAttribute("value")) {
    fileProgressEl.value = lastProgress.percent || 0;
  }
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

async function compareNoiseReduction() {
  const file = selectedServerAudio || audioFileEl.files?.[0];
  if (!file) {
    setStatus("파일 필요", "error");
    logEvent("노이즈 비교를 위해 오디오 파일을 선택하세요.");
    return;
  }

  const modes = ["off", "near_field", "far_field"];
  const results = [];
  const previousTranscriptionMode = transcriptionModeEl.value;
  isNoiseComparing = true;
  compareNoiseButton.disabled = true;
  correctionEl.textContent = "";
  comparisonPanelEl.hidden = false;
  setPanelExpanded(comparisonPanelEl, true);
  comparisonEl.textContent = "노이즈 감소 비교 시작\n";

  try {
    transcriptionModeEl.value = "realtime";
    for (const mode of modes) {
      noiseReductionEl.value = mode;
      comparisonEl.textContent += `\n[${mode}] 전사 시작...\n`;
      setStatus(`비교 중: ${mode}`, "live");
      logEvent(`noise.compare.start: ${mode}`);
      const transcript = await withTimeout(startTranscription(), 210000, `${mode} 비교 전사 시간 초과`);
      if (!transcript) {
        throw new Error(`${mode} 비교 전사 결과가 비어 있습니다.`);
      }
      results.push({ mode, transcript });
      comparisonEl.textContent += `[${mode}] 완료 (${transcript.length}자)\n${transcript}\n`;
      await sleep(1500);
    }
    setStatus("비교 완료", "live");
    comparisonEl.textContent += "\n=== 비교 완료 ===\n";
    logEvent("noise.compare.ok");
  } catch (error) {
    setStatus("비교 오류", "error");
    comparisonEl.textContent += `\n비교 오류: ${error.message || error}\n`;
    logEvent(`noise.compare.error: ${error.message || error}`);
  } finally {
    isNoiseComparing = false;
    transcriptionModeEl.value = previousTranscriptionMode;
    compareNoiseButton.disabled = false;
    startButton.disabled = false;
    stopButton.disabled = true;
  }
}

function withTimeout(promise, timeoutMs, message) {
  return Promise.race([
    promise,
    new Promise((_, reject) => setTimeout(() => reject(new Error(message)), timeoutMs)),
  ]);
}
