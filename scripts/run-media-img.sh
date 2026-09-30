#!/usr/bin/env bash
# Wrapper for systemd, NSSM, or manual runs. Configure via environment variables.
#
#   MEDIA_IMG_CMD       serve | ipc (default: serve)
#   MEDIA_IMG_BIN       path to media-img (default: media-img on PATH)
#   MEDIA_IMG_HOST      bind address (default: 127.0.0.1)
#   MEDIA_IMG_PORT      TCP port — default 8080 for serve, 9333 for ipc
#   MEDIA_IMG_CACHE_DIR optional — passed as --cache-dir
#   MEDIA_IMG_NO_CACHE  set to 1 to add --no-cache
#   MEDIA_IMG_IPC_UNIX    if set (and MEDIA_IMG_CMD=ipc), uses Unix socket instead of TCP
#
# Extra CLI arguments are appended after the fixed flags.
#
# Example:
#   export MEDIA_IMG_CMD=serve MEDIA_IMG_PORT=8080 MEDIA_IMG_CACHE_DIR=/var/cache/media-img
#   exec /opt/media-img/scripts/run-media-img.sh

set -euo pipefail

readonly CMD="${MEDIA_IMG_CMD:-serve}"
readonly BIN="${MEDIA_IMG_BIN:-media-img}"
readonly HOST="${MEDIA_IMG_HOST:-127.0.0.1}"

extra=()
if [[ -n "${MEDIA_IMG_CACHE_DIR:-}" ]]; then
  extra+=(--cache-dir "$MEDIA_IMG_CACHE_DIR")
fi
if [[ "${MEDIA_IMG_NO_CACHE:-}" == "1" ]]; then
  extra+=(--no-cache)
fi

case "$CMD" in
  serve)
    PORT="${MEDIA_IMG_PORT:-8080}"
    exec "$BIN" serve --host "$HOST" --port "$PORT" "${extra[@]}" "$@"
    ;;
  ipc)
    if [[ -n "${MEDIA_IMG_IPC_UNIX:-}" ]]; then
      exec "$BIN" ipc --unix "$MEDIA_IMG_IPC_UNIX" "${extra[@]}" "$@"
    fi
    PORT="${MEDIA_IMG_PORT:-9333}"
    exec "$BIN" ipc --host "$HOST" --port "$PORT" "${extra[@]}" "$@"
    ;;
  *)
    echo "run-media-img.sh: MEDIA_IMG_CMD must be 'serve' or 'ipc', got: $CMD" >&2
    exit 1
    ;;
esac
