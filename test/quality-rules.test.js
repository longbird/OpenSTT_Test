import assert from "node:assert/strict";
import test from "node:test";

import {
  DEFAULT_QUALITY_RULE_SETTINGS,
  evaluateTranscriptQualityLocally,
  formatLocalQualityEvaluation,
  normalizeQualityRuleSettings,
} from "../public/quality-rules.js";

test("evaluateTranscriptQualityLocally scores a complete dispatch counseling transcript", () => {
  const result = evaluateTranscriptQualityLocally([
    "상담원: 안녕하세요 대리운전입니다.",
    "고객: 성북구에서 강남까지 가려고요.",
    "상담원: 현재 5만원 배차가 어렵고 6만원부터 알아봐드리겠습니다.",
    "상담원: 배차 안 되시면 다시 연락드려도 괜찮으실까요?",
    "고객: 네 그렇게 해주세요.",
    "상담원: 네 감사합니다.",
  ].join("\n"));

  assert.equal(result.totalScore >= 80, true);
  assert.equal(result.confidence.level, "높음");
  assert.equal(result.totalScore, 100);
  assert.equal(result.categories.basic.score, result.categories.basic.maxScore);
  assert.equal(result.categories.business.items.fareGuide.passed, true);
  assert.equal(result.categories.business.items.followUp.passed, true);
});

test("evaluateTranscriptQualityLocally lowers confidence when speaker separation failed", () => {
  const result = evaluateTranscriptQualityLocally([
    "전사: 안녕하세요.",
    "전사: 여기 5만원 배차가 안 되면 6만원부터 알아보겠습니다.",
  ].join("\n"));

  assert.equal(result.confidence.level, "낮음");
  assert.equal(result.categories.reliability.items.speakerSeparation.passed, false);
  assert.equal(result.warnings.some((warning) => warning.includes("화자분리")), true);
});

test("formatLocalQualityEvaluation renders score, evidence, and AI helper note", () => {
  const result = evaluateTranscriptQualityLocally("상담원: 안녕하세요.\n고객: 문의드립니다.");
  const text = formatLocalQualityEvaluation(result, "AI 참고 의견입니다.");

  assert.match(text, /로컬 평가 점수:/);
  assert.match(text, /평가 신뢰도:/);
  assert.match(text, /근거:/);
  assert.match(text, /AI 참고 의견/);
});

test("evaluateTranscriptQualityLocally applies custom rule weights and disabled items", () => {
  const settings = normalizeQualityRuleSettings({
    ...DEFAULT_QUALITY_RULE_SETTINGS,
    rules: {
      ...DEFAULT_QUALITY_RULE_SETTINGS.rules,
      "basic.greeting": { enabled: false, score: 5 },
      "business.fareGuide": { enabled: true, score: 30 },
    },
  });

  const result = evaluateTranscriptQualityLocally([
    "상담원: 안녕하세요 대리운전입니다.",
    "고객: 성북구에서 강남까지 가려고요.",
    "상담원: 현재 5만원 배차가 어렵고 6만원부터 알아봐드리겠습니다.",
    "상담원: 배차 안 되시면 다시 연락드려도 괜찮으실까요?",
    "고객: 네 그렇게 해주세요.",
    "상담원: 네 감사합니다.",
  ].join("\n"), settings);

  assert.equal(result.categories.basic.items.greeting.enabled, false);
  assert.equal(result.categories.basic.items.greeting.score, 0);
  assert.equal(result.categories.business.items.fareGuide.maxScore, 30);
  assert.equal(result.rawScore, result.maxScore);
  assert.equal(result.totalScore, 100);
});
