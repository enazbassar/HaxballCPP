#!/usr/bin/env python3
"""haxctl - self-directing supervisor for haxball_stress.

Run it with a room and nothing else. It probes the room, starts at a middling
intensity, then walks its own ladder up or down every dwell window based on what
the tool actually reports:

  handshakes landing, no 429      -> climb (ports, cand-flood, slots, rate)
  429 or live penalty             -> drop a step, require two clean windows before climbing again
  many 503s, nothing live         -> re-probe (host gone, or we killed it)
  saturated with live sessions    -> hold
  signaling works but DTLS never lands -> fall back to ws-only mode, and leave it
                                          when the 503s stop

No profiles to choose. Ctrl-C shuts the child down cleanly.

  ./haxctl.py --room AMb-u9r7oNI
"""

import argparse
import atexit
import os
import queue
import re
import signal
import subprocess
import sys
import threading
import time

BIN = "./build_stress/haxball_stress"

# light -> brutal. level index is the ladder rung; 'extra' adds slots on top.
LADDER = [
    dict(slots=1, rate=1, burst=1, hold=90_000,  ports=2,  max_cand=2, cand_flood=32,  ping=1000, mode="full"),
    dict(slots=1, rate=1, burst=1, hold=120_000, ports=4,  max_cand=2, cand_flood=64,  ping=500,  mode="full"),
    dict(slots=2, rate=1, burst=2, hold=150_000, ports=6,  max_cand=3, cand_flood=128, ping=300,  mode="full"),
    dict(slots=2, rate=1, burst=2, hold=180_000, ports=8,  max_cand=3, cand_flood=256, ping=200,  mode="full"),
    dict(slots=3, rate=2, burst=3, hold=180_000, ports=10, max_cand=3, cand_flood=320, ping=200,  mode="full"),
    dict(slots=4, rate=2, burst=3, hold=240_000, ports=12, max_cand=3, cand_flood=384, ping=150,  mode="full"),
]
START_LEVEL = 2
WS_SLOTS = 12

STAT_RE = re.compile(
    r"slots (\d+)/(\d+) \| 429 (\d+) 503 (\d+) reject (\d+) \| "
    r"hs (\d+)/(\d+) churn (\d+) \| held (\d+) dtls (\d+) \| penalty (\d+)s")
PROBE_RE = re.compile(r"PROBE: (.*)")

STOP = False
CHILD = None


def on_signal(_s, _f):
    global STOP
    STOP = True


def kill_child(grace=8):
    global CHILD
    p = CHILD
    if not p or p.poll() is not None:
        return
    try:
        os.killpg(p.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        p.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        p.wait(timeout=5)


class Log:
    def __init__(self, path):
        self.fh = open(path, "a", buffering=1) if path else None

    def __call__(self, msg):
        line = f"[{time.strftime('%H:%M:%S')}] {msg}"
        print(line, flush=True)
        if self.fh:
            self.fh.write(line + "\n")

    def close(self):
        if self.fh:
            self.fh.close()


class Window:
    """Deltas over one dwell window."""
    __slots__ = ("d429", "d503", "dhs", "dchurn", "pen", "dtls", "held", "slots_ok")

    def __init__(self):
        for k in self.__slots__:
            setattr(self, k, 0)


def common(a, cfg, slots):
    args = [BIN, "--room", a.room, "--slots", str(slots),
            "--hold", str(cfg["hold"]), "--rate", str(cfg["rate"]),
            "--burst", str(cfg["burst"]), "--ports", str(cfg["ports"]),
            "--max-cand", str(cfg["max_cand"]), "--cand-flood", str(cfg["cand_flood"]),
            "--ping", str(cfg["ping"]), "--version", str(a.version),
            "--mode", cfg["mode"]]
    if a.token:
        args += ["--token", a.token]
    if a.password:
        args += ["--password", a.password]
    return args


def probe(a):
    args = [BIN, "--room", a.room, "--probe", "--version", str(a.version)]
    if a.token:
        args += ["--token", a.token]
    if a.password:
        args += ["--password", a.password]
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=90)
    except subprocess.TimeoutExpired:
        return "error: timeout"
    for line in p.stdout.splitlines():
        m = PROBE_RE.search(line)
        if m:
            return m.group(1).strip()
    return "error: no verdict"


class Ctl:
    def __init__(self, a, log):
        self.a = a
        self.log = log
        self.level = START_LEVEL
        self.extra = 0
        self.mode = "full"
        self.clean = 0
        self.ws_quiet = 0
        self.offline_streak = 0
        self.prev = Window()
        self.q = queue.Queue()

    # ---- config
    def cfg(self):
        c = dict(LADDER[min(self.level, len(LADDER) - 1)])
        c["mode"] = self.mode
        return c

    def slots(self):
        base = self.cfg()["slots"] if self.mode == "full" else WS_SLOTS
        return max(1, min(base + self.extra, self.a.max_slots))

    def spawn(self):
        global CHILD
        args = common(self.a, self.cfg(), self.slots())
        CHILD = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True, bufsize=1, start_new_session=True)
        self.q = queue.Queue()
        q = self.q
        threading.Thread(target=lambda: [q.put(l) for l in CHILD.stdout], daemon=True).start()
        self.log(f"start pid {CHILD.pid} | level {self.level} extra {self.extra} "
                 f"mode {self.mode} slots {self.slots()}")

    # ---- decisions
    def step(self, w):
        if self.mode == "ws":
            if w.d429 > 0 or w.pen > 0:
                if self.extra > 0:
                    self.extra -= 1
                    return "DOWN", f"429+{w.d429} penalty {w.pen}s"
                if self.level > 0:
                    self.level -= 1
                    return "DOWN", f"429+{w.d429} penalty {w.pen}s"
                return "HOLD", "429 at floor"
            if w.d503 == 0:
                self.ws_quiet += 1
                if self.ws_quiet >= 3:
                    self.ws_quiet = 0
                    self.mode = "full"
                    self.level = START_LEVEL
                    self.extra = 0
                    return "SWITCH", "ws quiet, retry full"
                return "HOLD", f"ws quiet {self.ws_quiet}/3"
            self.ws_quiet = 0
            return "HOLD", f"ws pressure 503+{w.d503}"

        if w.pen > 0 or w.d429 > 0:
            self.clean = 0
            if self.extra > 0:
                self.extra -= 1
                return "DOWN", f"429+{w.d429} penalty {w.pen}s"
            if self.level > 0:
                self.level -= 1
                return "DOWN", f"429+{w.d429} penalty {w.pen}s"
            return "HOLD", "429 at floor"

        self.clean += 1

        if w.dhs == 0 and w.dtls == 0 and w.d503 >= 3:
            return "REPROBE", f"dead: 503+{w.d503}, nothing live"

        if w.dhs > 0:
            if self.clean < 2:
                return "HOLD", f"warming, clean {self.clean}/2"
            if self.extra < self.a.max_slots and self.level >= len(LADDER) - 1:
                self.extra += 1
                return "UP", f"hs+{w.dhs}, extra slots"
            if self.level < len(LADDER) - 1:
                self.level += 1
                return "UP", f"hs+{w.dhs}"
            return "HOLD", f"top, hs+{w.dhs}"

        if w.dtls > 0:
            return "HOLD", f"saturated, {w.dtls} live"

        if w.d503 >= 3:
            self.ws_quiet += 1
            if self.ws_quiet >= 3:
                self.ws_quiet = 0
                self.mode = "ws"
                self.extra = 0
                return "SWITCH", "no DTLS landing, ws-only fallback"
            return "HOLD", f"no dtls {self.ws_quiet}/3"

        return "HOLD", "steady"

    # ---- main
    def run(self):
        backoff = 30
        while not STOP:
            v = probe(self.a)
            self.log(f"probe: {v}")
            if not v.lower().startswith("host alive"):
                self.offline_streak += 1
                if self.level > 1:
                    self.level -= 1
                wait = min(120, backoff * self.offline_streak)
                self.log(f"host not usable, sleeping {wait}s")
                for _ in range(wait):
                    if STOP:
                        return
                    time.sleep(1)
                continue

            backoff = 30
            self.offline_streak = 0
            self.spawn()
            self.prev = Window()
            started = time.time()

            while not STOP and CHILD.poll() is None:
                try:
                    line = self.q.get(timeout=1)
                except queue.Empty:
                    line = None
                if line:
                    m = STAT_RE.search(line)
                    if m:
                        g = [int(x) for x in m.groups()]
                        self.cur = g
                    elif "reject" in line:
                        self.log(line.rstrip())

                if time.time() - started < self.a.dwell:
                    continue

                g = getattr(self, "cur", None)
                if not g:
                    started = time.time()
                    continue

                w = Window()
                w.held, _, a429, a503, _, hs, _, churn, dtls, _, pen = g
                w.d429 = a429 - self.prev.d429
                w.d503 = a503 - self.prev.d503
                w.dhs = hs - self.prev.dhs
                w.dchurn = churn - self.prev.dchurn
                w.dtls, w.pen = dtls, pen
                self.prev = Window()
                for k in ("d429", "d503", "dhs", "dchurn", "dtls", "pen"):
                    setattr(self.prev, k, {"d429": a429, "d503": a503, "dhs": hs,
                                           "dchurn": churn, "dtls": dtls, "pen": pen}[k])

                act, why = self.step(w)
                self.log(f"lvl {self.level}+{self.extra} mode {self.mode} slots {self.slots()} "
                         f"| 429+{w.d429} 503+{w.d503} hs+{w.dhs} churn+{w.dchurn} "
                         f"live {w.dtls} pen {w.pen}s -> {act} ({why})")

                started = time.time()
                if act in ("UP", "DOWN", "SWITCH"):
                    kill_child()
                    self.spawn()
                elif act == "REPROBE":
                    kill_child()
                    break

            if STOP:
                return
            if CHILD.poll() is not None:
                self.log(f"child exited rc={CHILD.returncode}, restarting in 5s")
                for _ in range(5):
                    if STOP:
                        return
                    time.sleep(1)


def main():
    ap = argparse.ArgumentParser(description="self-directing haxball_stress supervisor")
    ap.add_argument("--room", required=True)
    ap.add_argument("--token")
    ap.add_argument("--password")
    ap.add_argument("--version", default="9")
    ap.add_argument("--max-slots", type=int, default=8)
    ap.add_argument("--dwell", type=int, default=120, help="seconds per evaluation window")
    ap.add_argument("--log", default="haxctl.log")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    log = Log(a.log)
    ctl = Ctl(a, log)
    atexit.register(kill_child)

    if a.dry_run:
        print(" ".join(common(a, ctl.cfg(), ctl.slots())))
        return 0

    log(f"room {a.room} | start level {START_LEVEL} | max slots {a.max_slots} | dwell {a.dwell}s")
    try:
        ctl.run()
    finally:
        kill_child()
        log("stopped")
        log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
