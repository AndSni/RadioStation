"""Stage: systematic artist-folder-name deduplication -- replaces the
one-off manual script run earlier this project (32 hand-found groups,
merged via a throwaway Bash-invoked Python script, never reusable and
blind to anything added since). Confirmed real problem: Illenium had 5
separate folders, Kayzo had 7, purely from stylization/typo variants never
caught systematically.

Also retroactively applies collab-credit splitting (artist_normalize.
split_credited_artists) to whatever's CURRENTLY in the artist column,
regardless of when it got there. This matters because retag.py's own
collab-normalization only ever runs on a row the moment it's first
retagged (retag_done was 0) -- it can never reach back and fix a row that
was already retagged in an earlier run, before that logic existed.
Confirmed real case: "BABYMETAL, Electric Callboy" and "BABYMETAL, Tom
Morello" sat as two separate folders because both files were retagged
months before collab-splitting was added, so simply re-running `retag`
never touches them again (retag_done is already 1). This stage's job is
"fix up whatever's currently in the artist column" regardless of history,
so it's the right place for this retroactive fix -- and since the affected
files here had frequently already been through `restructure`, this stage
now physically moves already-restructured files into the corrected folder
too, not just pre-restructure DB+tag updates.

Three things this stage auto-applies vs. only reports, by confidence:
  - exact_key() match (Unicode-decomposed, alphanumeric-only -- collapses
    "Motorhead"/"Motörhead", pure case/spacing variants) -> auto-merge,
    safe and unambiguous.
  - fuzzy (difflib ratio >= threshold) match on artists that DIDN'T
    exact-match -> written to a report only, NEVER auto-merged. Last
    time's fuzzy pass had real false positives (Arion/Ayrion, Iron
    Mack/Iron Mask) that required listening to actual files to resolve --
    that judgment call stays with a human, via manual_merge() below.
  - collab-credit splitting (artist_normalize.split_credited_artists) --
    ONLY the unambiguous "feat."/"ft."/"featuring" form auto-applies. A
    bare comma/&/// separator is NEVER auto-applied here either, for the
    same reason as the fuzzy pass: confirmed real damage where "Crosby,
    Stills, Nash & Young" (an actual band name) and "Yusuf / Cat Stevens"
    (one person's own stage name) got silently mangled into a fake "X feat.
    Y" credit, indistinguishable by punctuation alone from a genuine
    multi-artist credit like "BABYMETAL, Electric Callboy". These
    ambiguous candidates go to collab_credit_report.json instead, for a
    human to confirm via manual_collab_split() below.
"""

import difflib
import json
import shutil
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

import mutagen

from . import artist_normalize, checkpoint, text_normalize
from .restructure import _sanitize

DEFAULT_FUZZY_THRESHOLD = 0.85

_ELIGIBLE_WHERE = (
    "type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0 "
    "AND energy IS NOT NULL AND artist_folder_done=0"
)

# Only what this whole module ever reads off a row -- not SELECT * -- since
# by this stage every row carries its full fingerprint text *and* its
# energy_raw JSON, and on a first full run this query can capture nearly
# the entire settled library at once, making it the single largest
# avoidable in-memory cost in the pipeline at a large library's scale.
_ELIGIBLE_COLUMNS = "path, artist, title, restructure_done"


def _rewrite_tags_on_disk(path: Path, artist: str | None = None, title: str | None = None) -> bool:
    """Keeps on-disk tags in sync with the DB's artist/title columns --
    nothing else ever rewrites these again after this stage runs, so
    skipping this would let a later re-scan silently revert the change.
    mutagen.File can raise (not just return None) on a malformed/unreadable
    file -- caught here so one bad file doesn't abort the whole merge
    pass, same defensive style as retag.py's own catalog-lookup
    functions."""
    try:
        audio = mutagen.File(path, easy=True)
        if audio is None:
            return False
        if artist is not None:
            audio["artist"] = artist
        if title is not None:
            audio["title"] = title
        audio.save()
        return True
    except Exception:  # noqa: BLE001 -- a bad tag write shouldn't abort the merge
        return False


def _log_merge(excluded_root: Path, action: str, **fields) -> None:
    """Own separate audit log, deliberately NOT the shared dedup undo log --
    a plain artist-tag rename isn't a file move in the sense undo.py's
    generic replay assumes (it also resets dedup_status/dedup_cluster_id/
    dedup_score/duplicate_of, which are unrelated to an artist rename and
    would be wrongly touched by a generic replay)."""
    with checkpoint.artist_merge_log_path(excluded_root).open("a") as f:
        f.write(json.dumps({"action": action, "timestamp": datetime.now(timezone.utc).isoformat(), **fields}) + "\n")


def _move_to_canonical_folder(library_root: Path, src: Path, new_artist: str, new_title: str) -> Path:
    """Physically relocates an already-restructured file into
    library_root/<new_artist>/<new_artist> - <new_title>.ext -- filename
    rebuilt from scratch (same convention restructure.py itself uses)
    rather than string-substituting within the old filename, which would
    silently leave a stale name if the old filename didn't happen to
    contain the exact old artist substring (e.g. different casing), or
    wouldn't reflect a title change at all (collab-splitting changes both
    artist and title together)."""
    dest_dir = library_root / _sanitize(new_artist)
    dest_dir.mkdir(parents=True, exist_ok=True)
    filename_stem = f"{_sanitize(new_artist)} - {_sanitize(new_title)}"
    dest = dest_dir / f"{filename_stem}{src.suffix}"
    counter = 1
    while dest.exists() and dest.resolve() != src.resolve():
        dest = dest_dir / f"{filename_stem}__{counter}{src.suffix}"
        counter += 1
    if dest.resolve() != src.resolve():
        shutil.move(str(src), str(dest))
    return dest


def _prune_if_empty(folder: Path) -> None:
    if folder.is_dir() and not any(folder.iterdir()):
        folder.rmdir()


def auto_merge(
    library_root: Path, excluded_root: Path, fuzzy_threshold: float = DEFAULT_FUZZY_THRESHOLD, dry_run: bool = False
) -> dict:
    from . import log as log_module

    logger = log_module.get()
    stats = {
        "considered": 0, "collab_split": 0, "auto_merged_groups": 0,
        "auto_merged_rows": 0, "files_moved": 0, "fuzzy_candidates": 0,
        "ambiguous_collab_candidates": 0,
    }

    with checkpoint.connect(excluded_root) as conn:
        rows = [dict(r) for r in conn.execute(f"SELECT {_ELIGIBLE_COLUMNS} FROM files WHERE {_ELIGIBLE_WHERE}")]
        stats["considered"] = len(rows)

        # Step 1: split any collab credit in every row's CURRENT artist
        # string, regardless of when it was written. `effective_artist` is
        # what the rest of this pass groups/merges on; `pending_title`
        # records the feat.-updated title for rows that actually had a
        # credit to split (empty for everything else).
        effective_artist: dict[str, str] = {}
        pending_title: dict[str, str] = {}
        for row in rows:
            artist = row["artist"] or ""
            if not artist:
                continue
            primary, collaborators = artist_normalize.split_credited_artists(artist)
            effective_artist[row["path"]] = primary
            if collaborators:
                pending_title[row["path"]] = artist_normalize.merge_collaborators_into_title(row["title"], collaborators)

        # Step 2: group by exact_key() of the EFFECTIVE (post-split) artist
        # -- this is what folds "BABYMETAL, Electric Callboy" and
        # "BABYMETAL, Tom Morello" (and any plain "BABYMETAL" rows) into
        # one group, alongside the existing stylization/typo collapsing.
        by_exact_key: dict[str, Counter] = defaultdict(Counter)
        rows_by_effective: dict[str, list[dict]] = defaultdict(list)
        for row in rows:
            eff = effective_artist.get(row["path"])
            if not eff:
                continue
            by_exact_key[text_normalize.artist_exact_key(eff)][eff] += 1
            rows_by_effective[eff].append(row)

        canonical_by_effective: dict[str, str] = {}
        for key, variants in by_exact_key.items():
            if len(variants) <= 1:
                continue
            canonical = min(variants, key=lambda v: (-variants[v], len(v), v))
            for variant in variants:
                if variant != canonical:
                    canonical_by_effective[variant] = canonical

        # Step 3: apply whatever changed -- collab split, exact-merge
        # canonicalization, or both together -- to every affected row.
        touched_old_folders: set[Path] = set()
        for row in rows:
            path = row["path"]
            eff = effective_artist.get(path)
            if not eff:
                continue
            final_artist = canonical_by_effective.get(eff, eff)
            original_artist = row["artist"] or ""
            final_title = pending_title.get(path, row["title"])

            artist_changed = final_artist != original_artist
            title_changed = path in pending_title
            if not artist_changed and not title_changed:
                continue

            if path in pending_title:
                stats["collab_split"] += 1
            if final_artist != eff or eff != original_artist:
                stats["auto_merged_rows"] += 1

            logger.info(
                f"[ARTIST-FOLDER] {Path(path).name}: artist {original_artist!r} -> {final_artist!r}"
                + (f", title -> {final_title!r}" if title_changed else "")
            )

            if dry_run:
                continue

            if row["restructure_done"]:
                src = Path(path)
                old_folder = src.parent
                dest = _move_to_canonical_folder(library_root, src, final_artist, final_title or src.stem)
                if dest != src:
                    stats["files_moved"] += 1
                    touched_old_folders.add(old_folder)
                final_relative = dest.relative_to(library_root)
                conn.execute(
                    "UPDATE files SET artist=?, title=?, path=?, final_relative_path=? WHERE path=?",
                    (final_artist, final_title, str(dest), str(final_relative), path),
                )
                _rewrite_tags_on_disk(dest, artist=final_artist, title=final_title)
            else:
                conn.execute("UPDATE files SET artist=?, title=? WHERE path=?", (final_artist, final_title, path))
                _rewrite_tags_on_disk(Path(path), artist=final_artist, title=final_title)

            conn.commit()
            _log_merge(
                excluded_root, "artist_folder_auto_merge",
                path=path, old_artist=original_artist, new_artist=final_artist,
            )

        for folder in touched_old_folders:
            _prune_if_empty(folder)

        if canonical_by_effective:
            groups = defaultdict(int)
            for variant, canonical in canonical_by_effective.items():
                groups[canonical] += 1
            stats["auto_merged_groups"] = len(groups)

        # Fuzzy pass on whatever artist strings remain distinct after the
        # above (collab-split + exact-merge) -- re-read fresh so it
        # reflects this run's own changes, not the pre-pass snapshot.
        remaining_rows = [
            dict(r) for r in conn.execute(f"SELECT {_ELIGIBLE_COLUMNS} FROM files WHERE {_ELIGIBLE_WHERE}")
        ] if not dry_run else rows
        rows_by_artist_now: dict[str, list[dict]] = defaultdict(list)
        for row in remaining_rows:
            if row["artist"]:
                rows_by_artist_now[row["artist"]].append(row)
        surviving_artists = sorted(rows_by_artist_now)

        fuzzy_pairs = []
        for i, a in enumerate(surviving_artists):
            for b in surviving_artists[i + 1:]:
                ratio = difflib.SequenceMatcher(None, a.lower(), b.lower()).ratio()
                if ratio >= fuzzy_threshold:
                    fuzzy_pairs.append({
                        "artist_a": a, "artist_b": b, "ratio": ratio,
                        "count_a": len(rows_by_artist_now[a]), "count_b": len(rows_by_artist_now[b]),
                        "example_files_a": [r["path"] for r in rows_by_artist_now[a][:3]],
                        "example_files_b": [r["path"] for r in rows_by_artist_now[b][:3]],
                    })
        stats["fuzzy_candidates"] = len(fuzzy_pairs)
        if fuzzy_pairs:
            logger.info(f"[ARTIST-FOLDER] {len(fuzzy_pairs)} fuzzy candidate pair(s) written to report for manual review")
        checkpoint.artist_fuzzy_report_path(excluded_root).write_text(json.dumps(fuzzy_pairs, indent=2))

        # Ambiguous (comma/&///with/x/vs) collab-credit candidates, on
        # whatever artist strings remain after the safe feat./ft./featuring
        # split above -- report-only, see module docstring. Grouped by the
        # full original string so the same recurring credit (e.g. every
        # BABYMETAL/Electric-Callboy track) shows up once with its file count.
        collab_candidates = []
        for artist, artist_rows in rows_by_artist_now.items():
            candidate = artist_normalize.detect_ambiguous_collab_candidate(artist)
            if candidate is None:
                continue
            primary, collaborators = candidate
            collab_candidates.append({
                "artist": artist, "suggested_primary": primary, "suggested_collaborators": collaborators,
                "count": len(artist_rows), "example_files": [r["path"] for r in artist_rows[:3]],
            })
        stats["ambiguous_collab_candidates"] = len(collab_candidates)
        if collab_candidates:
            logger.info(
                f"[ARTIST-FOLDER] {len(collab_candidates)} ambiguous collab-credit candidate(s) "
                "written to report for manual review"
            )
        checkpoint.collab_credit_report_path(excluded_root).write_text(json.dumps(collab_candidates, indent=2))

        if not dry_run:
            conn.execute(f"UPDATE files SET artist_folder_done=1 WHERE {_ELIGIBLE_WHERE}")
            conn.commit()

    return stats


def manual_merge(library_root: Path, excluded_root: Path, from_artist: str, to_artist: str, dry_run: bool = False) -> dict:
    """Human-invoked, after reviewing an artist_fuzzy_report.json entry and
    confirming (e.g. by listening) that from_artist and to_artist really
    are the same act. Handles both pre-restructure (DB+tag rename only) and
    post-restructure (the realistic case -- these problems are usually
    spotted by looking at the *finished* folder tree) by physically moving
    files between Artist folders."""
    from . import log as log_module

    logger = log_module.get()
    stats = {"rows_updated": 0, "files_moved": 0}

    with checkpoint.connect(excluded_root) as conn:
        rows = [dict(r) for r in conn.execute("SELECT * FROM files WHERE artist=?", (from_artist,))]
        if not rows:
            logger.info(f"[ARTIST-MERGE] no rows found for artist={from_artist!r}")
            return stats

        touched_old_folders: set[Path] = set()
        for row in rows:
            stats["rows_updated"] += 1
            src = Path(row["path"])

            if row["restructure_done"]:
                if dry_run:
                    logger.info(f"[ARTIST-MERGE]   would move: {src} -> {library_root / to_artist}/")
                    continue
                old_folder = src.parent
                dest = _move_to_canonical_folder(library_root, src, to_artist, row["title"] or src.stem)
                if dest != src:
                    stats["files_moved"] += 1
                    touched_old_folders.add(old_folder)
                final_relative = dest.relative_to(library_root)
                conn.execute(
                    "UPDATE files SET artist=?, path=?, final_relative_path=? WHERE path=?",
                    (to_artist, str(dest), str(final_relative), row["path"]),
                )
                _rewrite_tags_on_disk(dest, artist=to_artist)
                logger.info(f"[ARTIST-MERGE]   moved: {src} -> {dest}")
            else:
                if dry_run:
                    logger.info(f"[ARTIST-MERGE]   would rename artist only (not yet restructured): {src}")
                    continue
                conn.execute("UPDATE files SET artist=? WHERE path=?", (to_artist, row["path"]))
                _rewrite_tags_on_disk(src, artist=to_artist)
                logger.info(f"[ARTIST-MERGE]   artist renamed (not yet restructured): {src}")

            conn.commit()
            _log_merge(
                excluded_root, "artist_manual_merge",
                path=str(src), old_artist=from_artist, new_artist=to_artist,
            )

        if not dry_run:
            for folder in touched_old_folders:
                _prune_if_empty(folder)

    return stats


def manual_collab_split(library_root: Path, excluded_root: Path, artist: str, dry_run: bool = False) -> dict:
    """Human-invoked, after reviewing a collab_credit_report.json entry and
    confirming (e.g. by listening, or just recognizing the acts) that
    `artist` really IS a multi-artist credit ("BABYMETAL, Electric
    Callboy") rather than a single act's own name that happens to contain
    a comma/&/// ("Crosby, Stills, Nash & Young", "Yusuf / Cat Stevens").
    Splits into primary + collaborators the same way the safe feat./ft.
    path does, appends "(feat. ...)" to the title, and -- like
    manual_merge() -- physically moves already-restructured files."""
    from . import log as log_module

    logger = log_module.get()
    stats = {"rows_updated": 0, "files_moved": 0}

    candidate = artist_normalize.detect_ambiguous_collab_candidate(artist)
    if candidate is None:
        logger.info(f"[ARTIST-MERGE] {artist!r} doesn't look like a collab credit (no comma/&///with/x/vs found)")
        return stats
    primary, collaborators = candidate

    with checkpoint.connect(excluded_root) as conn:
        rows = [dict(r) for r in conn.execute("SELECT * FROM files WHERE artist=?", (artist,))]
        if not rows:
            logger.info(f"[ARTIST-MERGE] no rows found for artist={artist!r}")
            return stats

        touched_old_folders: set[Path] = set()
        for row in rows:
            stats["rows_updated"] += 1
            src = Path(row["path"])
            new_title = artist_normalize.merge_collaborators_into_title(row["title"], collaborators)

            if row["restructure_done"]:
                if dry_run:
                    logger.info(f"[ARTIST-MERGE]   would split+move: {src} -> {library_root / primary}/")
                    continue
                old_folder = src.parent
                dest = _move_to_canonical_folder(library_root, src, primary, new_title or src.stem)
                if dest != src:
                    stats["files_moved"] += 1
                    touched_old_folders.add(old_folder)
                final_relative = dest.relative_to(library_root)
                conn.execute(
                    "UPDATE files SET artist=?, title=?, path=?, final_relative_path=? WHERE path=?",
                    (primary, new_title, str(dest), str(final_relative), row["path"]),
                )
                _rewrite_tags_on_disk(dest, artist=primary, title=new_title)
                logger.info(f"[ARTIST-MERGE]   split+moved: {src} -> {dest}")
            else:
                if dry_run:
                    logger.info(f"[ARTIST-MERGE]   would split artist/title only (not yet restructured): {src}")
                    continue
                conn.execute("UPDATE files SET artist=?, title=? WHERE path=?", (primary, new_title, row["path"]))
                _rewrite_tags_on_disk(src, artist=primary, title=new_title)
                logger.info(f"[ARTIST-MERGE]   split artist/title (not yet restructured): {src}")

            conn.commit()
            _log_merge(
                excluded_root, "artist_manual_collab_split",
                path=str(src), old_artist=artist, new_artist=primary, collaborators=collaborators,
            )

        if not dry_run:
            for folder in touched_old_folders:
                _prune_if_empty(folder)

    return stats
