"""Stage 4: re-tag Type=Song survivors -- AcoustID fingerprint lookup first
(fast, ~1-2s/file, reuses the fingerprint already computed during scan),
falling back to slow beets/MusicBrainz text search (~12.7s/file) only for
files AcoustID can't confidently identify at all.

Speed history: originally ran beets first for every file, with AcoustID as
a secondary check. Confirmed AcoustID is both ~7-10x faster AND, after the
fixes below, the more reliable source for Year/Album -- so it's now the
primary path, and beets only runs for the minority of files AcoustID
leaves without an artist/title. This also sidesteps a real concern with
parallelizing the old beets-first design: MusicBrainz's public API asks
for ~1 request/second, and running multiple `beet import` processes
concurrently would each independently hammer that limit. AcoustID's rate
limit is more generous, so the fast path runs with a small thread pool
(network I/O-bound, not CPU-bound) while the slow MusicBrainz fallback
stays strictly sequential.

BEETSDIR is pointed at a project-local directory so this never touches the
user's own `~/.config/beets` library. `import.move`/`import.copy` are
disabled: beets only writes tags in place, restructure.py owns file
placement (stage 7).

Bug fix 1 (per user question about compilation/reissue dates skewing
Era): `date`/`year` reflects the *specific release* matched (e.g. a 2008
"Best Of" compilation), while `originaldate`/`originalyear` is the
earliest known original release date for the recording (e.g. 1984) --
_read_back_tags() reads `originaldate` FIRST, only falling back to `date`
when nothing else is known.

Bug fix 2 (per user: "we need year, oldest year and album wins"): a direct
AcoustID lookup (_acoustid_oldest_match) confirms the artist AND base
title by majority vote across every candidate recording, then takes the
OLDEST dated release found across every release group linked to matching
recordings. Two real contamination cases drove the current design:
  - Voting on artist alone isn't enough: a high-scoring result correctly
    contained five "You've Got Another Thing Comin'" recordings but also
    one mislabeled "Never Satisfied" recording, both credited to Judas
    Priest -- aggregating across all of them pulled in that other song's
    date. Fixed by voting on (artist, title) together.
  - Voting on literal title isn't enough either: "Drops of Jupiter" /
    "Drops of Jupiter (Tell Me)" / "...(Tell Me) (Rock Mix)" are the same
    song, but split votes three ways, and the losing buckets held the
    correct 2001 release data. Fixed by stripping parenthetical/bracketed
    qualifiers before voting (_base_title_key) -- mixes/edits/remasters of
    the same song correctly merge, since finding the song's original era
    is the actual goal, not a specific mix's.
The AcoustID oldest-release result is applied whenever its year is older
than whatever's currently on file (or nothing is) -- "oldest wins" as an
active, authoritative rule, not a fallback for missing data. Also found:
many files here carry pre-existing embedded tags from whatever downloader
sourced them (confirmed `description: Tagged with qbdlX UI`, `comment:
thepiratebay.org...`), including junk placeholder years (e.g. 2026) --
this is exactly why "oldest wins" runs unconditionally rather than only
when Year is completely blank.

Bug fix 3 (per user: "fastest first, slowest last, MusicBrainz absolutely
last option"): a file with no fingerprint (fingerprinting failed at scan
time) or no AcoustID API key configured used to skip straight into the
slow MusicBrainz fallback list, bypassing both the free filename-split
guess AND the faster Discogs lookup entirely -- even when the file already
had perfectly usable pre-existing tags (the same downloader-sourced tags
mentioned above) that only needed a Discogs year lookup, not a full
from-scratch MusicBrainz text search. Every row now goes through
_acoustid_phase_worker regardless of fingerprint/key availability -- its
own `if apikey and row["fingerprint"]:` guard already skips just the
AcoustID network call when there's nothing to work with, and every other
step (reading existing tags, the filename-split fallback, routing to
Discogs vs. MusicBrainz) already works identically either way. MusicBrainz
is now only ever reached when literally nothing else -- not AcoustID, not
existing tags, not even a filename split -- could produce an artist/title.
"""

import json
import os
import re
import subprocess
import sys
import threading
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import acoustid
import mutagen

from . import artist_normalize
from . import log
from . import net
from . import text_normalize as tn

BEETS_DIR = net.KEYS_DIR
ACOUSTID_KEY_FILE = BEETS_DIR / "acoustid_key.txt"
DISCOGS_TOKEN_FILE = BEETS_DIR / "discogs_token.txt"
DISCOGS_USER_AGENT = "SharpRightLibraryCleanup/1.0"
MIN_ACOUSTID_SCORE = 0.9
ACOUSTID_WORKERS = 4

BEETS_CONFIG = """
directory: {beets_dir}/library
library: {beets_dir}/library.db
plugins: musicbrainz
import:
    move: no
    copy: no
    write: yes
    autotag: yes
    quiet: yes
    quiet_fallback: skip
match:
    preferred:
        original_year: yes
"""


# ~3 req/s -- AcoustID's documented courtesy rate limit for its public
# lookup API. Shared across every ACOUSTID_WORKERS thread. (RateLimiter
# itself lives in net.py, shared with Discogs here and Last.fm in
# lastfm_correct.py rather than each source defining its own.)
_ACOUSTID_RATE_LIMITER = net.RateLimiter(1 / 3)
# Safely under Discogs' authenticated search limit of ~60/min (module
# docstring already notes this limit; nothing previously enforced it).
_DISCOGS_RATE_LIMITER = net.RateLimiter(1.05)


def _write_beets_config():
    BEETS_DIR.mkdir(parents=True, exist_ok=True)
    (BEETS_DIR / "config.yaml").write_text(BEETS_CONFIG.format(beets_dir=BEETS_DIR))


def _acoustid_api_key() -> str | None:
    return net.read_api_key(ACOUSTID_KEY_FILE, "ACOUSTID_API_KEY")


def _acoustid_oldest_match(apikey: str, fingerprint: str, duration: float) -> dict:
    """Confirms the (artist, base title) by majority vote across every
    candidate recording AcoustID returns (above MIN_ACOUSTID_SCORE), then
    returns the oldest dated release found across every release group
    linked to recordings matching that pair. Returns {} if no confident
    match is found.

    Bug fix: confirmed a real false positive with no existing artist to
    cross-check against -- a silence-trimmed game-audio file (query
    duration 123.5s) tied 1-1 between "Bad Religion - You" (a genuine
    THPS2 soundtrack song, recording duration 125.3s) and a mislabeled
    "[unknown] - Weißt du, wieviel Sternlein stehen" children's lullaby
    (duration 120.6s), and the tie broke arbitrarily toward the wrong one.
    A first attempt at fixing this by hard-excluding candidates whose
    duration didn't match closely enough backfired: it also excluded a
    *correct* match (a real "Ozomatli" recording linked to the right 1998
    release) whose own stored duration happened to differ more than
    expected, regressing a result that was already right. Duration is a
    tie-breaker signal, not a hard filter -- vote count still decides the
    winner first; only among ties is the closest-duration candidate
    preferred.
    """
    _ACOUSTID_RATE_LIMITER.wait()
    try:
        result = acoustid.lookup(apikey, fingerprint, duration, meta="recordings releasegroups releases")
    except Exception as exc:  # noqa: BLE001 - falls through to MusicBrainz either way, but log it:
        # a network blip, a revoked key, and rate-limiting all used to look
        # identical to "no data from this source" -- with no log line, a
        # systemic failure across many files was invisible.
        log.get().warning(f"[RETAG] AcoustID lookup failed: {exc}")
        return {}
    if result.get("status") != "ok":
        return {}

    candidates = []  # (artist_name, title, base_title_key, recording)
    for r in result.get("results", []):
        if (r.get("score") or 0) < MIN_ACOUSTID_SCORE:
            continue
        for rec in r.get("recordings", []):
            artists = rec.get("artists") or []
            if artists:
                title = rec.get("title")
                candidates.append((artists[0]["name"], title, tn.base_title_key(title), rec))

    if not candidates:
        return {}

    vote_counts = Counter((a, bt) for a, _t, bt, _r in candidates)

    def duration_closeness(key: tuple[str, str]) -> float:
        diffs = [
            abs(rec["duration"] - duration)
            for a, _t, bt, rec in candidates
            if (a, bt) == key and rec.get("duration") is not None
        ]
        return min(diffs) if diffs else float("inf")

    winning_artist, winning_base_title = max(
        vote_counts, key=lambda key: (vote_counts[key], -duration_closeness(key))
    )

    best_year, best_album, best_title = None, None, None
    for artist, title, base_title, rec in candidates:
        if artist != winning_artist or base_title != winning_base_title:
            continue
        if best_title is None:
            best_title = title
        for release_group in rec.get("releasegroups", []):
            for release in release_group.get("releases", []):
                year = (release.get("date") or {}).get("year")
                if year is not None and (best_year is None or year < best_year):
                    best_year = year
                    best_album = release.get("title")

    if best_year is None:
        return {}
    return {"artist": winning_artist, "title": best_title, "album": best_album, "year": best_year}


def _read_back_tags(path: Path) -> dict:
    audio = mutagen.File(path, easy=True)
    if audio is None:
        return {}
    def first(key):
        values = audio.get(key)
        return values[0] if values else None
    year = None
    date = first("originaldate") or first("date")
    if date:
        year = int(str(date)[:4]) if str(date)[:4].isdigit() else None
    return {
        "artist": first("artist"),
        "title": first("title"),
        "album": first("album"),
        "genre": first("genre"),
        "year": year,
    }


def _split_artist_title_from_filename(path: Path, tags: dict) -> dict:
    """Free, zero-network, zero-latency fallback for files no catalog
    (AcoustID, Discogs, MusicBrainz) has ever heard of -- confirmed real
    case: independent royalty-free tracks ("The Evesdroppers... by Free
    Music") never commercially released, so no lookup service can have
    Year data for them at all (a genuine data-availability limit, not
    fixable by any source) -- but the filename often already follows
    "Artist - Title" convention with nothing having split it. Runs BEFORE
    routing to Discogs/MusicBrainz (see module docstring on phase order),
    since a successful split gives Discogs something to search on instead
    of jumping straight to the expensive last-resort step. Only fills in
    artist/title if artist is still completely missing; never touches
    Year (this has no way to find one).

    Bug fix: a leading date-stamp prefix glued to the artist name with an
    underscore ("20110824_Skeletal Spectre - Scalped") produced the wrong
    artist "20110824_Skeletal Spectre" -- stripped before splitting on
    " - ", not just a leading numeric *track number* segment.
    """
    if tags.get("artist"):
        return tags
    candidate = tags.get("title") or path.stem
    candidate = re.sub(r"^\d{6,8}_", "", candidate)
    parts = [p.strip() for p in candidate.split(" - ")]
    if parts and parts[0].isdigit():
        # "11 - Gang Starr - Mass Appeal" -- drop the leading track number
        # rather than naively taking it as the artist.
        parts = parts[1:]
    if len(parts) < 2:
        return tags
    artist, title = parts[0], " - ".join(parts[1:])
    if artist and title:
        tags["artist"] = artist
        tags["title"] = title
    return tags


def _discogs_api_key() -> str | None:
    return net.read_api_key(DISCOGS_TOKEN_FILE, "DISCOGS_TOKEN")


def _discogs_search(token: str, artist: str, title: str) -> dict:
    """One Discogs master-release search attempt. Returns {} on no result,
    no year data, or any request failure (including rate-limiting --
    Discogs' authenticated search limit is ~60/min, so this stays a
    sequential last-resort step, not thread-pooled like AcoustID)."""
    params = urllib.parse.urlencode({"artist": artist, "track": title, "type": "master"})
    url = f"https://api.discogs.com/database/search?{params}"
    request = urllib.request.Request(url, headers={
        "User-Agent": DISCOGS_USER_AGENT,
        "Authorization": f"Discogs token={token}",
    })
    _DISCOGS_RATE_LIMITER.wait()
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            data = json.loads(response.read())
    except urllib.error.HTTPError as exc:
        # Logged distinctly from other failures so a sustained run of 429s
        # (rate-limited despite the pacing above) is visible in
        # cleanup.log rather than looking identical to "no match found".
        if exc.code == 429:
            log.get().warning(f"[RETAG] Discogs rate-limited (429) for {artist!r}/{title!r}")
        else:
            log.get().warning(f"[RETAG] Discogs search failed ({exc.code}) for {artist!r}/{title!r}: {exc}")
        return {}
    except (urllib.error.URLError, TimeoutError, ValueError) as exc:
        log.get().warning(f"[RETAG] Discogs search failed for {artist!r}/{title!r}: {exc}")
        return {}

    best_year, best_album = None, None
    for result in data.get("results", []):
        year = result.get("year")
        if not year or not str(year).isdigit():
            continue
        year = int(year)
        if best_year is None or year < best_year:
            best_year = year
            raw_title = result.get("title", "")
            best_album = raw_title.split(" - ", 1)[-1] if " - " in raw_title else raw_title

    if best_year is None:
        return {}
    return {"album": best_album, "year": best_year}


def _artist_name_variants(artist: str) -> list[str]:
    """Common naming mistakes worth retrying automatically before giving
    up: a leading "The " present/missing is the single most common
    band-name mismatch ("The Evesdroppers" vs "Evesdroppers" -- confirmed
    real case where a filename's album-title prefix was mistaken for the
    artist name, and the mismatch alone was enough to make Discogs return
    nothing)."""
    variants = [artist]
    if artist.lower().startswith("the "):
        variants.append(artist[4:])
    else:
        variants.append(f"The {artist}")
    return variants


def _discogs_oldest_match(token: str, artist: str, title: str) -> dict:
    """Second layer, tried only when AcoustID left Year unresolved but did
    identify an artist/title. Searches Discogs' master-release index
    (type=master, scoped with artist=/track= so results are already
    restricted to releases containing this track) and takes the OLDEST
    master year found -- a Discogs "master release" is itself the
    canonical original version across every pressing/reissue, the same
    role MusicBrainz's release-group original-date plays for the AcoustID
    layer. Confirmed on real data: correctly returns 2001 for Train's
    "Drops of Jupiter" across all matching masters. Retries with a
    "The "-toggled artist variant before giving up entirely (see
    _artist_name_variants)."""
    for variant in _artist_name_variants(artist):
        result = _discogs_search(token, variant, title)
        if result:
            return result
    return {}


def _write_tags(path: Path, tags: dict) -> None:
    audio = mutagen.File(path, easy=True)
    if audio is None:
        return
    if tags.get("artist"):
        audio["artist"] = tags["artist"]
    if tags.get("title"):
        audio["title"] = tags["title"]
    if tags.get("album"):
        audio["album"] = tags["album"]
    if tags.get("year"):
        audio["date"] = str(tags["year"])
    audio.save()


def _finalize_row_tags(conn, row_path: str, tags: dict, needs_review_reason: str | None) -> None:
    """Single write path for all three retag phases -- replaces three
    near-identical UPDATE statements. Sets needs_review whenever a
    filename-derived candidate tripped artist_normalize.is_garbled_name()
    (never for a catalog-sourced AcoustID/Discogs/MusicBrainz result -- see
    that function's own docstring), so restructure.py quarantines it
    instead of creating a folder from a guess."""
    conn.execute(
        "UPDATE files SET artist=?, title=?, album=?, genre=?, year=?, retag_done=1, "
        "needs_review=?, needs_review_reason=? WHERE path=?",
        (
            tags.get("artist"), tags.get("title"), tags.get("album"), tags.get("genre"), tags.get("year"),
            1 if needs_review_reason else 0, needs_review_reason, row_path,
        ),
    )


# Fields confirmed present on files sourced from this library's original
# downloads that carry no useful music metadata -- torrent-site search
# links, downloader-tool signatures. Stripped from every file regardless
# of match outcome, independent of the artist/title/album/year resolution
# above.
JUNK_TAG_KEYS = ["comment", "description"]


def _strip_junk_tags(path: Path) -> bool:
    audio = mutagen.File(path, easy=True)
    if audio is None:
        return False
    removed = False
    for key in JUNK_TAG_KEYS:
        if key in audio:
            del audio[key]
            removed = True
    if removed:
        audio.save()
    return removed


def _trimmed_duration(path: Path, stated_duration: float) -> float | None:
    """Detects trailing silence and returns the real content length, or
    None if no significant trailing silence was found (i.e. the stated
    duration was already accurate).

    Bug fix: confirmed real case -- old console-game audio rips (PS2-era
    THPS soundtrack extracts) padded with a fixed-length silent tail, e.g.
    a file reporting 471s duration whose actual song ends at 246s. AcoustID
    lookups use duration as part of candidate matching, so submitting the
    padded 471s caused real, correctly-fingerprinted songs (confirmed:
    Ozomatli, AFI, Ramones, Motorhead tracks) to return zero results
    despite being genuinely in AcoustID's database. Only worth the ffmpeg
    decode cost as a retry for files AcoustID already failed on -- most
    files have no such padding and the first attempt (stated duration)
    already works.
    """
    try:
        result = subprocess.run(
            ["ffmpeg", "-i", str(path), "-af", "silencedetect=noise=-35dB:d=0.5", "-f", "null", "-"],
            capture_output=True, text=True, timeout=60,
        )
    except (subprocess.SubprocessError, OSError):
        return None

    segments = []  # (start, end)
    pending_start = None
    for line in result.stderr.splitlines():
        if "silence_start:" in line:
            try:
                pending_start = float(line.rsplit("silence_start:", 1)[1].strip())
            except ValueError:
                pending_start = None
        elif "silence_end:" in line and pending_start is not None:
            try:
                end = float(line.split("silence_end:", 1)[1].split("|", 1)[0].strip())
                segments.append((pending_start, end))
            except ValueError:
                pass
            pending_start = None
    if pending_start is not None:
        # A trailing silence run with no explicit silence_end reaches EOF.
        segments.append((pending_start, stated_duration))

    if not segments:
        return None

    # Walk backward from the end, merging consecutive silence segments
    # separated by only a brief blip of sound (a fade-out dipping briefly
    # above the threshold still counts as part of the trailing padding,
    # not real content) -- confirmed a real case where using only the
    # single LAST silence_start under-trimmed by ~24s because of exactly
    # such a blip, and the under-trimmed duration still didn't match.
    segments.sort()
    earliest_start = segments[-1][0]
    next_seg_start = segments[-1][0]
    for start, end in reversed(segments[:-1]):
        if next_seg_start - end > 3:  # real gap of actual content, stop merging
            break
        earliest_start = start
        next_seg_start = start

    if earliest_start < stated_duration - 2 and (stated_duration - earliest_start) > 5:
        return earliest_start
    return None


def _acoustid_phase_worker(row: dict, apikey: str) -> dict:
    """Runs entirely off local state + AcoustID (no beets, no shared
    connection) so it's safe to call from a thread pool. Returns enough
    info for the main thread to write the DB row and decide whether the
    Discogs layer or the slow MusicBrainz fallback is still needed."""
    path = Path(row["path"])
    tags = _read_back_tags(path)

    if apikey and row["fingerprint"]:
        stated_duration = row["duration"] or 0
        fallback = _acoustid_oldest_match(apikey, row["fingerprint"], stated_duration)
        if not fallback:
            # Retry once with a trailing-silence-corrected duration --
            # see _trimmed_duration docstring for the real case this fixed
            # (padded PS2-era game rips whose stated duration included a
            # long silent tail, causing AcoustID's duration-based matching
            # to reject otherwise-correct fingerprint matches).
            trimmed = _trimmed_duration(path, stated_duration)
            if trimmed is not None:
                fallback = _acoustid_oldest_match(apikey, row["fingerprint"], trimmed)
        if fallback:
            existing_artist = (tags.get("artist") or "").strip().lower()
            acoustid_artist = (fallback.get("artist") or "").strip().lower()
            artist_agrees = not existing_artist or existing_artist == acoustid_artist
            if artist_agrees:
                if not tags.get("artist"):
                    tags["artist"] = fallback.get("artist")
                if not tags.get("title"):
                    tags["title"] = fallback.get("title")
                current_year = tags.get("year")
                if fallback.get("year") is not None and (current_year is None or fallback["year"] < current_year):
                    tags["year"] = fallback["year"]
                    tags["album"] = fallback.get("album")
                    tags["_acoustid_applied"] = True

    # Free, local, zero-latency -- try this BEFORE routing to Discogs vs.
    # MusicBrainz, so a successful split gives Discogs something to search
    # on instead of jumping straight past it to the expensive last resort
    # (this exact ordering bug sent files straight to MusicBrainz whenever
    # AcoustID alone found nothing, even when the filename made the artist
    # obvious -- confirmed on real "The Evesdroppers"/game-soundtrack files).
    if not tags.get("artist"):
        before_artist = tags.get("artist")
        tags = _split_artist_title_from_filename(path, tags)
        if tags.get("artist") != before_artist:
            tags["_filename_split_applied"] = True
            if artist_normalize.is_garbled_name(tags.get("artist")):
                tags["_needs_review_reason"] = "garbled_artist"
            elif artist_normalize.is_garbled_name(tags.get("title")):
                tags["_needs_review_reason"] = "garbled_title"

    tags = artist_normalize.apply_collab_normalization(tags)
    _write_tags(path, tags)
    tags["_junk_stripped"] = _strip_junk_tags(path)
    tags["path"] = row["path"]
    # Routing for the caller: no artist/title at all -> only the slow
    # MusicBrainz text search (phase 3) can identify the file from
    # scratch. A garbled filename-derived guess is done regardless of
    # missing year -- there's nothing real to search a catalog for, and
    # it's headed for quarantine (needs_review), not a folder. Artist/title
    # known and clean but year still missing -> Discogs (phase 2) has
    # something to search on. Both known -> done.
    if tags.get("_needs_review_reason"):
        pass
    elif not tags.get("artist") or not tags.get("title"):
        tags["needs_musicbrainz_fallback"] = True
    elif tags.get("year") is None:
        tags["needs_discogs"] = True
    return tags


# ~24x the documented ~12.7s/file typical case -- generous enough to never
# false-trigger on a normal slow file, but bounded so a single hung
# MusicBrainz/beets call can't stall the whole (already documented as
# hours-long) fallback batch indefinitely.
BEET_IMPORT_TIMEOUT_SECONDS = 300


def _run_beet_import(path: str, env: dict, logger) -> None:
    """Runs `beet import` for one file with a hard timeout. The stdout
    drain runs on a background thread so a hang can be detected and the
    process killed without blocking forever on `for line in process.stdout`
    (which only returns once the child exits). On timeout, the caller's
    existing downstream handling needs no special-casing: _read_back_tags
    just finds nothing new was written, and the existing filename-split
    fallback runs exactly as it already does for any other "beets found
    nothing" outcome."""
    # `python -m beets` rather than a bare "beet": the console script is
    # only on PATH when the venv is activated (the TUI's run_tui.sh does
    # `source .venv/bin/activate`, but a direct `.venv/bin/python cleanup.py
    # retag` invocation does not), and this used to die with
    # FileNotFoundError: 'beet' the moment a file hit the MusicBrainz
    # fallback. `-m beets` resolves through the same interpreter that's
    # already running, so it works regardless of PATH/activation.
    process = subprocess.Popen(
        [sys.executable, "-m", "beets", "import", "--quiet", "--group-albums", path],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
    )

    def _drain():
        for line in process.stdout:
            line = line.strip()
            if line:
                logger.info(f"[RETAG]   {line}")

    reader = threading.Thread(target=_drain, daemon=True)
    reader.start()
    reader.join(timeout=BEET_IMPORT_TIMEOUT_SECONDS)
    if reader.is_alive():
        logger.warning(f"[RETAG]   beet import timed out after {BEET_IMPORT_TIMEOUT_SECONDS}s, killing: {path}")
        process.kill()
        reader.join(timeout=10)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        logger.error(f"[RETAG]   beet import process wouldn't die after kill: {path}")


def retag(library_root: Path, excluded_root: Path, dry_run: bool = False, workers: int = ACOUSTID_WORKERS) -> dict:
    from . import checkpoint
    from . import log as log_module

    logger = log_module.get()
    stats = {
        "files_attempted": 0, "files_processed": 0, "acoustid_oldest_applied": 0,
        "discogs_oldest_applied": 0, "junk_tags_stripped": 0, "musicbrainz_fallback_used": 0,
        "filename_split_applied": 0, "collab_normalized": 0, "needs_review": 0,
    }
    _write_beets_config()
    env = os.environ.copy()
    env["BEETSDIR"] = str(BEETS_DIR)

    apikey = _acoustid_api_key()
    if not apikey:
        logger.info("[RETAG] no AcoustID API key found -- oldest-release check disabled, MusicBrainz-only")

    with checkpoint.connect(excluded_root) as conn:
        rows = [
            dict(r) for r in conn.execute(
                "SELECT * FROM files WHERE type='Song' AND dedup_status IN ('unique','keeper') AND retag_done=0"
            )
        ]
        stats["files_attempted"] = len(rows)
        logger.info(f"[RETAG] {len(rows)} file(s) to process, AcoustID-first with {workers} workers")

        if dry_run:
            return stats

        needs_fallback: list[dict] = []
        needs_discogs: list[tuple[dict, dict]] = []

        # Phase 1: fast-first pass, parallelized -- network I/O bound (the
        # AcoustID lookup) for files with a fingerprint and a key, or free
        # local-only work (existing-tag read + filename split) for those
        # without one, so a thread pool suits both. All DB writes happen
        # here in the main thread as futures complete, never inside a
        # worker. Every row goes through the same worker regardless of
        # fingerprint/key availability -- see this module's "Bug fix 3" for
        # why skipping straight to the slow MusicBrainz fallback for those
        # rows was wrong.
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {pool.submit(_acoustid_phase_worker, row, apikey): row for row in rows}

            for future in as_completed(futures):
                row = futures[future]
                tags = future.result()
                relative = Path(row["path"]).relative_to(library_root)

                if tags.pop("_acoustid_applied", False):
                    stats["acoustid_oldest_applied"] += 1
                    logger.info(f"[RETAG] {relative} -> acoustid oldest-release: artist={tags.get('artist')!r} album={tags.get('album')!r} year={tags.get('year')!r}")
                if tags.pop("_junk_stripped", False):
                    stats["junk_tags_stripped"] += 1
                if tags.pop("_filename_split_applied", False):
                    stats["filename_split_applied"] += 1
                    logger.info(f"[RETAG] {relative} -> split from filename: artist={tags.get('artist')!r} title={tags.get('title')!r}")
                if tags.pop("_collab_normalized", False):
                    stats["collab_normalized"] += 1
                    logger.info(f"[RETAG] {relative} -> collab-normalized: artist={tags.get('artist')!r} title={tags.get('title')!r}")
                needs_review_reason = tags.pop("_needs_review_reason", None)
                if needs_review_reason:
                    stats["needs_review"] += 1
                    logger.info(f"[RETAG] {relative} -> needs review ({needs_review_reason}): artist={tags.get('artist')!r} title={tags.get('title')!r}")

                if not needs_review_reason and tags.pop("needs_musicbrainz_fallback", False):
                    needs_fallback.append(row)
                    continue
                if not needs_review_reason and tags.pop("needs_discogs", False):
                    needs_discogs.append((row, tags))
                    continue

                _finalize_row_tags(conn, row["path"], tags, needs_review_reason)
                conn.commit()
                stats["files_processed"] += 1
                logger.info(f"[RETAG] {relative} -> artist={tags.get('artist')!r} title={tags.get('title')!r} year={tags.get('year')!r} (acoustid only, no MusicBrainz needed)")

        # Phase 2: Discogs, second layer -- only for files AcoustID
        # identified (artist/title known) but couldn't find a Year for.
        # Sequential: Discogs' authenticated search limit is ~60/min, and
        # this is a much smaller subset than the full file list, so
        # staying sequential here is simple and safe rather than risking
        # a 429 from parallelizing it too.
        discogs_token = _discogs_api_key()
        if needs_discogs and not discogs_token:
            logger.info(f"[RETAG] no Discogs token found -- skipping second layer for {len(needs_discogs)} file(s)")
        for row, tags in needs_discogs:
            path = Path(row["path"])
            relative = path.relative_to(library_root)

            if discogs_token:
                discogs_result = _discogs_oldest_match(discogs_token, tags["artist"], tags["title"])
                if discogs_result and discogs_result.get("year") is not None:
                    tags["year"] = discogs_result["year"]
                    tags["album"] = discogs_result.get("album")
                    stats["discogs_oldest_applied"] += 1
                    logger.info(f"[RETAG] {relative} -> discogs oldest-release: album={tags.get('album')!r} year={tags.get('year')!r}")
                    _write_tags(path, tags)

            # Discogs never touches artist -- collab normalization already
            # happened in Phase 1 for anything routed here (needs_discogs
            # is only ever set once artist/title are already known).
            _finalize_row_tags(conn, row["path"], tags, None)
            conn.commit()
            stats["files_processed"] += 1
            if not (discogs_token and tags.get("year") is not None):
                logger.info(f"[RETAG] {relative} -> no Discogs match either, year stays {tags.get('year')!r}")

        # Phase 3: slow MusicBrainz fallback, strictly sequential -- only
        # for files AcoustID couldn't identify at all (no artist/title),
        # respecting MusicBrainz's ~1 req/sec public API guidance.
        stats["musicbrainz_fallback_used"] = len(needs_fallback)
        if needs_fallback:
            logger.info(f"[RETAG] {len(needs_fallback)} file(s) need the slow MusicBrainz fallback")

        for row in needs_fallback:
            path = row["path"]
            _run_beet_import(path, env, logger)

            tags = _read_back_tags(Path(path))
            needs_review_reason = None
            if not tags.get("artist"):
                before = dict(tags)
                tags = _split_artist_title_from_filename(Path(path), tags)
                if tags.get("artist") != before.get("artist"):
                    stats["filename_split_applied"] += 1
                    logger.info(f"[RETAG]   split from filename (no catalog match at all): artist={tags.get('artist')!r} title={tags.get('title')!r}")
                    if artist_normalize.is_garbled_name(tags.get("artist")):
                        needs_review_reason = "garbled_artist"
                    elif artist_normalize.is_garbled_name(tags.get("title")):
                        needs_review_reason = "garbled_title"
                    _write_tags(Path(path), tags)

            if not needs_review_reason:
                before_collab = (tags.get("artist"), tags.get("title"))
                tags = artist_normalize.apply_collab_normalization(tags)
                if (tags.get("artist"), tags.get("title")) != before_collab:
                    stats["collab_normalized"] += 1
                    logger.info(f"[RETAG]   collab-normalized: artist={tags.get('artist')!r} title={tags.get('title')!r}")
                    _write_tags(Path(path), tags)

            if _strip_junk_tags(Path(path)):
                stats["junk_tags_stripped"] += 1

            if needs_review_reason:
                stats["needs_review"] += 1
                logger.info(f"[RETAG]   needs review ({needs_review_reason}): artist={tags.get('artist')!r} title={tags.get('title')!r}")

            _finalize_row_tags(conn, path, tags, needs_review_reason)
            conn.commit()
            stats["files_processed"] += 1
            relative = Path(path).relative_to(library_root)
            logger.info(
                f"[RETAG]   musicbrainz fallback: {relative} -> "
                f"artist={tags.get('artist')!r} album={tags.get('album')!r} title={tags.get('title')!r} year={tags.get('year')!r}"
            )

    return stats
