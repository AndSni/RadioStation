"""Shared "what counts as the same artist/title" vocabulary, used by
retag.py (majority-vote grouping), title_dedup.py (cross-release duplicate
grouping), and artist_folder_normalize.py (artist-folder identity) -- kept
in one place so these three never independently drift on what "the same"
means.
"""

import re
import unicodedata


def strip_diacritics(s: str) -> str:
    """"Motörhead" -> "Motorhead", "Blue Öyster Cult" -> "Blue Oyster Cult" --
    NFKD-decomposes each character into base + combining marks, then drops
    the combining marks. Run before any further normalization so accented
    and unaccented spellings of the same name always compare equal."""
    if not s:
        return s
    decomposed = unicodedata.normalize("NFKD", s)
    return "".join(c for c in decomposed if not unicodedata.combining(c))


def normalize_title(title: str | None) -> str:
    """Reduces a string to lowercase alphanumeric words only, so
    apostrophe/quote/dash/diacritic/punctuation variants all group as the
    same value. Keeps only [a-z0-9], treating everything else as a word
    separator, rather than maintaining a character-by-character denylist."""
    if not title:
        return ""
    words = re.findall(r"[a-z0-9]+", strip_diacritics(title).lower())
    return " ".join(words)


def base_title_key(title: str | None) -> str:
    """Strips parenthetical/bracketed qualifiers ("(Rock Mix)", "[Remastered
    2011]") before normalizing, so different official mixes/edits/subtitles
    of the same underlying song group together."""
    if not title:
        return ""
    stripped = re.sub(r"[\(\[\{].*?[\)\]\}]", " ", title)
    return normalize_title(stripped)


def exact_key(artist: str | None) -> str:
    """Same normalization as normalize_title(), named separately for
    artist-identity call sites (title_dedup.py grouping,
    artist_folder_normalize.py's exact-match merge) -- readability, not a
    behavior difference."""
    return normalize_title(artist)


def artist_exact_key(artist: str | None) -> str:
    """Same as exact_key(), but additionally treats a leading "the" as
    insignificant -- "The Strokes"/"Strokes", "The Who"/"Who" are the same
    act by any real-world convention (this codebase's own retag.py already
    treats a leading "The " as an interchangeable naming variant for
    Discogs retries, see _artist_name_variants there -- this is the same
    reasoning applied to folder identity). Confirmed real gap: plain
    exact_key() keeps "the" (it's alphanumeric, not punctuation), so "The
    Strokes" and "Strokes" normalize to different keys and never merge;
    difflib's fuzzy ratio between them (0.78) also falls short of the
    default 0.85 threshold, so this pair silently fell through BOTH nets
    without this dedicated rule. Artist-only -- never used for
    normalize_title()/base_title_key(), since a song TITLE legitimately
    starting with "The" ("The Man" vs "Man") could be a genuinely
    different song, not an interchangeable naming variant."""
    key = exact_key(artist)
    if key.startswith("the "):
        key = key[4:]
    return key
