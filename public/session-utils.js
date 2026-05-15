export function buildSessionFetchErrorMessage(error) {
  const message = error?.message || String(error);
  if (error instanceof TypeError && /Failed to fetch/i.test(message)) {
    return "로컬 서버에 연결할 수 없습니다. 터미널에서 npm start가 실행 중인지 확인한 뒤 http://localhost:3000을 새로고침하세요.";
  }
  return message;
}
