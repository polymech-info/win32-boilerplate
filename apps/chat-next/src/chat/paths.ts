/** Path for a chat session — use from C++/WebView2 when navigating internally. */
export function chatSessionPath(sessionId: string): string {
  return `/chat/${encodeURIComponent(sessionId)}`;
}
