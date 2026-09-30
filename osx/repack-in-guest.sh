#!/usr/bin/env bash
# Run inside the Sequoia guest (Terminal) after you copy these onto the host share, e.g.
#   C:\Users\zx\sequoia-repack\stock-opencore.iso
#   C:\Users\zx\sequoia-repack\config.plist
#   (this script) repack-in-guest.sh
#   repack-opencore-hfs.sh
# Guest paths are then under /Volumes/<sharename>/… — default is /Volumes/zx/sequoia-repack
#
#   chmod +x repack-*.sh
#   ./repack-in-guest.sh
#   # Shut down the VM, then on Windows copy opencore-repacked.iso over D:\osx\sequoia-vm\opencore.iso

set -euo pipefail
SHARE="${1:-/Volumes/zx/sequoia-repack}"
HERE=$(cd "$(dirname "$0")" && pwd)
REPACK="${REPACK:-$HERE/repack-opencore-hfs.sh}"
if [[ ! -f $REPACK && -f $SHARE/repack-opencore-hfs.sh ]]; then
  REPACK=$SHARE/repack-opencore-hfs.sh
fi
if [[ ! -f $REPACK ]]; then
  echo "Set REPACK= to repack-opencore-hfs.sh or put it next to this script or in \$SHARE" >&2
  exit 1
fi
if [[ ! -d $SHARE ]]; then
  echo "Shared folder not mounted at: $SHARE" >&2
  echo "Create C:\\Users\\zx\\sequoia-repack on the host, copy files, adjust path." >&2
  exit 1
fi
exec bash "$REPACK" \
  --stock "$SHARE/stock-opencore.iso" \
  --plist "$SHARE/config.plist" \
  --out "$SHARE/opencore-repacked.iso"
