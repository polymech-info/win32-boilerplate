#!/usr/bin/env bash
# Repack OC4VM 2.5.1 opencore.iso: copy EFI/OC/config.plist through a *mounted* HFS+ slice
# so the filesystem is consistent (avoids unclean HFS+ / OpenCore HLT with raw block splicing).
#
# macOS: uses hdiutil. Linux: needs sudo and hfsplus (e.g. apt install hfsprogs; WSL: often unavailable).
#
#   ./repack-opencore-hfs.sh --stock /path/to/vendor/AMD/opencore.iso \
#     --plist /path/to/OPENCORE/EFI/OC/config.plist --out /path/to/sequoia-vm/opencore.iso
#
# OC4VM 2.5.1 (matches patch_hfs_plist.py / sequoia-vm)
HFS_OFF=$((32 * 1024))
HFS_SIZE=$((40 * 1024 * 1024 - 32768))
HFS_SECTS=$((HFS_SIZE / 4096))
SEEK_4K=$((HFS_OFF / 4096))

set -euo pipefail
usage() { sed -n '1,20p' "$0" | tail -n +2; }
STOCK= PLIST= OUT= WORKDIR= CLEAN=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --stock) STOCK=$2; shift 2 ;;
    --plist) PLIST=$2; shift 2 ;;
    --out)   OUT=$2; shift 2 ;;
    --work)  WORKDIR=$2; shift 2 ;;
    --no-cleanup) CLEAN=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done
[[ -n $STOCK && -n $PLIST && -n $OUT ]] || { echo "Need --stock --plist --out" >&2; exit 1; }
[[ -f $STOCK && -f $PLIST ]] || { echo "Missing file (stock or plist)" >&2; exit 1; }

if [[ -z $WORKDIR ]]; then
  WORKDIR=$(mktemp -d 2>/dev/null || mktemp -d -t oc4vmrepack)
fi
HFS_PATH="$WORKDIR/opencore-slice.hfs"
MNT="$WORKDIR/mnt"
mkdir -p "$MNT"
rm_workdir() { if [[ $CLEAN -eq 1 ]]; then rm -rf "$WORKDIR" 2>/dev/null; fi; }
trap 'rm_workdir' EXIT

echo "==> Extracting HFS+ slice ($HFS_SIZE bytes at offset $HFS_OFF)"
if ! dd "if=$STOCK" "of=$HFS_PATH" bs=4096 skip=$SEEK_4K count=$HFS_SECTS 2>/dev/null; then
  dd "if=$STOCK" "of=$HFS_PATH" bs=1 skip=$HFS_OFF count=$HFS_SIZE
fi
obs=$(wc -c < "$HFS_PATH" | tr -d ' \r\n' || true)
if [[ ${obs:-0} -ne $HFS_SIZE ]]; then echo "Slice size got $obs want $HFS_SIZE" >&2; exit 1; fi

KERNEL=$(uname -s)
unmount_() {
  if [[ $KERNEL == Darwin ]]; then
    hdiutil detach "$MNT" -force 2>/dev/null || hdiutil detach "$MNT" 2>/dev/null || true
  else
    ${SUDO:-sudo} umount "$MNT" 2>/dev/null || true
  fi
}

if [[ $KERNEL == Darwin ]]; then
  echo "==> macOS: hdiutil attach (r/w)"
  hdiutil attach -readwrite -nobrowse -mountpoint "$MNT" "$HFS_PATH"
else
  echo "==> Linux: loop mount hfs+ (need sudo; apt install hfsprogs on Debian/Ubuntu)"
  S=${SUDO:-sudo}
  if ! $S mount -t hfsplus -o loop,force,rw "$HFS_PATH" "$MNT" 2>/dev/null; then
    if ! $S mount -t hfs -o loop,force,rw "$HFS_PATH" "$MNT" 2>/dev/null; then
      echo "Could not mount HFS+." >&2
      echo "WSL1/WSL2 often lack reliable hfs+ write; use macOS, or a Linux host with hfsprogs." >&2
      exit 1
    fi
  fi
fi

TARGET=
for cand in "EFI/OC/config.plist" "OPENCORE/EFI/OC/config.plist"; do
  if [[ -f $MNT/$cand ]]; then
    TARGET=$MNT/$cand
    break
  fi
done
if [[ -z $TARGET ]]; then
  echo "config.plist not found. Listing (first 40 lines):" >&2
  find "$MNT" 2>/dev/null | head -n 40 >&2
  unmount_
  exit 1
fi
echo "==> Installing plist -> $TARGET"
cp -f "$PLIST" "$TARGET"
sync
unmount_
trap 'rm_workdir' EXIT

# Build on local disk only; do not dd(1) straight onto VMware HGFS (shared folder) — partial writes/seeking
# there have produced corrupt opencore.iso and "CPU has been disabled by the guest operating system" / HLT.
MERGED="$WORKDIR/merged.opencore.iso"
echo "==> Writing merged ISO in temp, then copy -> $OUT"
cp -f "$STOCK" "$MERGED" 2>/dev/null || cp "$STOCK" "$MERGED"
if ! dd "if=$HFS_PATH" "of=$MERGED" bs=4096 seek=$SEEK_4K count=$HFS_SECTS conv=notrunc 2>/dev/null; then
  dd "if=$HFS_PATH" "of=$MERGED" bs=1 seek=$HFS_OFF conv=notrunc
fi
cp -f "$MERGED" "$OUT"
echo "Wrote: $OUT"
if command -v shasum &>/dev/null; then
  shasum -a 256 "$OUT" | awk '{print $1}'
fi

rm_workdir
trap - EXIT
exit 0
