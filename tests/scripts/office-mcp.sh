#!/usr/bin/env bash
# office-mcp.sh — mcp-libre (patrup/mcp-libre) helpers for pm-image (stdio MCP).
#
# Layout (this repo): packages/media/cpp/systems/mcp/office  (clone + `uv sync` there)
# Profile MCP config:  %AppData%/PolyMech/pm-image/mcp.json (Windows) — same tree as settings.json
#
# Prerequisite: LibreOffice 24.2+ with `soffice` on PATH (or set LIBREOFFICE_PATH for the server).
# Without it, `llm info` can show handshake OK but read_spreadsheet_data / convert fail at runtime.
#
# Usage:
#   ./tests/scripts/office-mcp.sh help
#   ./tests/scripts/office-mcp.sh paths          # print resolved dirs + example mcp.json block
#   ./tests/scripts/office-mcp.sh serve          # foreground stdio MCP (for debugging)
#   ./tests/scripts/office-mcp.sh info           # pm-image llm info (MCP probe)
#   ./tests/scripts/office-mcp.sh agent-xlsx     # one agent turn on tests/viewer/3dtest/specs.xlsx
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OFFICE_ROOT="${OFFICE_ROOT:-${CPP_ROOT}/systems/mcp/office}"
SPECS_XLSX="${SPECS_XLSX:-${CPP_ROOT}/tests/viewer/3dtest/specs.xlsx}"
THREED_FOLDER="${THREED_FOLDER:-${CPP_ROOT}/tests/viewer/3dtest}"

pm_image_bin() {
  local w="${CPP_ROOT}/dist/pm-image.exe"
  local u="${CPP_ROOT}/dist/pm-image"
  if [[ -x "$w" ]]; then echo "$w"; return
  elif [[ -x "$u" ]]; then echo "$u"; return
  fi
  if command -v pm-image.exe >/dev/null 2>&1; then command -v pm-image.exe; return; fi
  if command -v pm-image >/dev/null 2>&1; then command -v pm-image; return; fi
  return 1
}

cmd_help() {
  sed -n '1,25p' "$0" | tail -n +2
}

cmd_paths() {
  echo "CPP_ROOT=$CPP_ROOT"
  echo "OFFICE_ROOT=$OFFICE_ROOT"
  echo "SPECS_XLSX=$SPECS_XLSX"
  echo "THREED_FOLDER=$THREED_FOLDER"
  local bin
  bin="$(pm_image_bin || true)"
  echo "PM_IMAGE=${bin:-<build packages/media/cpp dist pm-image first>}"
  echo ""
  echo "Example mcp.json fragment (merge under mcpServers). Fix paths if you move the tree:"
  cat <<EOF
  "libreoffice": {
    "command": "uv",
    "args": [
      "run",
      "--directory",
      "${OFFICE_ROOT}",
      "python",
      "src/main.py"
    ]
  }
EOF
  echo ""
  echo "Windows profile file (typical):"
  echo "  \$APPDATA/PolyMech/pm-image/mcp.json"
}

cmd_serve() {
  if [[ ! -f "${OFFICE_ROOT}/src/main.py" ]]; then
    echo "office-mcp: missing ${OFFICE_ROOT}/src/main.py — clone mcp-libre into systems/mcp/office and run: uv sync" >&2
    exit 1
  fi
  cd "${OFFICE_ROOT}"
  exec uv run python src/main.py
}

cmd_info() {
  local bin=""
  bin="$(pm_image_bin)" || true
  if [[ -z "$bin" ]]; then
    echo "office-mcp: pm-image not found under dist/; build Release or set PATH" >&2
    exit 1
  fi
  exec "$bin" llm info
}

cmd_agent_xlsx() {
  local bin=""
  bin="$(pm_image_bin)" || true
  if [[ -z "$bin" ]]; then
    echo "office-mcp: pm-image not found under dist/; build Release or set PATH" >&2
    exit 1
  fi
  if [[ ! -f "$SPECS_XLSX" ]]; then
    echo "office-mcp: missing $SPECS_XLSX (skip or set SPECS_XLSX=...)" >&2
    exit 1
  fi
  exec "$bin" llm agent \
    --paths "$SPECS_XLSX" \
    --folder "$THREED_FOLDER" \
    -p "Use mcp_libreoffice__read_spreadsheet_data on the selected xlsx path. Summarize sheet names, dimensions, and the first 15 rows of the first sheet as a markdown table. Do not use file_read on the xlsx. If the tool errors, quote the error text."
}

main() {
  local sub="${1:-help}"
  shift || true
  case "$sub" in
    help|-h|--help) cmd_help ;;
    paths) cmd_paths ;;
    serve|run|stdio) cmd_serve ;;
    info) cmd_info ;;
    agent-xlsx|agent) cmd_agent_xlsx ;;
    *)
      echo "office-mcp: unknown command: $sub" >&2
      cmd_help >&2
      exit 1
      ;;
  esac
}

main "$@"
