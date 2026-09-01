#!/bin/bash
# webm_followup.sh -- rescue opus-in-webm files the main cleanup flow skips.
#
# Why they're skipped: audio_probe.py probes with mutagen, which has no
# matroska/webm parser, so every .webm returns duration=None and scan marks
# it dedup_status='corrupt' -- excluded from dedup/retag/rename/embed. The
# files are fine (full-length opus audio), just in a container mutagen
# can't read AND the Hiby R3 Pro can't play.
#
# This pass: ffmpeg transcodes each .webm's audio to MP3 (CBR 320k by
# default, ~2-4s each), deletes the .webm once the .mp3 verifies, prunes
# the now-stale .webm rows from the checkpoint DB, then re-runs the no-move
# flow so the new .mp3 files get deduped / retagged / renamed / embedded
# like everything else. No `restructure` -- folder layout is preserved.
#
# Why MP3 and not a stream-copy to .opus: the Hiby R3 Pro plays MP3
# everywhere, Opus not reliably. Why not FLAC: the source is already lossy
# Opus, so FLAC would just wrap lossy audio losslessly -- 5-10x the size
# for zero fidelity gained. MP3 320 adds one negligible lossy generation.
#
# Usage:
#   ./webm_followup.sh --dry-run     # list what would be transcoded, do nothing
#   ./webm_followup.sh               # transcode + prune + cleanup pass
#   MP3_BITRATE=256k ./webm_followup.sh
#
# Safe to re-run: a .webm whose .mp3 already exists is skipped.
set -uo pipefail

cd "$(dirname "$0")" || exit 1
export PATH="$PWD/.venv/bin:$PATH"   # so retag's `beet` fallback resolves
PY=.venv/bin/python3
R="${MUSIC_DAP_ROOT:-/mnt/data/library/Music_DAP}"
X="${MUSIC_DAP_EXCLUDED:-/mnt/data/library/Excluded Audio DAP}"
DB="$X/_pipeline/state.sqlite"
MP3_BITRATE="${MP3_BITRATE:-320k}"
DRY=""
[ "${1:-}" = "--dry-run" ] && DRY=1

# --- guard: never overlap a running pipeline (shared checkpoint DB + .beets)
if pgrep -f "cleanup\.py (scan|dedup|retag|rename|embed|lastfm|intro|title|energy|artist|restructure|manifest)" >/dev/null; then
  echo "ERROR: a cleanup stage is already running -- wait for it to finish, then re-run this." >&2
  exit 1
fi

for bin in ffmpeg ffprobe; do
  command -v "$bin" >/dev/null || { echo "ERROR: $bin not on PATH" >&2; exit 1; }
done

mapfile -d '' WEBM < <(find "$R" -type f -iname '*.webm' -print0)
echo "found ${#WEBM[@]} .webm file(s) under $R"
[ "${#WEBM[@]}" -eq 0 ] && { echo "nothing to do"; exit 0; }

ok=0; fail=0; skip=0
for f in "${WEBM[@]}"; do
  out="${f%.webm}.mp3"
  if [ -e "$out" ]; then echo "SKIP  (.mp3 exists): ${f#"$R"/}"; skip=$((skip+1)); continue; fi
  if [ -n "$DRY" ]; then echo "would transcode: ${f#"$R"/}"; ok=$((ok+1)); continue; fi
  if ffmpeg -v error -nostdin -i "$f" -map 0:a:0 -c:a libmp3lame -b:a "$MP3_BITRATE" \
       -map_metadata 0 -id3v2_version 3 "$out" </dev/null; then
    d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$out" 2>/dev/null)
    if [ -n "$d" ] && awk "BEGIN{exit !($d>1)}"; then
      rm -f -- "$f"
      printf 'OK    %-6ss  %s\n' "${d%.*}" "${out#"$R"/}"
      ok=$((ok+1))
    else
      rm -f -- "$out"; echo "FAIL  (bad output, kept .webm): ${f#"$R"/}"; fail=$((fail+1))
    fi
  else
    echo "FAIL  (ffmpeg error, kept .webm): ${f#"$R"/}"; fail=$((fail+1))
  fi
done
echo "transcode: ok=$ok fail=$fail skip=$skip  (bitrate $MP3_BITRATE)"

if [ -n "$DRY" ]; then echo "(dry run -- no DB prune, no cleanup pass)"; exit 0; fi
[ "$ok" -eq 0 ] && { echo "no new .mp3 produced -- nothing more to do"; exit 0; }

# --- drop checkpoint rows for .webm files that no longer exist on disk
"$PY" - "$DB" <<'PYEOF'
import os, sqlite3, sys
db = sys.argv[1]
c = sqlite3.connect(db)
gone = [p for (p,) in c.execute("SELECT path FROM files WHERE path LIKE '%.webm'") if not os.path.exists(p)]
c.executemany("DELETE FROM files WHERE path=?", [(p,) for p in gone])
c.commit(); c.close()
print(f"pruned {len(gone)} stale .webm row(s) from the checkpoint DB")
PYEOF

# --- push the new .mp3 files through the same no-move flow
for stage in "scan --min-bitrate 0" "dedup" "retag" "rename" "embed"; do
  echo ">>>>> $stage  $(date '+%F %T')"
  # shellcheck disable=SC2086
  "$PY" cleanup.py $stage --root "$R" --excluded-root "$X" \
    || { echo "!!!!! '$stage' failed -- stopping" >&2; exit 1; }
done
echo "##### webm follow-up complete  $(date '+%F %T')"
