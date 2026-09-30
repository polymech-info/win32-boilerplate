#!/usr/bin/env python3
"""
scad_to_step.py - Convert OpenSCAD .scad files to STEP using FreeCAD.

Works on Windows, macOS, and Linux if FreeCAD is installed. OpenSCAD is optional
but recommended/usually required for FreeCAD's OpenSCAD importer.

Examples:
  python scad_to_step.py part.scad part.step
  python scad_to_step.py ./models ./step_out
  python scad_to_step.py C:\\models\\part.scad C:\\exports\\part.step
  python scad_to_step.py part.scad --freecad "C:\\Program Files\\FreeCAD 0.21\\bin\\FreeCADCmd.exe"
  python scad_to_step.py part.scad --openscad "C:\\Program Files\\OpenSCAD\\openscad.exe"

Input and output may be relative or absolute paths.
If INPUT is a directory, all .scad files under it are converted recursively.
"""
from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path
from typing import Iterable, Optional


FREECAD_WORKER = r'''
from __future__ import annotations
import os
import sys
import traceback

infile = os.path.abspath(sys.argv[1])
outfile = os.path.abspath(sys.argv[2])
openscad = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] else ""

try:
    import FreeCAD  # type: ignore

    if openscad:
        p = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/OpenSCAD")
        p.SetString("openscadexecutable", openscad)
        os.environ["OPENSCAD_EXECUTABLE"] = openscad
        os.environ["OPENSCAD"] = openscad
        os.environ["PATH"] = os.path.dirname(openscad) + os.pathsep + os.environ.get("PATH", "")

    doc = FreeCAD.newDocument("scad_to_step")
    import importCSG  # type: ignore

    loaded_doc = None
    try:
        loaded_doc = importCSG.open(infile)
    except Exception:
        importCSG.insert(infile, doc.Name)
        loaded_doc = FreeCAD.ActiveDocument or doc

    doc = loaded_doc or FreeCAD.ActiveDocument or doc
    doc.recompute()

    objects = []
    for obj in list(doc.Objects):
        shape = getattr(obj, "Shape", None)
        if shape is None:
            continue
        try:
            if shape.isNull():
                continue
        except Exception:
            pass
        objects.append(obj)

    if not objects:
        raise RuntimeError("No solid/shape objects were imported from the SCAD file.")

    outdir = os.path.dirname(outfile)
    if outdir:
        os.makedirs(outdir, exist_ok=True)

    import Import  # type: ignore
    Import.export(objects, outfile)

    if not os.path.exists(outfile) or os.path.getsize(outfile) == 0:
        raise RuntimeError("STEP export did not create a non-empty file: " + outfile)

    print("OK: " + outfile)
    sys.exit(0)
except Exception as exc:
    print("ERROR while converting:", infile, "->", outfile, file=sys.stderr)
    print(str(exc), file=sys.stderr)
    traceback.print_exc(file=sys.stderr)
    sys.exit(2)
'''


def dedupe(paths: Iterable[Path]) -> list[Path]:
    seen: set[str] = set()
    out: list[Path] = []
    for p in paths:
        try:
            key = str(p.expanduser().resolve())
        except Exception:
            key = str(p)
        low = key.lower() if os.name == "nt" else key
        if low not in seen:
            seen.add(low)
            out.append(p)
    return out


def is_executable_file(path: Path) -> bool:
    return path.is_file() and (os.name == "nt" or os.access(path, os.X_OK))


def existing_executables(candidates: Iterable[Path]) -> list[Path]:
    return [p for p in dedupe(candidates) if is_executable_file(p)]


def which_many(names: Iterable[str]) -> list[Path]:
    hits: list[Path] = []
    for name in names:
        found = shutil.which(name)
        if found:
            hits.append(Path(found))
    return existing_executables(hits)


def find_freecad(explicit: Optional[str] = None) -> Optional[Path]:
    if explicit:
        p = Path(explicit).expanduser()
        return p if is_executable_file(p) else None

    for key in ["FREECADCMD", "FREECAD_CMD", "FREECAD", "FREECAD_EXECUTABLE"]:
        val = os.environ.get(key)
        if val and is_executable_file(Path(val).expanduser()):
            return Path(val).expanduser()

    path_hits = which_many([
        "FreeCADCmd", "freecadcmd", "FreeCADCmd.exe", "freecadcmd.exe",
        "FreeCAD", "freecad", "FreeCAD.exe", "freecad.exe",
    ])
    if path_hits:
        return path_hits[0]

    sysname = platform.system().lower()
    candidates: list[Path] = []

    if sysname == "windows":
        roots = [
            os.environ.get("ProgramW6432"), os.environ.get("ProgramFiles"),
            os.environ.get("ProgramFiles(x86)"), os.environ.get("LOCALAPPDATA"),
            r"C:\ProgramData\chocolatey\bin",
            str(Path.home() / "scoop" / "apps" / "freecad" / "current" / "bin"),
        ]
        for root in [Path(r) for r in roots if r]:
            candidates += list(root.glob("FreeCAD*\\bin\\FreeCADCmd.exe"))
            candidates += list(root.glob("FreeCAD*\\bin\\freecadcmd.exe"))
            candidates += list(root.glob("FreeCAD*\\bin\\FreeCAD.exe"))
            candidates += list(root.glob("FreeCAD*\\bin\\freecad.exe"))
        candidates += [
            Path(r"C:\Program Files\FreeCAD 1.0\bin\FreeCADCmd.exe"),
            Path(r"C:\Program Files\FreeCAD 0.22\bin\FreeCADCmd.exe"),
            Path(r"C:\Program Files\FreeCAD 0.21\bin\FreeCADCmd.exe"),
            Path(r"C:\Program Files\FreeCAD 0.20\bin\FreeCADCmd.exe"),
        ]
    elif sysname == "darwin":
        candidates += [
            Path("/Applications/FreeCAD.app/Contents/MacOS/FreeCADCmd"),
            Path("/Applications/FreeCAD.app/Contents/MacOS/FreeCAD"),
            Path("/opt/homebrew/bin/freecadcmd"), Path("/opt/homebrew/bin/freecad"),
            Path("/usr/local/bin/freecadcmd"), Path("/usr/local/bin/freecad"),
        ]
    else:
        candidates += [
            Path("/usr/bin/freecadcmd"), Path("/usr/local/bin/freecadcmd"),
            Path("/snap/bin/freecad"), Path("/usr/bin/freecad"),
            Path("/usr/local/bin/freecad"), Path("/app/bin/freecadcmd"), Path("/app/bin/freecad"),
        ]

    hits = existing_executables(sorted(candidates, key=lambda p: str(p), reverse=True))
    return hits[0] if hits else None


def find_openscad(explicit: Optional[str] = None) -> Optional[Path]:
    if explicit:
        p = Path(explicit).expanduser()
        return p if is_executable_file(p) else None

    for key in ["OPENSCAD", "OPENSCAD_EXECUTABLE"]:
        val = os.environ.get(key)
        if val and is_executable_file(Path(val).expanduser()):
            return Path(val).expanduser()

    hits = which_many(["openscad", "OpenSCAD", "openscad.exe", "openscad.com"])
    if hits:
        return hits[0]

    sysname = platform.system().lower()
    candidates: list[Path] = []
    if sysname == "windows":
        roots = [
            os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)"),
            os.environ.get("LOCALAPPDATA"),
            str(Path.home() / "scoop" / "apps" / "openscad" / "current"),
            r"C:\ProgramData\chocolatey\bin",
        ]
        for root in [Path(r) for r in roots if r]:
            candidates += list(root.glob("OpenSCAD*\\openscad.exe"))
            candidates += list(root.glob("OpenSCAD*\\openscad.com"))
        candidates += [Path(r"C:\Program Files\OpenSCAD\openscad.exe"), Path(r"C:\Program Files\OpenSCAD\openscad.com")]
    elif sysname == "darwin":
        candidates += [Path("/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD"), Path("/opt/homebrew/bin/openscad"), Path("/usr/local/bin/openscad")]
    else:
        candidates += [Path("/usr/bin/openscad"), Path("/usr/local/bin/openscad"), Path("/snap/bin/openscad"), Path("/app/bin/openscad")]

    hits = existing_executables(candidates)
    return hits[0] if hits else None


def resolve_path(path_text: str) -> Path:
    return Path(path_text).expanduser().resolve()


def collect_jobs(input_path: Path, output_path: Optional[Path]) -> list[tuple[Path, Path]]:
    if input_path.is_file():
        if input_path.suffix.lower() != ".scad":
            raise SystemExit(f"Input file is not .scad: {input_path}")
        if output_path is None:
            out = input_path.with_suffix(".step")
        elif output_path.exists() and output_path.is_dir():
            out = output_path / (input_path.stem + ".step")
        elif str(output_path).endswith((os.sep, "/")):
            out = output_path / (input_path.stem + ".step")
        else:
            out = output_path
        return [(input_path, out)]

    if input_path.is_dir():
        out_root = output_path or (input_path / "step")
        scads = sorted(input_path.rglob("*.scad"))
        if not scads:
            raise SystemExit(f"No .scad files found under: {input_path}")
        return [(src, (out_root / src.relative_to(input_path)).with_suffix(".step")) for src in scads]

    raise SystemExit(f"Input path does not exist: {input_path}")


def run_one(freecad: Path, worker: Path, src: Path, dst: Path, openscad: Optional[Path], timeout: int) -> int:
    dst.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(freecad), str(worker), str(src), str(dst)]
    if openscad:
        cmd.append(str(openscad))

    env = os.environ.copy()
    if openscad:
        env["OPENSCAD"] = str(openscad)
        env["OPENSCAD_EXECUTABLE"] = str(openscad)
        env["PATH"] = str(openscad.parent) + os.pathsep + env.get("PATH", "")

    print(f"Converting:\n  SCAD: {src}\n  STEP: {dst}")
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, timeout=timeout)
    if proc.stdout.strip():
        print(proc.stdout.strip())
    if proc.stderr.strip():
        print(proc.stderr.strip(), file=sys.stderr)
    return proc.returncode


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Find FreeCAD and convert OpenSCAD .scad file(s) to STEP.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""
            Notes:
              - FreeCAD must be installed.
              - OpenSCAD should be installed for FreeCAD's OpenSCAD importer.
              - Relative paths are resolved from the current working directory.
              - For a directory input, output is treated as an output directory.
        """),
    )
    parser.add_argument("input", help="Input .scad file or directory containing .scad files")
    parser.add_argument("output", nargs="?", help="Output .step file or output directory")
    parser.add_argument("--freecad", help="Path to FreeCADCmd/FreeCAD executable")
    parser.add_argument("--openscad", help="Path to OpenSCAD executable")
    parser.add_argument("--timeout", type=int, default=900, help="Timeout per file in seconds (default: 900)")
    parser.add_argument("--keep-worker", action="store_true", help="Keep temporary FreeCAD worker script for debugging")
    args = parser.parse_args(argv)

    src_root = resolve_path(args.input)
    dst_root = resolve_path(args.output) if args.output else None

    freecad = find_freecad(args.freecad)
    if not freecad:
        print("Could not find FreeCADCmd/FreeCAD.", file=sys.stderr)
        print("Install FreeCAD or pass --freecad PATH. On Windows, the usual path is:", file=sys.stderr)
        print(r"  C:\Program Files\FreeCAD 0.21\bin\FreeCADCmd.exe", file=sys.stderr)
        return 1

    openscad = find_openscad(args.openscad)
    if not openscad:
        print("Warning: OpenSCAD executable not found. FreeCAD may still work if configured already.", file=sys.stderr)
        print("If conversion fails, install OpenSCAD or pass --openscad PATH.", file=sys.stderr)

    jobs = collect_jobs(src_root, dst_root)

    tmp = tempfile.NamedTemporaryFile("w", suffix="_freecad_scad_to_step.py", delete=False, encoding="utf-8")
    worker_path = Path(tmp.name)
    try:
        tmp.write(FREECAD_WORKER)
        tmp.close()

        print(f"Using FreeCAD:  {freecad}")
        print(f"Using OpenSCAD: {openscad if openscad else '(not found / FreeCAD preference only)'}")
        print(f"Jobs: {len(jobs)}")

        failed = 0
        for src, dst in jobs:
            try:
                code = run_one(freecad, worker_path, src, dst, openscad, args.timeout)
            except subprocess.TimeoutExpired:
                print(f"Timed out: {src}", file=sys.stderr)
                code = 124
            if code != 0:
                failed += 1
                print(f"FAILED ({code}): {src}", file=sys.stderr)
            else:
                print(f"WROTE: {dst}")

        if failed:
            print(f"Done with failures: {failed}/{len(jobs)} failed", file=sys.stderr)
            return 2
        print("Done: all conversions succeeded")
        return 0
    finally:
        if args.keep_worker:
            print(f"Kept worker script: {worker_path}")
        else:
            try:
                worker_path.unlink()
            except OSError:
                pass


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
