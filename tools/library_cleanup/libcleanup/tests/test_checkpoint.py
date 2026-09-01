"""Regression coverage for checkpoint.py's fix: connect() should only run
schema creation/migration once per DB path per process, not on every call
-- important because the TUI calls connect() fresh once a second while
polling progress. reset_pipeline_state() must invalidate that cache since
it moves the DB file out from under it.
"""

from libcleanup import checkpoint


def test_connect_only_migrates_once_per_path(tmp_path, monkeypatch):
    excluded_root = tmp_path / "Excluded"
    checkpoint._schema_verified.clear()

    calls = {"migrate": 0}
    original_migrate = checkpoint._migrate

    def counting_migrate(conn):
        calls["migrate"] += 1
        original_migrate(conn)

    monkeypatch.setattr(checkpoint, "_migrate", counting_migrate)

    for _ in range(5):
        with checkpoint.connect(excluded_root) as conn:
            conn.execute("SELECT 1")

    assert calls["migrate"] == 1


def test_reset_pipeline_state_forces_reverification(tmp_path, monkeypatch):
    excluded_root = tmp_path / "Excluded"
    checkpoint._schema_verified.clear()

    calls = {"migrate": 0}
    original_migrate = checkpoint._migrate

    def counting_migrate(conn):
        calls["migrate"] += 1
        original_migrate(conn)

    monkeypatch.setattr(checkpoint, "_migrate", counting_migrate)

    with checkpoint.connect(excluded_root) as conn:
        conn.execute("INSERT INTO files (path, size, mtime) VALUES ('a', 1, 1)")
    assert calls["migrate"] == 1

    checkpoint.reset_pipeline_state(excluded_root)

    db_key = str(checkpoint.checkpoint_path(excluded_root).resolve())
    assert db_key not in checkpoint._schema_verified

    with checkpoint.connect(excluded_root) as conn:
        row = conn.execute("SELECT COUNT(*) FROM files").fetchone()
        assert row[0] == 0  # fresh DB, not the pre-reset one with row 'a'

    assert calls["migrate"] == 2
