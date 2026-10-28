"""Robot Framework keywords driving a ``kestrel-sim`` process (Kestrel spec 04 §5).

The simulator prints ``{"event":"ready","port":N}`` once listening, then JSON lines: one response per
stdin command (objects with an ``ok`` key) interleaved with ``{"event":"telegram",...}`` lines. A reader
thread splits the two streams; telegram events are kept for the log and for assertions.
"""

import json
import queue
import subprocess
import threading
import time

from robot.api import logger
from robot.libraries.BuiltIn import BuiltIn


class KestrelSim:
    ROBOT_LIBRARY_SCOPE = "SUITE"

    def __init__(self):
        self._process = None
        self._responses = queue.Queue()
        self._events = []
        self._events_lock = threading.Lock()
        self._stderr = []
        self._port = None
        self._ready = threading.Event()

    # --- lifecycle -------------------------------------------------------------------------------

    def start_simulator(self, scenario, sim=None, port=0, timeout=10):
        """Starts kestrel-sim with ``scenario`` and returns the UDP port it listens on.

        ``sim`` defaults to the ``${SIM}`` variable passed by ctest.
        """
        if self._process is not None:
            raise AssertionError("simulator already running")
        sim = sim or BuiltIn().get_variable_value("${SIM}")
        command = [sim, "--port", str(port), "--scenario", scenario]
        logger.info("Starting: %s" % " ".join(command))
        self._process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        self._ready.clear()
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()
        if not self._ready.wait(float(timeout)):
            self._kill()
            raise AssertionError("kestrel-sim did not report ready within %ss:\n%s" % (timeout, "".join(self._stderr)))
        if self._port is None:
            self._kill()
            raise AssertionError("kestrel-sim failed to start:\n%s" % "".join(self._stderr))
        logger.info("Simulator listening on 127.0.0.1:%d" % self._port)
        return self._port

    def stop_simulator(self, timeout=10):
        """Sends ``quit``, waits for the process and logs the telegram trace."""
        if self._process is None:
            return
        try:
            if self._process.poll() is None:
                self.send_simulator_command("quit", timeout=timeout)
                self._process.wait(float(timeout))
        except Exception as error:  # still clean up and keep the trace
            logger.warn("Stopping kestrel-sim: %s" % error)
            self._kill()
        finally:
            self._log_trace()
            rc = self._process.returncode
            self._process = None
            self._port = None
        if rc not in (0, None):
            raise AssertionError("kestrel-sim exited with %s:\n%s" % (rc, "".join(self._stderr)))

    def simulator_port(self):
        return self._port

    # --- commands --------------------------------------------------------------------------------

    def send_simulator_command(self, cmd, timeout=10, expect_ok=True, **fields):
        """Sends one JSON command and returns the response object."""
        if self._process is None or self._process.poll() is not None:
            raise AssertionError("simulator is not running")
        request = dict(fields)
        request["cmd"] = cmd
        line = json.dumps(request)
        logger.debug("sim <- %s" % line)
        self._process.stdin.write(line + "\n")
        self._process.stdin.flush()
        try:
            response = self._responses.get(timeout=float(timeout))
        except queue.Empty:
            raise AssertionError("no response from kestrel-sim to %s" % line)
        logger.debug("sim -> %s" % json.dumps(response))
        if _truthy(expect_ok) and not response.get("ok"):
            raise AssertionError("simulator command %s failed: %s" % (line, response.get("error")))
        return response

    def press_programming_button(self, device):
        """Toggles the programming mode of a simulated device; returns the new state."""
        return self.send_simulator_command("press-prog-button", device=device)["progMode"]

    def get_simulated_device(self, device):
        """Returns address, progMode, serial, maskVersion, restartCount and groupObjects."""
        return self.send_simulator_command("device", device=device)

    def get_simulated_group_value(self, device, ga):
        return self.send_simulator_command("group-value", device=device, ga=ga)["value"]

    def set_simulated_group_value(self, device, ga, value):
        """The device application changes a value (sent on the bus if the object has the T flag)."""
        return self.send_simulator_command("group-value", device=device, ga=ga, value=value)["value"]

    def simulated_group_value_should_be(self, device, ga, expected):
        actual = self.get_simulated_group_value(device, ga)
        if _norm_hex(actual) != _norm_hex(expected):
            raise AssertionError("%s %s: expected %s, got %s" % (device, ga, expected, actual))

    def read_simulated_memory(self, device, address, length):
        return self.send_simulator_command("memory", device=device, address=address, length=int(length))["data"]

    def get_simulator_status(self):
        return self.send_simulator_command("status")

    def wait_until_simulator_has_clients(self, count=1, timeout=10):
        """Waits until at least ``count`` tunnel connections are open."""
        deadline = time.monotonic() + float(timeout)
        while True:
            clients = self.get_simulator_status()["clients"]
            if clients >= int(count):
                return clients
            if time.monotonic() > deadline:
                raise AssertionError("expected %s tunnel client(s), have %s" % (count, clients))
            time.sleep(0.05)

    def wait_until_simulator_has_no_clients(self, timeout=10):
        """Waits until every tunnel connection is closed (e.g. after a previous CLI run exited)."""
        deadline = time.monotonic() + float(timeout)
        while self.get_simulator_status()["clients"] != 0:
            if time.monotonic() > deadline:
                raise AssertionError("tunnel clients still connected after %ss" % timeout)
            time.sleep(0.05)

    def get_simulator_telegrams(self, since=0):
        """Telegram events received so far (index >= since).

        A status round trip first: responses follow the event lines on stdout, so every telegram put
        on the bus before this call has been read once the response arrives.
        """
        self.get_simulator_status()
        with self._events_lock:
            return [e for e in self._events if e.get("index", 0) >= int(since)]

    def get_simulator_telegram_count(self):
        self.get_simulator_status()
        with self._events_lock:
            return len(self._events)

    # --- internals -------------------------------------------------------------------------------

    def _read_stdout(self):
        for line in self._process.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                message = json.loads(line)
            except ValueError:
                logger.console("kestrel-sim: unexpected output: %s" % line)
                continue
            event = message.get("event")
            if event == "ready":
                self._port = int(message["port"])
                self._ready.set()
            elif event == "error":
                self._stderr.append(message.get("message", "") + "\n")
                self._ready.set()
            elif event == "telegram":
                with self._events_lock:
                    self._events.append(message)
            else:
                self._responses.put(message)
        self._ready.set()

    def _read_stderr(self):
        for line in self._process.stderr:
            self._stderr.append(line)

    def _kill(self):
        if self._process and self._process.poll() is None:
            self._process.kill()
            self._process.wait()

    def _log_trace(self):
        with self._events_lock:
            events = list(self._events)
        rows = ["%5s  %-9s -> %-9s  %-20s %-34s %s" % ("index", "source", "dest", "tpdu", "apci", "data")]
        for e in events:
            rows.append(
                "%5s  %-9s -> %-9s  %-20s %-34s %s"
                % (e.get("index"), e.get("source"), e.get("destination"), e.get("tpdu", ""), e.get("apci", ""),
                   e.get("data", ""))
            )
        logger.info("Simulator telegram trace (%d telegrams):\n%s" % (len(events), "\n".join(rows)))


def _norm_hex(text):
    return "".join(str(text).split()).lower()


def _truthy(value):
    if isinstance(value, str):
        return value.lower() not in ("false", "no", "0", "")
    return bool(value)
