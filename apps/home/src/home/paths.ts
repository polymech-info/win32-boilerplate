/** Path for a chat session — use from C++/WebView2 when navigating internally. */
export function homeSessionPath(sessionId: string): string {
  return `/home/${encodeURIComponent(sessionId)}`;
}
