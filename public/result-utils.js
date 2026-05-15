export function buildResultExport({
  file,
  options,
  transcript,
  corrections,
  qualityEvaluation,
  qualityRuleSettings,
  events,
  comparison,
  createdAt,
}) {
  const cleanTranscript = String(transcript || "").trim();
  if (!cleanTranscript) {
    throw new Error("Transcript is required for result export.");
  }

  return {
    schemaVersion: 1,
    createdAt,
    file: {
      name: file?.name || "",
      size: Number(file?.size || 0),
    },
    options: {
      source: options?.source || "file",
      model: options?.model || "",
      language: options?.language || "ko",
      noiseReduction: options?.noiseReduction || "off",
      prompt: options?.prompt || "",
      correctionProvider: options?.correctionProvider || "",
    },
    transcript: cleanTranscript,
    corrections: normalizeCorrections(corrections),
    qualityEvaluation: String(qualityEvaluation || "").trim(),
    qualityRuleSettings: qualityRuleSettings || null,
    comparison: String(comparison || "").trim(),
    events: String(events || "").trim(),
  };
}

export function normalizeCorrections(corrections = {}) {
  return Object.fromEntries(
    Object.entries(corrections)
      .filter(([, value]) => String(value || "").trim())
      .map(([provider, value]) => [provider, String(value).trim()]),
  );
}

export function downloadJsonFile({ documentRef, objectUrlFactory, revokeObjectUrl, filename, data }) {
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: "application/json" });
  const url = objectUrlFactory(blob);
  const link = documentRef.createElement("a");
  link.href = url;
  link.download = filename;
  documentRef.body.appendChild(link);
  link.click();
  link.remove();
  revokeObjectUrl(url);
}
