"""Stage: cross-check retag's artist/title against Last.fm's independent
name database and correct it where Last.fm disagrees.

None of retag.py's sources (AcoustID, Discogs, MusicBrainz/beets, or the
filename-split last resort) ever get a second opinion from each other -- a
confidently-wrong catalog match or a clean-looking-but-typo'd filename
guess sails straight through with nothing to catch it (see retag.py's own
docstring for a real confirmed false-positive case). Last.fm's
`track.getCorrection`/`artist.getCorrection` API is purpose-built for
exactly this: given a possibly-wrong (artist, title), it returns its own
best-guess canonical spelling from a large, independently-crowd-sourced
database.

Runs right after retag and before intro_outro/title_dedup -- both of those
group/match on artist and title, so they should see corrected names (same
reasoning stages.py's docstring already gives for why intro_outro itself
sits right after retag).

Because this gets a normal `lastfm_correct_done` checkpoint column (added
via checkpoint.py's existing migration mechanism, same as
intro_outro_done/title_dedup_done before it), every row already in the
library defaults to not-done the moment this ships -- so running this stage
once sweeps the entire existing library retroactively, not just newly
scanned files. Rows already fully processed (some already restructured,
physically living at /Music/<Artist>/<Artist> - <Title>.ext) are exactly
what get swept up on that first run, so applying a correction reuses
artist_folder_normalize.py's already-solved "rename an artist/title that
may already be a real folder" machinery (_move_to_canonical_folder,
_rewrite_tags_on_disk, _prune_if_empty, _log_merge) rather than
reimplementing it -- this codebase already reuses "private" helpers across
modules this way (e.g. title_dedup.py importing dedup.py's
_keeper_sort_key).

Last.fm returns no confidence score, just one suggested (artist, title)
pair -- and its own database (crowd-sourced from scrobbles) can be wrong
too, especially for the kind of niche/self-released/game-rip content this
library has a lot of. So corrections are tiered by how different the
suggestion actually is, using the same exact_key()-based "same underlying
identity" test artist_folder_normalize.py already uses for its own
safe-vs-ambiguous split:
  - same exact_key() on both artist and title (case/diacritics/punctuation/
    whitespace differences only, e.g. "Motorhead" -> "Motörhead") -> auto-
    applied, safe cosmetic cleanup.
  - anything more different (a real respelling, e.g. "Champange Supernova"
    -> "Champagne Supernova", or a materially different name) -> written to
    lastfm_correction_report.json only, never auto-applied -- a human
    confirms via manual_apply() (the `lastfm-apply-correction` CLI command),
    mirroring artist_folder_normalize.py's manual_merge()/
    manual_collab_split() pattern exactly.

Two-layer lookup per file, mirroring retag.py's own AcoustID-then-Discogs
shape: `track.getcorrection` (precise, needs both artist+title) first; if
Last.fm doesn't know the track as a formal entity at all (expected for a
lot of this library's content), `artist.getcorrection` alone as a fallback,
touching only the artist half.
"""

import json
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from . import log
from . import net
from . import text_normalize as tn

LASTFM_API_URL = "https://ws.audioscrobbler.com/2.0/"
LASTFM_KEY_FILE = net.KEYS_DIR / "lastfm_api_key.txt"
LASTFM_WORKERS = 4

# ~5 req/s -- commonly cited courtesy limit for a free Last.fm API key (no
# official hard number published; same "be a good citizen" posture already
# applied to AcoustID/Discogs in retag.py).
_LASTFM_RATE_LIMITER = net.RateLimiter(0.2)

_ELIGIBLE_WHERE = (
    "type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0 "
    "AND retag_done=1 AND lastfm_correct_done=0"
)


def _lastfm_api_key() -> str | None:
    return net.read_api_key(LASTFM_KEY_FILE, "LASTFM_API_KEY")


def _lastfm_get(params: dict) -> dict | None:
    """Shared GET+JSON call for both correction endpoints. None on any
    failure -- network, timeout, or Last.fm's own error-object response
    (returned both for real failures and for the ordinary "not in our
    database" case, which is expected often enough for this library's
    niche content that it isn't worth logging on its own). Network/parse
    failures ARE logged, so a systemic problem (bad key, rate limiting,
    an outage) is visible in cleanup.log rather than looking identical to
    "nothing found" for every file."""
    url = f"{LASTFM_API_URL}?{urllib.parse.urlencode(params)}"
    _LASTFM_RATE_LIMITER.wait()
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            data = json.loads(response.read())
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, ValueError) as exc:
        log.get().warning(f"[LASTFM] request failed ({params.get('method')}): {exc}")
        return None
    if "error" in data:
        return None
    return data


def _first_correction(data: dict) -> dict | None:
    """Last.fm's JSON responses collapse a single-item list to a bare
    object (an XML-to-JSON conversion quirk their API is known for) --
    normalize both shapes here rather than at every call site."""
    correction = (data.get("corrections") or {}).get("correction")
    if not correction:
        return None
    if isinstance(correction, list):
        correction = correction[0] if correction else None
    return correction


def _lastfm_track_correction(apikey: str, artist: str, title: str) -> tuple[str, str] | None:
    data = _lastfm_get({
        "method": "track.getcorrection", "artist": artist, "track": title,
        "api_key": apikey, "format": "json",
    })
    if data is None:
        return None
    correction = _first_correction(data)
    if not correction:
        return None
    track = correction.get("track") or {}
    corrected_title = track.get("name")
    corrected_artist = (track.get("artist") or {}).get("name")
    if not corrected_artist or not corrected_title:
        return None
    return corrected_artist, corrected_title


def _lastfm_artist_correction(apikey: str, artist: str) -> str | None:
    data = _lastfm_get({
        "method": "artist.getcorrection", "artist": artist,
        "api_key": apikey, "format": "json",
    })
    if data is None:
        return None
    correction = _first_correction(data)
    if not correction:
        return None
    return (correction.get("artist") or {}).get("name") or None


def _is_near_identical(old: str, new: str) -> bool:
    """Same underlying identity, just presentation (case/diacritics/
    punctuation/whitespace) different -- safe to auto-apply. A real
    respelling (different letters) is NOT near-identical and must go
    through review instead. Same exact_key()-based test
    artist_folder_normalize.py already uses for its own safe-vs-ambiguous
    split."""
    return tn.exact_key(old) == tn.exact_key(new)


def _correction_worker(row: dict, apikey: str) -> dict:
    """Runs entirely off local state + Last.fm (no DB access) so it's safe
    to call from a thread pool -- same shape as retag.py's own
    _acoustid_phase_worker."""
    artist, title = row["artist"], row["title"]
    correction = _lastfm_track_correction(apikey, artist, title)
    if correction is None:
        fallback_artist = _lastfm_artist_correction(apikey, artist)
        if fallback_artist and fallback_artist != artist:
            correction = (fallback_artist, title)
    return {"path": row["path"], "correction": correction}


def _apply_correction(
    conn, library_root: Path, row: dict, new_artist: str, new_title: str, excluded_root: Path,
    action: str = "lastfm_auto_correct",
) -> bool:
    """Applies a correction to one row -- DB+tag update only if not yet
    restructured, or a physical file move if it already is (reusing
    artist_folder_normalize.py's own already-solved rename machinery for
    exactly this problem). Returns True if a physical file move happened."""
    from . import artist_folder_normalize as afn

    path = row["path"]
    moved = False
    if row["restructure_done"]:
        src = Path(path)
        old_folder = src.parent
        dest = afn._move_to_canonical_folder(library_root, src, new_artist, new_title)
        moved = dest != src
        final_relative = dest.relative_to(library_root)
        conn.execute(
            "UPDATE files SET artist=?, title=?, path=?, final_relative_path=? WHERE path=?",
            (new_artist, new_title, str(dest), str(final_relative), path),
        )
        afn._rewrite_tags_on_disk(dest, artist=new_artist, title=new_title)
        if moved:
            afn._prune_if_empty(old_folder)
    else:
        conn.execute("UPDATE files SET artist=?, title=? WHERE path=?", (new_artist, new_title, path))
        afn._rewrite_tags_on_disk(Path(path), artist=new_artist, title=new_title)

    afn._log_merge(
        excluded_root, action,
        path=path, old_artist=row["artist"], new_artist=new_artist,
        old_title=row["title"], new_title=new_title,
    )
    return moved


def correct_names(
    library_root: Path, excluded_root: Path, dry_run: bool = False, workers: int = LASTFM_WORKERS
) -> dict:
    from . import checkpoint
    from . import log as log_module

    logger = log_module.get()
    stats = {"considered": 0, "auto_applied": 0, "reported": 0, "no_correction": 0, "files_moved": 0}
    report_groups: dict[tuple[str, str], dict] = {}

    with checkpoint.connect(excluded_root) as conn:
        rows = [
            dict(r) for r in conn.execute(
                f"SELECT path, artist, title, restructure_done FROM files WHERE {_ELIGIBLE_WHERE}"
            )
        ]
        stats["considered"] = len(rows)
        logger.info(f"[LASTFM] {len(rows)} file(s) to cross-check, {workers} workers")

        if dry_run:
            return stats

        apikey = _lastfm_api_key()
        if not apikey:
            logger.info(
                "[LASTFM] no Last.fm API key found -- get a free key from "
                "last.fm/api/account/create, save it to .beets/lastfm_api_key.txt "
                "(or set LASTFM_API_KEY) -- skipping, all files left pending"
            )
            return stats

        eligible = [row for row in rows if row["artist"] and row["title"]]

        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {pool.submit(_correction_worker, row, apikey): row for row in eligible}

            for future in as_completed(futures):
                row = futures[future]
                correction = future.result()["correction"]
                relative = Path(row["path"]).relative_to(library_root)

                if correction is None:
                    stats["no_correction"] += 1
                    continue

                new_artist, new_title = correction
                if new_artist == row["artist"] and new_title == row["title"]:
                    stats["no_correction"] += 1
                    continue

                if _is_near_identical(row["artist"], new_artist) and _is_near_identical(row["title"], new_title):
                    if _apply_correction(conn, library_root, row, new_artist, new_title, excluded_root):
                        stats["files_moved"] += 1
                    conn.commit()
                    stats["auto_applied"] += 1
                    logger.info(
                        f"[LASTFM] {relative} -> auto-corrected: "
                        f"artist={row['artist']!r}->{new_artist!r} title={row['title']!r}->{new_title!r}"
                    )
                else:
                    key = (row["artist"], row["title"])
                    group = report_groups.setdefault(key, {
                        "old_artist": row["artist"], "old_title": row["title"],
                        "suggested_artist": new_artist, "suggested_title": new_title,
                        "files": [],
                    })
                    group["files"].append(row["path"])
                    stats["reported"] += 1
                    logger.info(
                        f"[LASTFM] {relative} -> reported for review: "
                        f"artist={row['artist']!r}->{new_artist!r} title={row['title']!r}->{new_title!r}"
                    )

        # Bulk-mark done via the stage's own eligibility WHERE, not a
        # per-row `path=?` -- an auto-applied correction may already have
        # changed a row's `path` (the primary key) via a physical move, so
        # a stale-path UPDATE here would silently miss it. The eligibility
        # clause doesn't reference path at all, so it still correctly
        # matches every row this run considered, wherever it ended up.
        conn.execute(f"UPDATE files SET lastfm_correct_done=1 WHERE {_ELIGIBLE_WHERE}")
        conn.commit()

    if report_groups:
        checkpoint.lastfm_correction_report_path(excluded_root).write_text(
            json.dumps(list(report_groups.values()), indent=2)
        )
        logger.info(f"[LASTFM] {len(report_groups)} correction(s) written to report for manual review")

    return stats


def manual_apply(
    library_root: Path, excluded_root: Path,
    from_artist: str, from_title: str, to_artist: str, to_title: str,
    dry_run: bool = False,
) -> dict:
    """Human-invoked, after reviewing a lastfm_correction_report.json entry
    and confirming Last.fm's suggestion is right. Works before or after
    restructure, same as artist_folder_normalize.py's manual_merge()/
    manual_collab_split()."""
    from . import checkpoint
    from . import log as log_module

    logger = log_module.get()
    stats = {"rows_updated": 0, "files_moved": 0}

    with checkpoint.connect(excluded_root) as conn:
        rows = [
            dict(r) for r in conn.execute(
                "SELECT * FROM files WHERE artist=? AND title=?", (from_artist, from_title)
            )
        ]
        if not rows:
            logger.info(f"[LASTFM] no rows found for artist={from_artist!r} title={from_title!r}")
            return stats

        for row in rows:
            stats["rows_updated"] += 1
            src = Path(row["path"])
            if dry_run:
                logger.info(f"[LASTFM]   would apply: {src} -> artist={to_artist!r} title={to_title!r}")
                continue

            if _apply_correction(conn, library_root, row, to_artist, to_title, excluded_root, action="lastfm_manual_apply"):
                stats["files_moved"] += 1
            conn.commit()
            logger.info(f"[LASTFM]   applied: {src} -> artist={to_artist!r} title={to_title!r}")

    return stats
