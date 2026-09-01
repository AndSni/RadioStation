"""Collaboration-credit normalization and garbled-name detection for
retag.py, plus the artist-folder-name systematic dedup used by
artist_folder_normalize.py.

Confirmed real cases driving this module: "BABYMETAL, Electric Callboy" and
"BABYMETAL, Tom Morello" were being treated as two entirely separate
artists (and folders) instead of both folding under BABYMETAL -- a
multi-artist credit string stored as one literal artist tag. And
"PapaSean-BlackPlanetMusic" (a filename-derived guess with no real word
boundary) was turned into a real Artist folder instead of being flagged.

Bug fix (real damage confirmed on the live library): comma/&//-based
auto-splitting was applied unconditionally, which silently corrupted real
single-act identities that happen to contain one of those characters --
"Crosby, Stills, Nash & Young" (an actual band name) became "Crosby" +
"(feat. Stills, Nash, Young)"; "Yusuf / Cat Stevens" (one person's own
official stage name) became "Yusuf" + "(feat. Cat Stevens)". A bare
standalone "x" separator is worse still: it matched the literal letter "X"
inside "Generation X" (Billy Idol's earlier band), silently dropping it to
"Generation". Comma/&//-based splitting has NO way to distinguish "this is
a genuine multi-artist credit" from "this IS the act's real name" by
punctuation alone -- unlike "feat."/"ft."/"featuring", which is essentially
never part of a permanent act's own identity. So only the keyword-based
form auto-applies now (split_credited_artists, used by retag.py and
artist_folder_normalize.py's auto_merge()); the punctuation-based form is
report-only (detect_ambiguous_collab_candidate, surfaced in
artist_folder_normalize.py's collab_credit_report.json for a human to
confirm via manual_collab_split()), mirroring the same auto-vs-report
confidence split already used for artist-name-typo merging.
"""

import re

# Single-artist strings that would otherwise be false-split by either
# pattern below (their real name literally contains a separator
# character). Deliberately small and explicit -- extend only as real false
# positives are found, not maintained by guesswork.
COLLAB_SPLIT_EXCEPTIONS = {"ac/dc"}

# Unambiguous -- "X feat. Y"/"X featuring Y"/"X ft. Y" is essentially never
# part of a permanent act's own name, so this auto-applies everywhere.
# "featuring" is listed before "feat"/"feat." so it matches whole rather
# than being cut short at "feat"; the punctuated form of each abbreviation
# is listed before its bare form so "feat." consumes the period as part of
# the separator instead of leaving a stray leading "." on the next segment
# (a \bfeat\.?\b-style pattern looks equivalent but actually fails to
# consume the period: \b requires a word/non-word transition, and there
# isn't one between "." and the following space).
_SAFE_COLLAB_SEPARATOR = re.compile(
    r"\s*(?:\bfeaturing\b|\bfeat\.|\bfeat\b|\bft\.|\bft\b)\s*",
    re.IGNORECASE,
)

# Ambiguous -- comma/&/// and bare "with"/"x"/"vs" are all common in real
# band names too (see module docstring's confirmed false positives), so
# these are NEVER auto-applied -- only detected for a report a human
# reviews (detect_ambiguous_collab_candidate).
_AMBIGUOUS_COLLAB_SEPARATOR = re.compile(
    r"\s*(?:,|&|/|\bwith\b|\bx\b|\bvs\.|\bvs\b)\s*",
    re.IGNORECASE,
)


def split_credited_artists(artist: str) -> tuple[str, list[str]]:
    """Returns (primary_artist, [collaborator, ...]) using ONLY the
    unambiguous feat./ft./featuring separator -- safe to auto-apply
    everywhere. Empty collaborator list if artist is a known exception, or
    nothing matches."""
    if not artist or artist.strip().lower() in COLLAB_SPLIT_EXCEPTIONS:
        return artist, []
    parts = [p.strip() for p in _SAFE_COLLAB_SEPARATOR.split(artist) if p.strip()]
    if len(parts) < 2:
        return artist, []
    return parts[0], parts[1:]


def detect_ambiguous_collab_candidate(artist: str) -> tuple[str, list[str]] | None:
    """Report-only detection using comma/&/// /with/x/vs -- NEVER
    auto-applied (see module docstring). Returns (primary, [collaborators])
    if the pattern matches at all, for a human to review via
    artist_folder_normalize.py's collab_credit_report.json and confirm
    with manual_collab_split(); None if nothing matches."""
    if not artist or artist.strip().lower() in COLLAB_SPLIT_EXCEPTIONS:
        return None
    parts = [p.strip() for p in _AMBIGUOUS_COLLAB_SEPARATOR.split(artist) if p.strip()]
    if len(parts) < 2:
        return None
    return parts[0], parts[1:]


def merge_collaborators_into_title(title: str | None, collaborators: list[str]) -> str | None:
    """Appends "(feat. X, Y)" to title, skipping any collaborator already
    present as a substring (case-insensitive) -- keeps this idempotent
    across re-runs, so a file already normalized doesn't accumulate
    "(feat. X)(feat. X)" on a second retag pass."""
    if not title or not collaborators:
        return title
    lowered_title = title.lower()
    missing = [c for c in collaborators if c.lower() not in lowered_title]
    if not missing:
        return title
    return f"{title} (feat. {', '.join(missing)})"


def apply_collab_normalization(tags: dict) -> dict:
    """The single call site every retag.py phase uses right before writing
    tags. No-op (returns tags unchanged) if the artist string doesn't
    contain a collaboration credit."""
    artist = tags.get("artist")
    if not artist:
        return tags
    primary, collaborators = split_credited_artists(artist)
    if not collaborators:
        return tags
    tags["artist"] = primary
    tags["title"] = merge_collaborators_into_title(tags.get("title"), collaborators)
    tags["_collab_normalized"] = True
    return tags


# A concatenated-words filename guess (no real word boundary) tends to have
# no spaces at all, be longer than a real single word, and switch case
# internally more than once -- e.g. "PapaSean-BlackPlanetMusic" has 3
# lower->upper transitions ("aS", "nB", "tP") and zero spaces. A real artist
# name this long either has spaces or is a single deliberately-styled word
# (rarely more than one internal case transition, e.g. "DragonForce").
_GARBLED_MIN_LENGTH = 12
_GARBLED_MIN_TRANSITIONS = 2


def is_garbled_name(candidate: str | None) -> bool:
    """Heuristic for "this filename-derived guess is probably not a real
    artist/title, quarantine it instead of guessing." Deliberately NEVER
    applied to a catalog-sourced (AcoustID/Discogs/MusicBrainz) result --
    only to candidates that came from splitting a raw filename."""
    if not candidate or " " in candidate or len(candidate) <= _GARBLED_MIN_LENGTH:
        return False
    transitions = sum(
        1 for a, b in zip(candidate, candidate[1:]) if a.islower() and b.isupper()
    )
    return transitions >= _GARBLED_MIN_TRANSITIONS
