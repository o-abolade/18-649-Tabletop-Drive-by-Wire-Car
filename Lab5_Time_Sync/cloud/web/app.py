"""Lab 5 extra credit: vehicle monitor (cloud node).

The Pi (N1) mirrors the vehicle's Raft log into Redis; this app shows it in a
browser. Redis keys, all written by the Pi's uplink (pi_cockpit/uplink.c):

  vehicle:log        stream, one entry per committed log entry:
                     run, term, index, type, code, replayed, global_us, wall_ms
  vehicle:heartbeat  hash with a 5 s TTL, rewritten every 1 s: Pi wall clock,
                     global time, Raft role/term/leader/commit, held entries,
                     time-sync figures, and the network state as JSON
  vehicle:last       the same hash without a TTL: what the GUI shows, greyed
                     out, under the staleness banner while the Pi is away
  vehicle:uplink     the publisher's bookkeeping (last acknowledged entry)

The vehicle's log is the source of truth: this app only reads, except for
"clear" (after a full cold restart of the vehicle the log starts again at 1).

Environment: REDIS_URL, GUI_USER, GUI_PASSWORD, SECRET_KEY, COOKIE_SECURE
(1 behind HTTPS; 0 only for local testing). REDIS_URL=fake:// runs on an
in-memory Redis with demo data, for testing without a server.
"""
import asyncio
import contextlib
import hmac
import json
import os
import pathlib
import time

from fastapi import FastAPI, Form, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, RedirectResponse
from starlette.middleware.sessions import SessionMiddleware

STATIC = pathlib.Path(__file__).parent / "static"
REDIS_URL = os.environ.get("REDIS_URL", "redis://localhost:6379/0")
GUI_USER = os.environ["GUI_USER"]
GUI_PASSWORD = os.environ["GUI_PASSWORD"]
SECRET_KEY = os.environ["SECRET_KEY"]
COOKIE_SECURE = os.environ.get("COOKIE_SECURE", "1") == "1"

STALE_MS = 3000      # heartbeat older than this: show the staleness banner
HB_TTL_MS = 5000     # the Pi sets EXPIRE 5 on vehicle:heartbeat each time it writes it
LOG_ROWS = 60
PUSH_S = 1.0         # websocket update period

if REDIS_URL == "fake://":
    import fakeredis  # test only

    r = fakeredis.aioredis.FakeRedis(decode_responses=True)
else:
    import redis.asyncio as aioredis

    r = aioredis.from_url(REDIS_URL, decode_responses=True)



# ---------------- local demo data (REDIS_URL=fake://) ----------------

async def _demo_data():
    await r.delete("vehicle:log", "vehicle:heartbeat", "vehicle:last")
    now = int(time.time() * 1000)
    events = [
        (1, 1, "COCKPIT_OK", 0), (2, 1, "CANLINK_drivetrain_A_DOWN", 0),
        (3, 2, "NODE_cockpit_DOWN", 1), (4, 2, "COCKPIT_ERROR", 1),
        (5, 2, "NODE_cockpit_UP", 0), (6, 2, "COCKPIT_OK", 0),
    ]
    for idx, term, typ, rep in events:
        await r.xadd("vehicle:log", {"run": "demo", "term": term, "index": idx, "type": typ,
                                     "code": "0x00", "replayed": rep, "global_us": idx * 1000000,
                                     "wall_ms": now - (10 - idx) * 1000})
    state = {"nodes": [{"id": 1, "name": "cockpit", "up": 1, "A": 1, "B": 1},
                       {"id": 2, "name": "steering", "up": 1, "A": 1, "B": 1},
                       {"id": 3, "name": "drivetrain", "up": 1, "A": 0, "B": 1}],
             "cockpit_ok": 1}
    while True:
        if demo_paused:
            await asyncio.sleep(0.1)
            continue
        hb = {"wall_ms": int(time.time() * 1000), "global_us": 123456789, "run": "demo",
              "role": "follower", "term": 2, "leader": 2, "commit": 6, "published": 6,
              "held": 0, "replayed": 2, "sync": 1, "drift_ppb": -2136, "err_us": 4,
              "state": json.dumps(state)}
        await r.hset("vehicle:heartbeat", mapping=hb)
        await r.expire("vehicle:heartbeat", 5)
        await r.hset("vehicle:last", mapping=hb)
        await asyncio.sleep(1)


demo_paused = False   # tests stop the demo writer to control the heartbeat


@contextlib.asynccontextmanager
async def lifespan(_app):
    task = asyncio.create_task(_demo_data()) if REDIS_URL == "fake://" else None
    yield
    if task:
        task.cancel()


app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None, lifespan=lifespan)
app.add_middleware(SessionMiddleware, secret_key=SECRET_KEY, https_only=COOKIE_SECURE,
                   same_site="strict", max_age=12 * 3600)


def logged_in(conn) -> bool:
    return conn.session.get("user") == GUI_USER


_last_fresh_ms = None   # server time of the newest heartbeat seen


async def snapshot() -> dict:
    global _last_fresh_ms
    hb = await r.hgetall("vehicle:heartbeat")
    pttl = await r.pttl("vehicle:heartbeat")
    last = hb or await r.hgetall("vehicle:last")
    entries = await r.xrevrange("vehicle:log", "+", "-", count=LOG_ROWS)
    total = await r.xlen("vehicle:log")
    now_ms = int(time.time() * 1000)
    # Age from the key's remaining TTL, i.e. on the server's clock only. The
    # Pi has no RTC: after a reboot its wall clock is minutes behind until NTP
    # steps it, so its wall_ms must not decide staleness.
    if hb and pttl > 0:
        age_ms = max(0, HB_TTL_MS - pttl)
        _last_fresh_ms = now_ms - age_ms
    elif _last_fresh_ms is not None:
        age_ms = now_ms - _last_fresh_ms
    else:   # nothing seen since this app started: fall back to the Pi's clock
        age_ms = now_ms - int(last["wall_ms"]) if last.get("wall_ms") else None
    stale = not hb or age_ms is None or age_ms > STALE_MS
    try:
        state = json.loads(last.get("state", "{}"))
    except ValueError:
        state = {}
    return {
        "now_ms": now_ms,
        "stale": stale,
        "age_ms": age_ms,
        "heartbeat": last,
        "state": state,
        "log": [{"id": i, **f} for i, f in entries],
        "log_total": total,
    }


# ---------------- pages ----------------

LOGIN_PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Vehicle monitor: sign in</title><style>
body{font-family:system-ui,Segoe UI,Arial,sans-serif;background:#f3f5f8;color:#1d2733;
display:grid;place-items:center;min-height:100vh;margin:0}
form{background:#fff;padding:28px 32px;border-radius:10px;box-shadow:0 2px 10px #0002;width:300px}
h1{font-size:18px;margin:0 0 16px}label{display:block;font-size:13px;margin:10px 0 4px}
input{width:100%;box-sizing:border-box;padding:8px;border:1px solid #c5ccd6;border-radius:6px;font-size:14px}
button{margin-top:16px;width:100%;padding:9px;border:0;border-radius:6px;background:#17365d;color:#fff;font-size:14px}
.err{color:#b3261e;font-size:13px;margin-top:10px}</style></head><body>
<form method="post" action="/login"><h1>18-649 vehicle monitor</h1>
<label for="u">User</label><input id="u" name="username" autocomplete="username" required>
<label for="p">Password</label><input id="p" name="password" type="password" autocomplete="current-password" required>
<button type="submit">Sign in</button>{error}</form></body></html>"""


@app.get("/login", response_class=HTMLResponse)
async def login_page():
    return LOGIN_PAGE.replace("{error}", "")


@app.post("/login")
async def login(request: Request, username: str = Form(...), password: str = Form(...)):
    ok = hmac.compare_digest(username.encode(), GUI_USER.encode()) & \
        hmac.compare_digest(password.encode(), GUI_PASSWORD.encode())
    if not ok:
        await asyncio.sleep(1.0)  # slow down guessing
        return HTMLResponse(LOGIN_PAGE.replace("{error}", '<p class="err">Wrong user or password.</p>'),
                            status_code=401)
    request.session["user"] = GUI_USER
    return RedirectResponse("/", status_code=303)


@app.post("/logout")
async def logout(request: Request):
    request.session.clear()
    return RedirectResponse("/login", status_code=303)


@app.get("/")
async def index(request: Request):
    if not logged_in(request):
        return RedirectResponse("/login", status_code=303)
    return FileResponse(STATIC / "index.html", headers={"Cache-Control": "no-store"})


@app.get("/api/snapshot")
async def api_snapshot(request: Request):
    if not logged_in(request):
        return JSONResponse({"error": "login required"}, status_code=401)
    return await snapshot()


@app.post("/api/clear")
async def api_clear(request: Request):
    """After a full cold restart of the vehicle its log starts again at 1."""
    if not logged_in(request):
        return JSONResponse({"error": "login required"}, status_code=401)
    await r.delete("vehicle:log", "vehicle:index", "vehicle:uplink")
    return {"ok": True}


@app.get("/healthz")
async def healthz():
    return {"ok": True}


@app.websocket("/ws")
async def ws(websocket: WebSocket):
    if not logged_in(websocket):
        await websocket.close(code=4401)
        return
    await websocket.accept()
    try:
        while True:
            await websocket.send_json(await snapshot())
            await asyncio.sleep(PUSH_S)
    except (WebSocketDisconnect, RuntimeError):
        pass
