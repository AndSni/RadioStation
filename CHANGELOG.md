# Changelog

All notable changes to this project are documented here. The format is based
on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/).

## [0.1] – 2026-09-01

First public release.

### Added

- **Auto DJ** with weighted category rotation and artist/title separation.
- **Schedule blocks**: day/time windows driving playlist, selection strategy
  and cart behaviour.
- **Clock wheels**: reusable hour templates with hour-anchored resume after a
  restart.
- **Cart Wall**: hotkey jingle/sweeper grid, plus schedule-driven cart
  automation in *insert* (hard cut between songs) and *overlay* modes.
- Per-cart **"in-between only"** flag — excludes a clip from overlay
  automation so jingles with their own music bed are only ever played solo
  between songs.
- **Dual-deck mixing**: equal-power automatic crossfades, idle-deck
  cue-ahead, manual override.
- **MixEngine mastering chain**: K-weighted loudness leveler, true-peak
  lookahead limiter, per-channel and master EQ, mic ducking.
- **Icecast/Shoutcast streaming** (MP3 / Ogg Vorbis / Opus).
- **Library**: SQLite store with FTS5 search, smart playlists, star ratings,
  ReplayGain analysis, loudness history.
- **Console UI**: hand-painted faders, rotary knobs, LED meters, dot-matrix
  deck displays; fully dockable panels.
- Application icon, `About` dialog, versioned window title, Linux `.desktop`
  entry and install rules.
- GitHub Actions CI building the project and running the test suite.
