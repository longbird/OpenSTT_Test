const CATEGORY_DEFINITIONS = {
  basic: {
    label: "응대 기본 흐름",
    maxScore: 30,
    items: {
      greeting: {
        label: "인사",
        score: 5,
        patterns: [/안녕하세/i, /감사합니다/i],
      },
      requestConfirm: {
        label: "고객 요청 확인",
        score: 10,
        patterns: [/가려고|가실|출발|도착|문의|예약|배차|취소/i],
      },
      conditionGuide: {
        label: "조건/금액 안내",
        score: 10,
        patterns: [/\d+\s*만\s*원|\d+\s*만원|금액|요금|배차|가능|어렵|안\s*되/i],
      },
      closing: {
        label: "마무리 안내",
        score: 5,
        patterns: [/감사합니다|알겠습니다|연락|취소해드릴|확인/i],
      },
    },
  },
  business: {
    label: "업무 처리 정확성",
    maxScore: 35,
    items: {
      dispatchReason: {
        label: "배차 가능성 설명",
        score: 10,
        patterns: [/배차.*(어렵|안\s*되|늦|가능)|기사.*(없|늦)|잘\s*안\s*되/i],
      },
      fareGuide: {
        label: "금액 조정 안내",
        score: 10,
        patterns: [/\d+\s*만\s*원|\d+\s*만원|추가|부터.*알아/i],
      },
      customerDecision: {
        label: "고객 의사 확인",
        score: 10,
        patterns: [/괜찮으|하시겠|해드릴까요|취소|내일|네|아니/i],
      },
      followUp: {
        label: "후속 조치 안내",
        score: 5,
        patterns: [/다시\s*연락|연락드|알아봐|취소해드|처리/i],
      },
    },
  },
  conversation: {
    label: "대화 품질",
    maxScore: 20,
    items: {
      enoughTurns: {
        label: "대화량 충분",
        score: 5,
        test: ({ utterances }) => utterances.length >= 4,
      },
      balancedSpeakers: {
        label: "상담원/고객 발화 균형",
        score: 5,
        test: ({ speakerCounts }) => speakerCounts.agent > 0 && speakerCounts.customer > 0,
      },
      customerReflected: {
        label: "고객 발화 반영",
        score: 5,
        test: ({ speakerCounts, text }) => speakerCounts.customer > 0 && /괜찮|알겠|취소|내일|출발|도착|문의|가려고/i.test(text),
      },
      noExcessiveRepetition: {
        label: "반복 과다 없음",
        score: 5,
        test: ({ utterances }) => repetitionRatio(utterances) < 0.45,
      },
    },
  },
  reliability: {
    label: "전사/화자분리 신뢰도",
    maxScore: 15,
    items: {
      speakerSeparation: {
        label: "화자분리 정상",
        score: 5,
        test: ({ speakerCounts, unknownRatio }) => speakerCounts.agent > 0 && speakerCounts.customer > 0 && unknownRatio < 0.35,
      },
      enoughSentences: {
        label: "문장 수 충분",
        score: 5,
        test: ({ utterances }) => utterances.length >= 4,
      },
      orderReasonable: {
        label: "대화 순서 정상",
        score: 5,
        test: ({ utterances }) => countSpeakerSwitches(utterances) >= 1,
      },
    },
  },
};

export const DEFAULT_QUALITY_RULE_SETTINGS = buildDefaultQualityRuleSettings();

export function getQualityRuleCatalog() {
  return Object.fromEntries(
    Object.entries(CATEGORY_DEFINITIONS).map(([categoryKey, category]) => [categoryKey, {
      label: category.label,
      items: Object.fromEntries(
        Object.entries(category.items).map(([itemKey, rule]) => [itemKey, {
          id: `${categoryKey}.${itemKey}`,
          label: rule.label,
          defaultScore: rule.score,
        }]),
      ),
    }]),
  );
}

export function normalizeQualityRuleSettings(settings = {}) {
  const inputRules = settings?.rules || {};
  return {
    rules: Object.fromEntries(
      Object.entries(flattenRuleDefinitions()).map(([ruleId, rule]) => {
        const input = inputRules[ruleId] || {};
        return [ruleId, {
          enabled: input.enabled !== false,
          score: normalizeScore(input.score, rule.score),
        }];
      }),
    ),
  };
}

export function evaluateTranscriptQualityLocally(transcript, settings = DEFAULT_QUALITY_RULE_SETTINGS) {
  const utterances = parseUtterances(transcript);
  const text = utterances.map((utterance) => utterance.text).join("\n");
  const speakerCounts = countSpeakers(utterances);
  const unknownRatio = utterances.length ? speakerCounts.unknown / utterances.length : 1;
  const context = { utterances, text, speakerCounts, unknownRatio };
  const normalizedSettings = normalizeQualityRuleSettings(settings);

  const categories = Object.fromEntries(
    Object.entries(CATEGORY_DEFINITIONS).map(([key, category]) => {
      const items = Object.fromEntries(
        Object.entries(category.items).map(([itemKey, rule]) => {
          const ruleSettings = normalizedSettings.rules[`${key}.${itemKey}`];
          const enabled = ruleSettings.enabled;
          const maxScore = enabled ? ruleSettings.score : 0;
          const passed = enabled && evaluateRule(rule, context);
          return [itemKey, {
            label: rule.label,
            enabled,
            maxScore,
            score: passed ? maxScore : 0,
            passed,
            evidence: passed ? findEvidence(rule, utterances) : "",
          }];
        }),
      );
      const score = Object.values(items).reduce((sum, item) => sum + item.score, 0);
      const maxScore = Object.values(items).reduce((sum, item) => sum + item.maxScore, 0);
      return [key, {
        label: category.label,
        maxScore,
        score,
        items,
      }];
    }),
  );

  const rawScore = Object.values(categories).reduce((sum, category) => sum + category.score, 0);
  const maxScore = Object.values(categories).reduce((sum, category) => sum + category.maxScore, 0);
  const totalScore = maxScore ? Math.round((rawScore / maxScore) * 100) : 0;
  const warnings = buildWarnings(context);
  const confidence = buildConfidence(context, warnings);

  return {
    totalScore,
    rawScore,
    maxScore,
    confidence,
    categories,
    passed: collectItems(categories, true),
    failed: collectItems(categories, false),
    warnings,
  };
}

export function formatLocalQualityEvaluation(result, aiNote = "") {
  const failed = result.failed
    .map((item) => `- ${item.category}: ${item.label}`)
    .join("\n") || "- 없음";
  const passed = result.passed
    .slice(0, 8)
    .map((item) => `- ${item.category}: ${item.label}${item.evidence ? ` (${item.evidence})` : ""}`)
    .join("\n") || "- 없음";
  const warnings = result.warnings.map((warning) => `- ${warning}`).join("\n") || "- 없음";

  return [
    `로컬 평가 점수: ${result.totalScore}/100 (원점수 ${result.rawScore}/${result.maxScore})`,
    `평가 신뢰도: ${result.confidence.label}`,
    "",
    "통과 항목:",
    passed,
    "",
    "감점 항목:",
    failed,
    "",
    "근거:",
    warnings,
    aiNote ? ["", "AI 참고 의견:", aiNote.trim()].join("\n") : "",
  ].filter(Boolean).join("\n");
}

function parseUtterances(transcript) {
  return String(transcript || "")
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => {
      const match = line.match(/^(?:\[[^\]]+\]\s*)?([^:：]{1,12})[:：]\s*(.+)$/);
      const speaker = normalizeSpeaker(match?.[1] || "");
      return {
        speaker,
        text: (match?.[2] || line).trim(),
      };
    });
}

function normalizeSpeaker(speaker) {
  const value = String(speaker || "").trim().toLowerCase();
  if (["상담원", "상담사", "기사", "agent", "right"].includes(value)) return "agent";
  if (["고객", "customer", "left"].includes(value)) return "customer";
  return "unknown";
}

function countSpeakers(utterances) {
  return utterances.reduce((counts, utterance) => {
    counts[utterance.speaker] += 1;
    return counts;
  }, { agent: 0, customer: 0, unknown: 0 });
}

function evaluateRule(rule, context) {
  if (rule.test) return Boolean(rule.test(context));
  return rule.patterns.some((pattern) => pattern.test(context.text));
}

function findEvidence(rule, utterances) {
  if (!rule.patterns) return "";
  const utterance = utterances.find((candidate) => rule.patterns.some((pattern) => pattern.test(candidate.text)));
  return utterance?.text.slice(0, 42) || "";
}

function collectItems(categories, passed) {
  return Object.values(categories).flatMap((category) => (
    Object.values(category.items)
      .filter((item) => item.enabled !== false && item.passed === passed)
      .map((item) => ({ ...item, category: category.label }))
  ));
}

function buildDefaultQualityRuleSettings() {
  return {
    rules: Object.fromEntries(
      Object.entries(flattenRuleDefinitions()).map(([ruleId, rule]) => [ruleId, {
        enabled: true,
        score: rule.score,
      }]),
    ),
  };
}

function flattenRuleDefinitions() {
  return Object.fromEntries(
    Object.entries(CATEGORY_DEFINITIONS).flatMap(([categoryKey, category]) => (
      Object.entries(category.items).map(([itemKey, rule]) => [`${categoryKey}.${itemKey}`, rule])
    )),
  );
}

function normalizeScore(value, fallback) {
  const score = Number(value);
  if (!Number.isFinite(score)) return fallback;
  return Math.max(0, Math.min(100, Math.round(score)));
}

function buildWarnings({ utterances, speakerCounts, unknownRatio }) {
  const warnings = [];
  if (utterances.length < 4) warnings.push("전사 문장 수가 적어 평가 신뢰도가 낮습니다.");
  if (unknownRatio >= 0.35) warnings.push("화자분리 라벨이 부족하거나 전사 라벨 비율이 높습니다.");
  if (!speakerCounts.agent || !speakerCounts.customer) warnings.push("상담원/고객 중 한쪽 발화가 확인되지 않습니다.");
  return warnings;
}

function buildConfidence({ utterances, speakerCounts, unknownRatio }, warnings) {
  if (utterances.length >= 4 && speakerCounts.agent > 0 && speakerCounts.customer > 0 && unknownRatio < 0.2) {
    return { level: "높음", label: "높음" };
  }
  if (warnings.length <= 1 && utterances.length >= 3) {
    return { level: "보통", label: "보통" };
  }
  return { level: "낮음", label: "낮음" };
}

function repetitionRatio(utterances) {
  if (utterances.length < 2) return 0;
  const normalized = utterances.map((utterance) => utterance.text.replace(/\s+/g, " ").trim());
  const repeated = normalized.filter((text, index) => index > 0 && normalized[index - 1] === text).length;
  return repeated / utterances.length;
}

function countSpeakerSwitches(utterances) {
  let switches = 0;
  let previous = "";
  utterances.forEach((utterance) => {
    if (utterance.speaker === "unknown") return;
    if (previous && previous !== utterance.speaker) switches += 1;
    previous = utterance.speaker;
  });
  return switches;
}
