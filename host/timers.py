"""Timer and stopwatch kept on the Mac and sent to the Xenon in every sample as "tm": [mode, seconds, running].

Only one runs at a time: starting a timer replaces the stopwatch and vice versa. A finished timer keeps showing
TIME'S UP on the device for DONE_SECONDS, then clears itself.
"""
import threading
import time

NONE, TIMER, STOPWATCH = 0, 1, 2
DONE_SECONDS = 60


class TimerState:
    def __init__(self):
        self._lock = threading.Lock()
        self.mode = NONE
        self._end = 0.0          # timer: monotonic end time while running
        self._remaining = 0.0    # timer: seconds left while paused
        self._started = 0.0      # stopwatch: monotonic start of the current run
        self._elapsed = 0.0      # stopwatch: seconds accumulated before the current run
        self.running = False
        self._done_at = None
        self._done_unseen = False

    # ---- timer ----
    def start_timer(self, seconds):
        with self._lock:
            self.mode, self.running = TIMER, True
            self._end = time.monotonic() + seconds
            self._done_at, self._done_unseen = None, False

    def toggle_pause(self):
        with self._lock:
            now = time.monotonic()
            if self.mode == TIMER and self._done_at is None:
                if self.running:
                    self._remaining, self.running = max(0.0, self._end - now), False
                else:
                    self._end, self.running = now + self._remaining, True
            elif self.mode == STOPWATCH:
                self._toggle_stopwatch(now)

    def cancel(self):
        with self._lock:
            self.mode, self.running, self._done_at = NONE, False, None

    # ---- stopwatch ----
    def _toggle_stopwatch(self, now):
        if self.running:
            self._elapsed += now - self._started
            self.running = False
        else:
            self._started, self.running = now, True

    def stopwatch_start_stop(self):
        with self._lock:
            now = time.monotonic()
            if self.mode != STOPWATCH:
                self.mode, self._elapsed, self.running, self._done_at = STOPWATCH, 0.0, False, None
            self._toggle_stopwatch(now)

    def stopwatch_reset(self):
        with self._lock:
            if self.mode == STOPWATCH:
                self._elapsed, self._started = 0.0, time.monotonic()
                if not self.running:
                    self.mode = NONE

    # ---- reporting ----
    def value(self):
        """Seconds to display: remaining for a timer, elapsed for a stopwatch."""
        now = time.monotonic()
        if self.mode == TIMER:
            return max(0.0, self._end - now) if self.running else self._remaining
        if self.mode == STOPWATCH:
            return self._elapsed + (now - self._started if self.running else 0.0)
        return 0.0

    def sample(self):
        """[mode, seconds, running] for the device, or None when idle. Also detects a timer finishing."""
        with self._lock:
            if self.mode == NONE:
                return None
            v = self.value()
            if self.mode == TIMER and self.running and v <= 0:
                if self._done_at is None:
                    self._done_at, self._done_unseen = time.monotonic(), True
                elif time.monotonic() - self._done_at > DONE_SECONDS:
                    self.mode, self.running, self._done_at = NONE, False, None
                    return None
            return [self.mode, round(v, 2), 1 if self.running else 0]

    def consume_done(self):
        """True once after a timer finishes (for the macOS notification)."""
        with self._lock:
            done, self._done_unseen = self._done_unseen, False
            return done

    def describe(self):
        if self.mode == NONE:
            return None
        s = int(self.value() + (0.999 if self.mode == TIMER else 0))
        text = f"{s // 3600}:{s // 60 % 60:02d}:{s % 60:02d}" if s >= 3600 else f"{s // 60}:{s % 60:02d}"
        kind = "Timer" if self.mode == TIMER else "Stopwatch"
        if self._done_at is not None:
            return "Timer: TIME'S UP"
        return f"{kind}: {text}" + ("" if self.running else " (paused)")
