export type PrototypeTheme = "dark" | "light";

export function nextPrototypeTheme(theme: PrototypeTheme): PrototypeTheme {
  return theme === "dark" ? "light" : "dark";
}
