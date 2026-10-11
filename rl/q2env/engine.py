# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""One server process, driven through a shared block and a pair of pipes.

The server (run/q2ded, see server/sv_rl.c) does nothing until a byte arrives
on its pipe; it then carries out the request in the block and sends a byte
back. A step is one server frame of 100 ms. `ask` and `wait` are apart so
that many engines can be set going before any is waited for.
"""

import os
import subprocess
import tempfile
from pathlib import Path

import numpy as np

from . import layout as L

REPO = Path(__file__).resolve().parents[2]


class EngineError(RuntimeError):
    pass


def run_dir():
    """The folder the game runs from: it holds q2ded and baseq2."""
    return Path(os.environ.get("Q2PT_RUN", REPO / "run"))


def data_dir():
    """Where datasets, weights, demos, logs and the navigation graphs go:
    never in the repository."""
    return Path(os.environ.get("Q2PT_RL_DATA", Path.home() / "q2pt-rl"))


class Engine:
    def __init__(self, cpu=None, log=None, extra=()):
        """cpu: a processor thread to keep the server on, or None.
        log: a file for the server's console output, or None to drop it.
        extra: more words for the server's command line."""
        run = run_dir()
        exe = run / "q2ded"
        if not exe.exists():
            raise EngineError(f"{exe} is not built: cmake --build build/linux --target q2ded game")
        if not (run / "baseq2" / "pak0.pak").exists():
            raise EngineError(f"no game data in {run / 'baseq2'}")

        # where the game keeps the navigation graph it builds of each map
        nav = data_dir() / "nav"
        nav.mkdir(parents=True, exist_ok=True)

        fd, self._shm = tempfile.mkstemp(prefix="q2rl-", dir="/dev/shm")
        os.ftruncate(fd, L.SHARED.itemsize)
        os.close(fd)
        self.block = np.memmap(self._shm, dtype=L.SHARED, mode="r+", shape=(1,))[0]
        self.block["magic"] = L.MAGIC
        self.block["version"] = L.VERSION
        self.block["size"] = L.SHARED.itemsize

        to_r, self._to_w = os.pipe()        # requests to the server
        self._from_r, from_w = os.pipe()    # its replies
        out = open(log, "w") if log else subprocess.DEVNULL

        def pin():
            if cpu is not None:
                os.sched_setaffinity(0, {cpu})

        self.proc = subprocess.Popen(
            [str(exe), "+set", "rl_shm", self._shm, "+set", "rl_nav", str(nav),
             "+set", "rl_fd_in", str(to_r), "+set", "rl_fd_out", str(from_w), *extra],
            cwd=run, pass_fds=(to_r, from_w), preexec_fn=pin,
            stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT)
        os.close(to_r)
        os.close(from_w)
        if log:
            out.close()
        self._waiting = False

    # ------------------------------------------------------------ requests

    def ask(self, request):
        self.block["request"] = request
        os.write(self._to_w, b"\1")
        self._waiting = True

    def wait(self):
        got = os.read(self._from_r, 1)
        self._waiting = False
        if not got:
            raise EngineError(f"the server stopped (exit code {self.proc.poll()})")
        if self.block["error"]:
            raise EngineError(self.block["error_text"].decode(errors="replace"))

    def reset(self, map, seed, skill=1, time_limit=0, demo="", flags=0, mode=0, back=""):
        self.ask_reset(map, seed, skill, time_limit, demo, flags, mode, back)
        self.wait()

    def ask_reset(self, map, seed, skill=1, time_limit=0, demo="", flags=0, mode=0, back=""):
        """mode: what the teacher is out to do (layout.MODE_). back: the maps,
        with spaces between, whose exits are not the way on."""
        b = self.block
        b["map"] = map.encode()
        b["seed"] = seed
        b["skill"] = skill
        b["time_limit"] = time_limit
        b["flags"] = flags
        b["mode"] = mode
        b["back"] = back.encode()
        b["demo"] = str(demo).encode()
        self.ask(L.REQ_RESET)

    def ask_step(self, action=None):
        """action: one choice per branch, or None for the teacher's own."""
        if action is None:
            self.block["act_teacher"] = 1
        else:
            self.block["act_teacher"] = 0
            self.block["action"] = action
        self.ask(L.REQ_STEP)

    def step(self, action=None):
        self.ask_step(action)
        self.wait()

    # --------------------------------------------------------------- close

    def close(self):
        if self.proc is None:
            return
        try:
            if self.proc.poll() is None and not self._waiting:
                self.block["request"] = L.REQ_QUIT
                os.write(self._to_w, b"\1")
                self.proc.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            pass
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self.proc = None
        os.close(self._to_w)
        os.close(self._from_r)
        del self.block
        os.unlink(self._shm)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
