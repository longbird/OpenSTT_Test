import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import {
  getApiKey,
  getApiKeyStatus,
  readStoredApiKeys,
  saveApiKeys,
} from "../key-store.js";

test("saveApiKeys stores encrypted data without leaking plaintext keys", async () => {
  const tempDir = await mkdtemp(path.join(os.tmpdir(), "openai-stt-keys-"));
  const storePath = path.join(tempDir, "api-keys.enc.json");

  try {
    await saveApiKeys({
      openai: "  sk-openai-secret  ",
      gemini: "gemini-secret",
      anthropic: "anthropic-secret",
    }, {
      storePath,
      masterSecret: "test-master-secret",
      randomBytes: deterministicRandomBytes,
    });

    const raw = await readFile(storePath, "utf8");
    assert.doesNotMatch(raw, /sk-openai-secret/);
    assert.doesNotMatch(raw, /gemini-secret/);
    assert.doesNotMatch(raw, /anthropic-secret/);
    assert.match(raw, /aes-256-gcm/);

    assert.deepEqual(await readStoredApiKeys({
      storePath,
      masterSecret: "test-master-secret",
    }), {
      openai: "sk-openai-secret",
      gemini: "gemini-secret",
      anthropic: "anthropic-secret",
    });
  } finally {
    await rm(tempDir, { recursive: true, force: true });
  }
});

test("saveApiKeys merges updates and ignores blank fields", async () => {
  const tempDir = await mkdtemp(path.join(os.tmpdir(), "openai-stt-keys-"));
  const storePath = path.join(tempDir, "api-keys.enc.json");

  try {
    await saveApiKeys({ openai: "sk-one", gemini: "gemini-one" }, {
      storePath,
      masterSecret: "test-master-secret",
      randomBytes: deterministicRandomBytes,
    });
    await saveApiKeys({ openai: "", anthropic: "anthropic-one" }, {
      storePath,
      masterSecret: "test-master-secret",
      randomBytes: deterministicRandomBytes,
    });

    assert.deepEqual(await readStoredApiKeys({
      storePath,
      masterSecret: "test-master-secret",
    }), {
      openai: "sk-one",
      gemini: "gemini-one",
      anthropic: "anthropic-one",
    });
  } finally {
    await rm(tempDir, { recursive: true, force: true });
  }
});

test("getApiKey prefers encrypted local keys over environment fallback", async () => {
  const tempDir = await mkdtemp(path.join(os.tmpdir(), "openai-stt-keys-"));
  const storePath = path.join(tempDir, "api-keys.enc.json");

  try {
    await saveApiKeys({ openai: "stored-openai" }, {
      storePath,
      masterSecret: "test-master-secret",
      randomBytes: deterministicRandomBytes,
    });

    assert.equal(await getApiKey("openai", {
      storePath,
      masterSecret: "test-master-secret",
      env: { OPENAI_API_KEY: "env-openai" },
    }), "stored-openai");
    assert.equal(await getApiKey("gemini", {
      storePath,
      masterSecret: "test-master-secret",
      env: { GEMINI_API_KEY: "env-gemini" },
    }), "env-gemini");
  } finally {
    await rm(tempDir, { recursive: true, force: true });
  }
});

test("getApiKeyStatus returns only presence and source, not secret values", async () => {
  const tempDir = await mkdtemp(path.join(os.tmpdir(), "openai-stt-keys-"));
  const storePath = path.join(tempDir, "api-keys.enc.json");

  try {
    await saveApiKeys({ anthropic: "stored-anthropic" }, {
      storePath,
      masterSecret: "test-master-secret",
      randomBytes: deterministicRandomBytes,
    });

    assert.deepEqual(await getApiKeyStatus({
      storePath,
      masterSecret: "test-master-secret",
      env: {
        OPENAI_API_KEY: "env-openai",
      },
    }), {
      openai: { configured: true, source: "env" },
      gemini: { configured: false, source: "missing" },
      anthropic: { configured: true, source: "stored" },
    });
  } finally {
    await rm(tempDir, { recursive: true, force: true });
  }
});

function deterministicRandomBytes(size) {
  return Buffer.alloc(size, 7);
}
