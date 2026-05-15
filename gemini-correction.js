export const DEFAULT_GEMINI_MODEL = "gemini-2.5-flash";
export const DEFAULT_CLAUDE_MODEL = "claude-sonnet-4-6";
export const DEFAULT_AI_TIMEOUT_MS = 180000;
export const DEFAULT_CORRECTION_CHUNK_CHARACTERS = 1600;

export function buildCorrectionPrompt(transcript) {
  return [
    "다음은 한국어 통화 녹취의 자동 STT 결과입니다.",
    "의미를 바꾸지 말고, 잘못 인식된 어절과 문장부호만 자연스럽게 보정하세요.",
    "화자 라벨(고객, 상담원)과 대화 순서는 유지하세요.",
    "상담 맥락의 구어체는 유지하고, 없는 정보를 추가하지 마세요.",
    "결과는 보정된 대화문만 출력하세요.",
    "",
    transcript.trim(),
  ].join("\n");
}

export function buildQualityEvaluationPrompt(transcript) {
  return [
    "다음은 한국어 상담 통화 녹취입니다.",
    "상담 품질을 운영자가 바로 확인할 수 있게 평가하세요.",
    "다음 형식만 사용하세요.",
    "",
    "종합 점수: 0-100",
    "요약: 한 문장",
    "강점:",
    "- 항목",
    "개선 필요:",
    "- 항목",
    "확인 필요:",
    "- 항목",
    "",
    transcript.trim(),
  ].join("\n");
}

export function extractGeminiText(payload) {
  return payload?.candidates
    ?.flatMap((candidate) => candidate.content?.parts || [])
    ?.map((part) => part.text || "")
    ?.join("")
    ?.trim() || "";
}

export async function correctTranscriptWithGemini({
  apiKey,
  transcript,
  model = DEFAULT_GEMINI_MODEL,
  fetchImpl = fetch,
  timeoutMs = DEFAULT_AI_TIMEOUT_MS,
  maxChunkCharacters = DEFAULT_CORRECTION_CHUNK_CHARACTERS,
}) {
  if (!apiKey) {
    throw new Error("GEMINI_API_KEY is not set.");
  }

  const cleanTranscript = String(transcript || "").trim();
  if (!cleanTranscript) {
    throw new Error("Transcript is empty.");
  }

  const chunks = splitTranscriptIntoCorrectionChunks(cleanTranscript, maxChunkCharacters);
  const correctedChunks = [];
  for (const chunk of chunks) {
    correctedChunks.push(await requestGeminiText({
      apiKey,
      model,
      prompt: buildCorrectionPrompt(chunk),
      fetchImpl,
      timeoutMs,
      errorLabel: "correction",
      emptyLabel: "correction",
    }));
  }

  return correctedChunks.join("\n").trim();
}

export async function evaluateTranscriptQualityWithGemini({
  apiKey,
  transcript,
  model = DEFAULT_GEMINI_MODEL,
  fetchImpl = fetch,
}) {
  if (!apiKey) {
    throw new Error("GEMINI_API_KEY is not set.");
  }

  const cleanTranscript = String(transcript || "").trim();
  if (!cleanTranscript) {
    throw new Error("Transcript is empty.");
  }

  const response = await fetchImpl(
    `https://generativelanguage.googleapis.com/v1beta/models/${encodeURIComponent(model)}:generateContent`,
    {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "x-goog-api-key": apiKey,
      },
      body: JSON.stringify({
        contents: [
          {
            role: "user",
            parts: [{ text: buildQualityEvaluationPrompt(cleanTranscript) }],
          },
        ],
        generationConfig: {
          temperature: 0.2,
        },
      }),
      signal: AbortSignal.timeout(DEFAULT_AI_TIMEOUT_MS),
    },
  );

  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload?.error?.message || `Gemini quality evaluation failed: HTTP ${response.status}`);
  }

  const evaluation = extractGeminiText(payload);
  if (!evaluation) {
    throw new Error("Gemini returned an empty quality evaluation.");
  }

  return evaluation;
}

export function extractClaudeText(payload) {
  return payload?.content
    ?.map((part) => part.type === "text" ? part.text || "" : "")
    ?.join("")
    ?.trim() || "";
}

export async function correctTranscriptWithClaude({
  apiKey,
  transcript,
  model = DEFAULT_CLAUDE_MODEL,
  fetchImpl = fetch,
  timeoutMs = DEFAULT_AI_TIMEOUT_MS,
  maxChunkCharacters = DEFAULT_CORRECTION_CHUNK_CHARACTERS,
}) {
  if (!apiKey) {
    throw new Error("ANTHROPIC_API_KEY is not set.");
  }

  const cleanTranscript = String(transcript || "").trim();
  if (!cleanTranscript) {
    throw new Error("Transcript is empty.");
  }

  const chunks = splitTranscriptIntoCorrectionChunks(cleanTranscript, maxChunkCharacters);
  const correctedChunks = [];
  for (const chunk of chunks) {
    correctedChunks.push(await requestClaudeText({
      apiKey,
      model,
      prompt: buildCorrectionPrompt(chunk),
      fetchImpl,
      timeoutMs,
      errorLabel: "correction",
      emptyLabel: "correction",
    }));
  }

  return correctedChunks.join("\n").trim();
}

export async function evaluateTranscriptQualityWithClaude({
  apiKey,
  transcript,
  model = DEFAULT_CLAUDE_MODEL,
  fetchImpl = fetch,
}) {
  if (!apiKey) {
    throw new Error("ANTHROPIC_API_KEY is not set.");
  }

  const cleanTranscript = String(transcript || "").trim();
  if (!cleanTranscript) {
    throw new Error("Transcript is empty.");
  }

  const response = await fetchImpl("https://api.anthropic.com/v1/messages", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "x-api-key": apiKey,
      "anthropic-version": "2023-06-01",
    },
    body: JSON.stringify({
      model,
      max_tokens: 4096,
      temperature: 0.2,
      messages: [
        {
          role: "user",
          content: buildQualityEvaluationPrompt(cleanTranscript),
        },
      ],
    }),
    signal: AbortSignal.timeout(DEFAULT_AI_TIMEOUT_MS),
  });

  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload?.error?.message || `Claude quality evaluation failed: HTTP ${response.status}`);
  }

  const evaluation = extractClaudeText(payload);
  if (!evaluation) {
    throw new Error("Claude returned an empty quality evaluation.");
  }

  return evaluation;
}

export function splitTranscriptIntoCorrectionChunks(transcript, maxCharacters = DEFAULT_CORRECTION_CHUNK_CHARACTERS) {
  const limit = Math.max(20, Number(maxCharacters) || DEFAULT_CORRECTION_CHUNK_CHARACTERS);
  const lines = String(transcript || "")
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
  const chunks = [];
  let current = "";

  for (const line of lines) {
    if (line.length > limit) {
      if (current) {
        chunks.push(current);
        current = "";
      }
      for (let index = 0; index < line.length; index += limit) {
        chunks.push(line.slice(index, index + limit));
      }
      continue;
    }

    const next = current ? `${current}\n${line}` : line;
    if (next.length > limit && current) {
      chunks.push(current);
      current = line;
    } else {
      current = next;
    }
  }

  if (current) chunks.push(current);
  return chunks.length ? chunks : [String(transcript || "").trim()].filter(Boolean);
}

async function requestGeminiText({
  apiKey,
  model,
  prompt,
  fetchImpl,
  timeoutMs,
  errorLabel,
  emptyLabel,
}) {
  const response = await fetchImpl(
    `https://generativelanguage.googleapis.com/v1beta/models/${encodeURIComponent(model)}:generateContent`,
    {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "x-goog-api-key": apiKey,
      },
      body: JSON.stringify({
        contents: [
          {
            role: "user",
            parts: [{ text: prompt }],
          },
        ],
        generationConfig: {
          temperature: 0.2,
        },
      }),
      signal: AbortSignal.timeout(timeoutMs),
    },
  );

  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload?.error?.message || `Gemini ${errorLabel} failed: HTTP ${response.status}`);
  }

  const text = extractGeminiText(payload);
  if (!text) {
    throw new Error(`Gemini returned an empty ${emptyLabel}.`);
  }
  return text;
}

async function requestClaudeText({
  apiKey,
  model,
  prompt,
  fetchImpl,
  timeoutMs,
  errorLabel,
  emptyLabel,
}) {
  const response = await fetchImpl("https://api.anthropic.com/v1/messages", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "x-api-key": apiKey,
      "anthropic-version": "2023-06-01",
    },
    body: JSON.stringify({
      model,
      max_tokens: 4096,
      temperature: 0.2,
      messages: [
        {
          role: "user",
          content: prompt,
        },
      ],
    }),
    signal: AbortSignal.timeout(timeoutMs),
  });

  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload?.error?.message || `Claude ${errorLabel} failed: HTTP ${response.status}`);
  }

  const text = extractClaudeText(payload);
  if (!text) {
    throw new Error(`Claude returned an empty ${emptyLabel}.`);
  }
  return text;
}
