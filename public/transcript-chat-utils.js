const SPEAKER_ALIASES = {
  고객: "customer",
  customer: "customer",
  left: "customer",
  상담원: "agent",
  상담사: "agent",
  기사: "agent",
  agent: "agent",
  right: "agent",
};

const SPEAKER_LABELS = {
  customer: "고객",
  agent: "상담원",
  unknown: "전사",
};

export function parseTranscriptLine(line) {
  const raw = String(line || "").trim();
  const withTime = raw.match(/^\[([^\]]+)\]\s*([^:：]+)[:：]\s*(.+)$/);
  if (withTime) {
    return buildMessage({
      time: withTime[1].trim(),
      speaker: withTime[2].trim(),
      text: withTime[3].trim(),
    });
  }

  const withSpeaker = raw.match(/^([^:：]{1,12})[:：]\s*(.+)$/);
  if (withSpeaker && SPEAKER_ALIASES[withSpeaker[1].trim().toLowerCase()]) {
    return buildMessage({
      time: "",
      speaker: withSpeaker[1].trim(),
      text: withSpeaker[2].trim(),
    });
  }

  return buildMessage({
    time: "",
    speaker: "",
    text: raw,
  });
}

export function buildTranscriptMessage({ speaker, channel, start, end, text }) {
  let role = normalizeSpeakerRole(speaker);
  if (role === "unknown") {
    role = normalizeSpeakerRole(channel);
  }

  return {
    role,
    label: SPEAKER_LABELS[role],
    time: formatMessageTime(start, end),
    text: String(text || "").trim(),
  };
}

function buildMessage({ time, speaker, text }) {
  const role = normalizeSpeakerRole(speaker);
  return {
    role,
    label: SPEAKER_LABELS[role],
    time,
    text,
  };
}

function normalizeSpeakerRole(speaker) {
  return SPEAKER_ALIASES[String(speaker || "").trim().toLowerCase()] || "unknown";
}

function formatMessageTime(start, end) {
  if (!Number.isFinite(Number(start)) || !Number.isFinite(Number(end))) return "";
  return `${formatSeconds(start)}-${formatSeconds(end)}`;
}

function formatSeconds(seconds) {
  const total = Math.max(0, Number(seconds) || 0);
  const minutes = String(Math.floor(total / 60)).padStart(2, "0");
  const wholeSeconds = String(Math.floor(total % 60)).padStart(2, "0");
  const centiseconds = String(Math.round((total - Math.floor(total)) * 100)).padStart(2, "0");
  return `${minutes}:${wholeSeconds}.${centiseconds}`;
}
