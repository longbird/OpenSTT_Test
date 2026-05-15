import assert from "node:assert/strict";
import test from "node:test";

import { buildSessionFetchErrorMessage } from "../public/session-utils.js";

test("buildSessionFetchErrorMessage explains local server connection failures", () => {
  const message = buildSessionFetchErrorMessage(new TypeError("Failed to fetch"));

  assert.match(message, /로컬 서버에 연결할 수 없습니다/);
  assert.match(message, /npm start/);
  assert.match(message, /localhost:3000/);
});

test("buildSessionFetchErrorMessage preserves other errors", () => {
  const message = buildSessionFetchErrorMessage(new Error("unexpected"));

  assert.equal(message, "unexpected");
});
