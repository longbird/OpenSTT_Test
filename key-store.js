import { createCipheriv, createDecipheriv, randomBytes as nodeRandomBytes, scryptSync } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

export const API_KEY_DEFINITIONS = {
  openai: { envName: "OPENAI_API_KEY" },
  gemini: { envName: "GEMINI_API_KEY" },
  anthropic: { envName: "ANTHROPIC_API_KEY" },
};

const ALGORITHM = "aes-256-gcm";
const STORE_VERSION = 1;
const DEFAULT_STORE_PATH = path.resolve(process.cwd(), ".local", "api-keys.enc.json");

export async function saveApiKeys(keys, options = {}) {
  const storePath = options.storePath || DEFAULT_STORE_PATH;
  const existing = await readStoredApiKeys(options);
  const next = { ...existing };

  for (const [name, definition] of Object.entries(API_KEY_DEFINITIONS)) {
    const value = keys?.[name] ?? keys?.[definition.envName];
    const trimmed = typeof value === "string" ? value.trim() : "";
    if (trimmed) {
      next[name] = trimmed;
    }
  }

  const encrypted = encryptPayload(next, options);
  await mkdir(path.dirname(storePath), { recursive: true });
  await writeFile(storePath, `${JSON.stringify(encrypted, null, 2)}\n`, {
    mode: 0o600,
  });
  return next;
}

export async function readStoredApiKeys(options = {}) {
  const storePath = options.storePath || DEFAULT_STORE_PATH;
  let raw;
  try {
    raw = await readFile(storePath, "utf8");
  } catch (error) {
    if (error.code === "ENOENT") return {};
    throw error;
  }

  const envelope = JSON.parse(raw);
  return decryptPayload(envelope, options);
}

export async function getApiKey(name, options = {}) {
  assertKnownKeyName(name);
  const stored = await readStoredApiKeys(options);
  if (stored[name]) return stored[name];
  return String((options.env || process.env)[API_KEY_DEFINITIONS[name].envName] || "").trim();
}

export async function getApiKeyStatus(options = {}) {
  const stored = await readStoredApiKeys(options);
  const env = options.env || process.env;

  return Object.fromEntries(Object.entries(API_KEY_DEFINITIONS).map(([name, definition]) => {
    if (stored[name]) {
      return [name, { configured: true, source: "stored" }];
    }
    if (String(env[definition.envName] || "").trim()) {
      return [name, { configured: true, source: "env" }];
    }
    return [name, { configured: false, source: "missing" }];
  }));
}

function encryptPayload(payload, options) {
  const randomBytes = options.randomBytes || nodeRandomBytes;
  const salt = randomBytes(16);
  const iv = randomBytes(12);
  const key = deriveKey(salt, options);
  const cipher = createCipheriv(ALGORITHM, key, iv);
  const ciphertext = Buffer.concat([
    cipher.update(JSON.stringify(payload), "utf8"),
    cipher.final(),
  ]);

  return {
    version: STORE_VERSION,
    algorithm: ALGORITHM,
    salt: salt.toString("base64"),
    iv: iv.toString("base64"),
    tag: cipher.getAuthTag().toString("base64"),
    ciphertext: ciphertext.toString("base64"),
  };
}

function decryptPayload(envelope, options) {
  if (envelope?.version !== STORE_VERSION || envelope?.algorithm !== ALGORITHM) {
    throw new Error("Unsupported API key store format.");
  }

  const salt = Buffer.from(envelope.salt, "base64");
  const iv = Buffer.from(envelope.iv, "base64");
  const tag = Buffer.from(envelope.tag, "base64");
  const ciphertext = Buffer.from(envelope.ciphertext, "base64");
  const key = deriveKey(salt, options);
  const decipher = createDecipheriv(ALGORITHM, key, iv);
  decipher.setAuthTag(tag);
  const plaintext = Buffer.concat([
    decipher.update(ciphertext),
    decipher.final(),
  ]).toString("utf8");

  return sanitizeStoredKeys(JSON.parse(plaintext));
}

function sanitizeStoredKeys(value) {
  const result = {};
  for (const name of Object.keys(API_KEY_DEFINITIONS)) {
    const key = typeof value?.[name] === "string" ? value[name].trim() : "";
    if (key) result[name] = key;
  }
  return result;
}

function deriveKey(salt, options) {
  return scryptSync(secretMaterial(options), salt, 32);
}

function secretMaterial(options = {}) {
  if (options.masterSecret) return options.masterSecret;
  if (process.env.OPENAI_STT_KEYSTORE_SECRET) return process.env.OPENAI_STT_KEYSTORE_SECRET;
  return [
    "openai-stt-local-api-key-store",
    os.hostname(),
    os.userInfo().username,
    path.resolve(options.projectRoot || process.cwd()),
  ].join("|");
}

function assertKnownKeyName(name) {
  if (!Object.hasOwn(API_KEY_DEFINITIONS, name)) {
    throw new Error(`Unknown API key name: ${name}`);
  }
}
