import assert from "node:assert/strict";
import test from "node:test";

import {
  buildTranscriptMessage,
  parseTranscriptLine,
} from "../public/transcript-chat-utils.js";

test("parseTranscriptLine reads timestamped speaker lines", () => {
  assert.deepEqual(
    parseTranscriptLine("[00:03.00-00:10.00] 상담원: 안녕하세요."),
    {
      role: "agent",
      label: "상담원",
      time: "00:03.00-00:10.00",
      text: "안녕하세요.",
    },
  );
});

test("parseTranscriptLine maps customer speaker labels", () => {
  assert.deepEqual(
    parseTranscriptLine("고객: 출발지는 성북구입니다."),
    {
      role: "customer",
      label: "고객",
      time: "",
      text: "출발지는 성북구입니다.",
    },
  );
});

test("parseTranscriptLine keeps plain transcript lines neutral", () => {
  assert.deepEqual(
    parseTranscriptLine("안녕하세요. 접수 도와드리겠습니다."),
    {
      role: "unknown",
      label: "전사",
      time: "",
      text: "안녕하세요. 접수 도와드리겠습니다.",
    },
  );
});

test("buildTranscriptMessage preserves explicit speaker and timestamp metadata", () => {
  assert.deepEqual(
    buildTranscriptMessage({
      speaker: "고객",
      channel: "left",
      start: 3,
      end: 4.25,
      text: "출발지는 성북구입니다.",
    }),
    {
      role: "customer",
      label: "고객",
      time: "00:03.00-00:04.25",
      text: "출발지는 성북구입니다.",
    },
  );
});

test("buildTranscriptMessage falls back to channel when speaker is not mapped", () => {
  assert.deepEqual(
    buildTranscriptMessage({
      speaker: "speaker_1",
      channel: "right",
      start: 5,
      end: 7,
      text: "배차 확인해드리겠습니다.",
    }),
    {
      role: "agent",
      label: "상담원",
      time: "00:05.00-00:07.00",
      text: "배차 확인해드리겠습니다.",
    },
  );
});
