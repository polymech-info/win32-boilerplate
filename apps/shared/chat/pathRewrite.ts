export function rewriteLocalPath(p: string): string {
  if (!p) return p;
  if (/^(https?|data):/i.test(p)) return p;
  let s = p;
  if (/^pm-files:\/\//i.test(s)) {
    s = s.replace(/^pm-files:\/\//i, "");
  } else if (/^file:\/\/\//i.test(s)) {
    s = decodeURIComponent(s.replace(/^file:\/\/\//i, ""));
  }
  const m = /^([A-Za-z]):[\\/](.*)$/.exec(s);
  if (m) {
    const letter = m[1].toLowerCase();
    const rest = m[2].replace(/\\/g, "/");
    return `https://pm-files-${letter}.local/${rest}`;
  }
  if (s.startsWith("/")) {
    return "file://" + s;
  }
  return "file:///" + s.replace(/\\/g, "/").replace(/^\/+/, "");
}
