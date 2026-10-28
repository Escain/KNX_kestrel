"""Robot Framework keywords running ``kestrel-cli`` (path from the ``${CLI}`` variable)."""

import json
import subprocess

from robot.api import logger
from robot.libraries.BuiltIn import BuiltIn


class KestrelCli:
    ROBOT_LIBRARY_SCOPE = "SUITE"

    def __init__(self):
        self._background = {}
        self._next_handle = 1

    def _cli(self):
        return BuiltIn().get_variable_value("${CLI}")

    def run_cli(self, *args, expected_rc=0, timeout=60):
        """Runs kestrel-cli in text mode, checks the exit code, returns stripped stdout."""
        rc, stdout, stderr = self._run(list(args), timeout)
        self._check_rc(args, rc, expected_rc, stdout, stderr)
        return stdout.strip()

    def run_cli_json(self, *args, expected_rc=0, timeout=60):
        """Runs kestrel-cli with --json, checks the exit code, returns the parsed JSON object."""
        rc, stdout, stderr = self._run(list(args) + ["--json"], timeout)
        self._check_rc(args, rc, expected_rc, stdout, stderr)
        lines = [line for line in stdout.splitlines() if line.strip()]
        if not lines:
            raise AssertionError("kestrel-cli printed nothing")
        return json.loads(lines[-1])

    def run_cli_for_id(self, *args, prefix=None, timeout=60):
        """Runs a creating command in text mode and returns the printed id (checked against ``prefix``)."""
        created = self.run_cli(*args, timeout=timeout)
        if prefix and not created.startswith(prefix + "-"):
            raise AssertionError("expected a %s-… id, got %r" % (prefix, created))
        return created

    def start_cli(self, *args):
        """Starts kestrel-cli --json in the background (e.g. monitor, program-ia); returns a handle."""
        command = [self._cli()] + list(args) + ["--json"]
        logger.info("Starting: %s" % " ".join(command))
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                   encoding="utf-8")
        handle = self._next_handle
        self._next_handle += 1
        self._background[handle] = (process, args)
        return handle

    def wait_for_cli(self, handle, expected_rc=0, timeout=60):
        """Waits for a background kestrel-cli; returns its stdout as a list of JSON objects."""
        process, args = self._background.pop(int(handle))
        try:
            stdout, stderr = process.communicate(timeout=float(timeout))
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()
            raise AssertionError("kestrel-cli %s did not finish within %ss\nstdout:\n%s\nstderr:\n%s"
                                 % (" ".join(args), timeout, stdout, stderr))
        self._log(args, process.returncode, stdout, stderr)
        self._check_rc(args, process.returncode, expected_rc, stdout, stderr)
        return [json.loads(line) for line in stdout.splitlines() if line.strip()]

    def _run(self, args, timeout):
        command = [self._cli()] + [str(a) for a in args]
        try:
            completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                       timeout=float(timeout))
        except subprocess.TimeoutExpired as error:
            raise AssertionError("kestrel-cli %s timed out after %ss" % (" ".join(args), error.timeout))
        self._log(args, completed.returncode, completed.stdout, completed.stderr)
        return completed.returncode, completed.stdout, completed.stderr

    @staticmethod
    def _log(args, rc, stdout, stderr):
        logger.info("$ kestrel-cli %s\n[exit %s]\n%s%s" % (" ".join(str(a) for a in args), rc, stdout,
                                                          ("stderr:\n" + stderr) if stderr else ""))

    @staticmethod
    def _check_rc(args, rc, expected_rc, stdout, stderr):
        if rc != int(expected_rc):
            raise AssertionError("kestrel-cli %s exited with %s (expected %s)\nstdout:\n%s\nstderr:\n%s"
                                 % (" ".join(str(a) for a in args), rc, expected_rc, stdout, stderr))
