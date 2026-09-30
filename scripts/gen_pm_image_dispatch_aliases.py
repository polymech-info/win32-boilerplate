"""Emit one field name per line from PmImageCliState (for tooling / sync checks).

Dispatch code uses explicit `st.<field>` in pm_image_dispatch*.cpp — no generated includes.
"""
import re
from pathlib import Path

HPP = Path(__file__).resolve().parents[1] / "src" / "cli" / "pm_image_cli_state.hpp"
t = HPP.read_text(encoding="utf-8")
i = t.find("struct PmImageCliState {")
j = t.rfind("};", i)
body = t[i : j + 2]
names: list[str] = []
lines = body.splitlines()
k = 0
while k < len(lines):
    L = lines[k]
    s = L.strip()
    if s.startswith("#if"):
        k += 1
        continue
    if s.startswith("#endif"):
        k += 1
        continue
    if s.startswith("#"):
        k += 1
        continue
    if not s or s.startswith("//"):
        k += 1
        continue
    if s in ("struct PmImageCliState {", "};"):
        k += 1
        continue
    m = re.match(
        r"^(\s*)(const\s+)?(CLI::Option\*|CLI::App\*|std::vector<[^>]+>|std::string|bool|int)\s+(\w+)(\s*=\s*[^;]+)?\s*;\s*$",
        L,
    )
    if m:
        names.append(m.group(4))
    else:
        raise SystemExit(f"unparsed state line: {L!r}")
    k += 1

out = Path(__file__).resolve().parent / "pm_image_cli_field_names.generated.txt"
out.write_text("\n".join(names) + "\n", encoding="utf-8")
print("Wrote", out, f"({len(names)} names)")
