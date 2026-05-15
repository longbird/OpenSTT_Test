import assert from "node:assert/strict";
import test from "node:test";

import {
  buildCorrectionPrompt,
  buildQualityEvaluationPrompt,
  correctTranscriptWithClaude,
  correctTranscriptWithGemini,
  extractClaudeText,
  extractGeminiText,
  splitTranscriptIntoCorrectionChunks,
} from "../gemini-correction.js";

test("buildCorrectionPrompt includes Korean correction rules and transcript", () => {
  const prompt = buildCorrectionPrompt("여보세요? 어디 안녕하세요?");

  assert.match(prompt, /의미를 바꾸지 말고/);
  assert.match(prompt, /여보세요\? 어디 안녕하세요\?/);
});

test("buildQualityEvaluationPrompt asks for counseling quality score", () => {
  const prompt = buildQualityEvaluationPrompt("상담원: 안녕하세요.\n고객: 문의드립니다.");

  assert.match(prompt, /상담 품질/);
  assert.match(prompt, /종합 점수: 0-100/);
  assert.match(prompt, /상담원: 안녕하세요/);
});

test("extractGeminiText reads concatenated response parts", () => {
  const text = extractGeminiText({
    candidates: [
      { content: { parts: [{ text: "보정 " }, { text: "결과" }] } },
    ],
  });

  assert.equal(text, "보정 결과");
});

test("correctTranscriptWithGemini rejects missing api key", async () => {
  await assert.rejects(
    () => correctTranscriptWithGemini({ apiKey: "", transcript: "테스트" }),
    /GEMINI_API_KEY/,
  );
});

test("splitTranscriptIntoCorrectionChunks keeps correction requests bounded", () => {
  const chunks = splitTranscriptIntoCorrectionChunks([
    "상담원: 안녕하세요.",
    `고객: ${"문의".repeat(30)}`,
    "상담원: 확인해드리겠습니다.",
  ].join("\n"), 40);

  assert.equal(chunks.length > 1, true);
  assert.equal(chunks.every((chunk) => chunk.length <= 40), true);
});

test("correctTranscriptWithGemini corrects long transcripts in chunks", async () => {
  const prompts = [];
  const corrected = await correctTranscriptWithGemini({
    apiKey: "gemini-key",
    transcript: [
      "상담원: 안녕하세요.",
      `고객: ${"문의".repeat(30)}`,
      "상담원: 확인해드리겠습니다.",
    ].join("\n"),
    maxChunkCharacters: 40,
    fetchImpl: async (_url, init) => {
      prompts.push(JSON.parse(init.body).contents[0].parts[0].text);
      return {
        ok: true,
        json: async () => ({
          candidates: [{ content: { parts: [{ text: `보정${prompts.length}` }] } }],
        }),
      };
    },
  });

  assert.equal(prompts.length > 1, true);
  assert.equal(corrected, prompts.map((_, index) => `보정${index + 1}`).join("\n"));
});

test("extractClaudeText reads Anthropic text blocks", () => {
  const text = extractClaudeText({
    content: [
      { type: "text", text: "Claude " },
      { type: "text", text: "보정" },
    ],
  });

  assert.equal(text, "Claude 보정");
});

test("correctTranscriptWithClaude rejects missing api key", async () => {
  await assert.rejects(
    () => correctTranscriptWithClaude({ apiKey: "", transcript: "테스트" }),
    /ANTHROPIC_API_KEY/,
  );
});
