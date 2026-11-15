"""Tests for the vehicle monitor, on an in-memory Redis (no server needed).

    REDIS_URL=fake:// GUI_USER=team GUI_PASSWORD=pw SECRET_KEY=k COOKIE_SECURE=0 \\
        python -m pytest -q test_app.py      (or: python test_app.py)
"""
import os
import time

os.environ.setdefault("REDIS_URL", "fake://")
os.environ.setdefault("GUI_USER", "team")
os.environ.setdefault("GUI_PASSWORD", "pw")
os.environ.setdefault("SECRET_KEY", "test-key")
os.environ.setdefault("COOKIE_SECURE", "0")

from fastapi.testclient import TestClient  # noqa: E402

import app as monitor  # noqa: E402


def login(c, password="pw"):
    return c.post("/login", data={"username": "team", "password": password},
                  follow_redirects=False)


def test_everything_needs_login():
    with TestClient(monitor.app) as c:
        assert c.get("/", follow_redirects=False).status_code == 303
        assert c.get("/api/snapshot").status_code == 401
        assert c.post("/api/clear").status_code == 401
        try:
            with c.websocket_connect("/ws") as ws:
                ws.receive_json()
            raise AssertionError("websocket accepted without login")
        except Exception as e:  # closed with 4401
            assert "4401" in str(e) or "WebSocketDisconnect" in type(e).__name__


def test_wrong_password_rejected():
    with TestClient(monitor.app) as c:
        assert login(c, "nope").status_code == 401
        assert c.get("/api/snapshot").status_code == 401


def test_dashboard_and_snapshot():
    with TestClient(monitor.app) as c:
        assert login(c).status_code == 303
        assert "vehicle monitor" in c.get("/").text
        time.sleep(1.2)   # the demo heartbeat task has written once
        s = c.get("/api/snapshot").json()
        assert s["log_total"] == 6
        assert s["log"][0]["index"] == "6"            # newest first
        assert sum(e["replayed"] == "1" for e in s["log"]) == 2
        assert [n["name"] for n in s["state"]["nodes"]] == ["cockpit", "steering", "drivetrain"]
        assert s["heartbeat"]["leader"] == "2"
        assert s["stale"] is False


def test_websocket_streams_snapshots():
    with TestClient(monitor.app) as c:
        login(c)
        with c.websocket_connect("/ws") as ws:
            first = ws.receive_json()
            assert "log" in first and "stale" in first


def test_stale_when_heartbeat_expires():
    import asyncio

    with TestClient(monitor.app) as c:
        login(c)
        time.sleep(1.2)
        # The Pi goes away: its heartbeat key expires, vehicle:last stays
        asyncio.run(monitor.r.delete("vehicle:heartbeat"))
        s = c.get("/api/snapshot").json()
        assert s["stale"] is False or s["heartbeat"]  # demo task may rewrite it
        monitor.STALE_MS = -1   # any age counts as old
        s = c.get("/api/snapshot").json()
        assert s["stale"] is True
        assert s["state"]["nodes"]          # last known state still shown
        monitor.STALE_MS = 3000


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok  ", name)
