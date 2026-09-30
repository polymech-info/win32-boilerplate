#!/usr/bin/env python3
"""
Strip SwiftLint SwiftPM build-tool plugin usage from Package.swift files under
SourcePackages/checkouts. Non-interactive `xcodebuild` often fails when the
SwiftLint binary runs inside the plugin sandbox (SourceKit / sourcekitdInProc).
Run after `xcodebuild -resolvePackageDependencies` and before `xcodebuild build`.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

# .target( / .testTarget( ... plugins: [ .plugin(name: "SwiftLint", package: "SwiftLintPlugin") ] )
_PLUGIN_BLOCK = re.compile(
    r",?\s*plugins:\s*\[\s*\.plugin\s*\(\s*name:\s*\"SwiftLint\"\s*,\s*package:\s*\"SwiftLintPlugin\"\s*\)\s*\]\s*,?",
    re.MULTILINE | re.DOTALL,
)

# Optional // SwiftLint comment + .package( url: "https://github.com/lukepistrol/SwiftLintPlugin", ... ),
_PACKAGE_BLOCK = re.compile(
    r"\s*//\s*SwiftLint\s*\r?\n\s*\.package\s*\(\s*url:\s*\"https://github\.com/lukepistrol/SwiftLintPlugin\"[\s\S]*?\)\s*,?",
    re.MULTILINE,
)

# Same .package without a leading comment (url may be on the next line)
_PACKAGE_BARE = re.compile(
    r"\s*\.package\s*\(\s*url:\s*\"https://github\.com/lukepistrol/SwiftLintPlugin\"[\s\S]*?\)\s*,?",
    re.MULTILINE,
)


def patch(text: str) -> str:
    if "SwiftLintPlugin" not in text and "SwiftLint" not in text:
        return text
    t = _PLUGIN_BLOCK.sub("", text)
    t = _PACKAGE_BLOCK.sub("\n", t)
    t = _PACKAGE_BARE.sub("\n", t)
    return t


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: strip_spm_swiftlint_plugins.py <SourcePackages/checkouts dir>", file=sys.stderr)
        return 2
    root = Path(sys.argv[1])
    if not root.is_dir():
        return 0
    n = 0
    for p in root.rglob("Package.swift"):
        raw = p.read_text(encoding="utf-8")
        new = patch(raw)
        if new != raw:
            p.write_text(new, encoding="utf-8")
            n += 1
            print(f"strip_spm_swiftlint_plugins: patched {p}", file=sys.stderr)
    if n:
        print(f"strip_spm_swiftlint_plugins: updated {n} Package.swift file(s)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
