import assert from "node:assert/strict";
import test from "node:test";

import {
  buildResultExport,
  downloadJsonFile,
  normalizeCorrections,
} from "../public/result-utils.js";

test("buildResultExport preserves file, options, transcript, corrections, events, and comparison exactly", () => {
  const result = buildResultExport({
    createdAt: "2026-05-13T00:00:00.000Z",
    file: { name: "call.mp3", size: 1234 },
    options: {
      source: "file",
      model: "gpt-realtime-whisper",
      language: "ko",
      noiseReduction: "off",
      prompt: "",
      correctionProvider: "claude",
    },
    transcript: " 여보세요\n안녕하세요 ",
    corrections: {
      claude: "보정 결과",
      gemini: " ",
    },
    qualityEvaluation: "종합 점수: 85",
    qualityRuleSettings: { rules: { "basic.greeting": { enabled: true, score: 5 } } },
    comparison: "off wins",
    events: "[00:00] done",
  });

  assert.deepEqual(result, {
    schemaVersion: 1,
    createdAt: "2026-05-13T00:00:00.000Z",
    file: { name: "call.mp3", size: 1234 },
    options: {
      source: "file",
      model: "gpt-realtime-whisper",
      language: "ko",
      noiseReduction: "off",
      prompt: "",
      correctionProvider: "claude",
    },
    transcript: "여보세요\n안녕하세요",
    corrections: {
      claude: "보정 결과",
    },
    qualityEvaluation: "종합 점수: 85",
    qualityRuleSettings: { rules: { "basic.greeting": { enabled: true, score: 5 } } },
    comparison: "off wins",
    events: "[00:00] done",
  });
});

test("buildResultExport rejects empty transcripts", () => {
  assert.throws(
    () => buildResultExport({ transcript: "   " }),
    /Transcript is required/,
  );
});

test("normalizeCorrections removes blank provider results and trims values", () => {
  assert.deepEqual(normalizeCorrections({
    claude: "  A  ",
    gemini: "",
    other: null,
  }), {
    claude: "A",
  });
});

test("downloadJsonFile writes a formatted JSON blob, clicks the link, then revokes the URL", async () => {
  let appendedLink;
  let clicked = false;
  let removed = false;
  let revokedUrl = "";
  let capturedBlob;

  const fakeDocument = {
    body: {
      appendChild(link) {
        appendedLink = link;
      },
    },
    createElement(tagName) {
      assert.equal(tagName, "a");
      return {
        href: "",
        download: "",
        click() {
          clicked = true;
        },
        remove() {
          removed = true;
        },
      };
    },
  };

  downloadJsonFile({
    documentRef: fakeDocument,
    objectUrlFactory(blob) {
      capturedBlob = blob;
      return "blob:result-json";
    },
    revokeObjectUrl(url) {
      revokedUrl = url;
    },
    filename: "call-stt-result.json",
    data: {
      transcript: "테스트",
      corrections: { claude: "보정" },
    },
  });

  assert.equal(appendedLink.href, "blob:result-json");
  assert.equal(appendedLink.download, "call-stt-result.json");
  assert.equal(clicked, true);
  assert.equal(removed, true);
  assert.equal(revokedUrl, "blob:result-json");
  assert.equal(capturedBlob.type, "application/json");
  assert.equal(
    await capturedBlob.text(),
    '{\n  "transcript": "테스트",\n  "corrections": {\n    "claude": "보정"\n  }\n}',
  );
});
