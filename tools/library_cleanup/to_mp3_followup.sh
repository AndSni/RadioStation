#!/bin/bash
# to_mp3_followup.sh -- normalise /Music_DAP to MP3 + FLAC only.
#
# Transcodes every audio file that is NOT .mp3 and NOT .flac (.opus, .m4a,
# .m4b, .ogg, .aac, .wav, .wma, .aiff ...) to MP3 (CBR 320k by default,
# metadata carried), deletes the source once the .mp3 verifies, prunes the
# now-stale rows from the checkpoint DB, then re-runs the no-move flow so
# the new .mp3 files get deduped / retagged / renamed / embedded like
# everything else. No `restructure` -- folder layout is preserved.
#
# Rationale: the Hiby R3 Pro plays MP3 and FLAC reliably; Opus/other
# containers not. FLAC is kept (lossless, plays fine). Sources here are all
# already lossy (AAC/Opus), so MP3 320 costs one inaudible generation.
#
# Usage:
#   ./to_mp3_followup.sh --dry-run     # list what would convert, do nothing
#   ./to_mp3_followup.sh               # convert + prune + cleanup pass
#   MP3_BITRATE=256k ./to_mp3_followup.sh
#
# Safe to re-run: a source whose .mp3 already exists is skipped.
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

# extensions to transcode (everything audio that is neither mp3 nor flac)
CONV_EXT_RE='\.(opus|ogg|oga|m4a|m4b|m4r|mp4|aac|wav|wave|aif|aiff|aifc|wma|ape|wv|mpc|alac)$'

if pgrep -f "cleanup\.py (scan|dedup|retag|rename|embed|lastfm|intro|title|energy|artist|restructure|manifest)" >/dev/null; then
  echo "ERROR: a cleanup stage is already running -- wait for it to finish, then re-run this." >&2
  exit 1
fi
for bin in ffmpeg ffprobe; do
  command -v "$bin" >/dev/null || { echo "ERROR: $bin not on PATH" >&2; exit 1; }
done

mapfile -d '' SRC < <(find "$R" -type f -regextype posix-extended -iregex ".*$CONV_EXT_RE" -print0)
echo "found ${#SRC[@]} non-MP3/FLAC audio file(s) under $R"
[ "${#SRC[@]}" -eq 0 ] && { echo "nothing to do"; exit 0; }

ok=0; fail=0; skip=0
for f in "${SRC[@]}"; do
  out="${f%.*}.mp3"
  if [ -e "$out" ]; then echo "SKIP  (.mp3 exists): ${f#"$R"/}"; skip=$((skip+1)); continue; fi
  if [ -n "$DRY" ]; then echo "would convert: ${f#"$R"/}"; ok=$((ok+1)); continue; fi
  if ffmpeg -v error -nostdin -i "$f" -map 0:a:0 -c:a libmp3lame -b:a "$MP3_BITRATE" \
       -map_metadata 0 -id3v2_version 3 "$out" </dev/null; then
    d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$out" 2>/dev/null)
    if [ -n "$d" ] && awk "BEGIN{exit !($d>1)}"; then
      rm -f -- "$f"
      printf 'OK    %-6ss  %s\n' "${d%.*}" "${out#"$R"/}"
      ok=$((ok+1))
    else
      rm -f -- "$out"; echo "FAIL  (bad output, kept source): ${f#"$R"/}"; fail=$((fail+1))
    fi
  else
    echo "FAIL  (ffmpeg error, kept source): ${f#"$R"/}"; fail=$((fail+1))
  fi
done
echo "convert: ok=$ok fail=$fail skip=$skip  (bitrate $MP3_BITRATE)"

if [ -n "$DRY" ]; then echo "(dry run -- no DB prune, no cleanup pass)"; exit 0; fi
[ "$ok" -eq 0 ] && { echo "no new .mp3 produced -- nothing more to do"; exit 0; }

# drop checkpoint rows for now-deleted non-mp3/flac sources
"$PY" - "$DB" <<'PYEOF'
import os, re, sqlite3, sys
db = sys.argv[1]
c = sqlite3.connect(db)
rx = re.compile(r"\.(opus|ogg|oga|m4a|m4b|m4r|mp4|aac|wav|wave|aif|aiff|aifc|wma|ape|wv|mpc|alac|webm)$", re.I)
gone = [p for (p,) in c.execute("SELECT path FROM files") if rx.search(p) and not os.path.exists(p)]
c.executemany("DELETE FROM files WHERE path=?", [(p,) for p in gone])
c.commit(); c.close()
print(f"pruned {len(gone)} stale non-MP3/FLAC row(s) from the checkpoint DB")
PYEOF

for stage in "scan --min-bitrate 0" "dedup" "retag" "rename" "embed"; do
  echo ">>>>> $stage  $(date '+%F %T')"
  # shellcheck disable=SC2086
  "$PY" cleanup.py $stage --root "$R" --excluded-root "$X" \
    || { echo "!!!!! '$stage' failed -- stopping" >&2; exit 1; }
done
echo "##### to-mp3 follow-up complete  $(date '+%F %T')"
