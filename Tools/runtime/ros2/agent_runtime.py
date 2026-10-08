"""Own/reuse a UDP Agent without terminating an external process.

Agent readiness is verified by an XRCE GET_INFO/INFO_ACTIVITY exchange, not by
an occupied port. All socket checks are nonblocking and run from the ROS loop.
"""
import errno
import socket
import subprocess
import time
import queue
import threading
import re
import os


class AgentRuntime:
    PROBE = bytes([0x80, 0, 0, 0, 2, 1, 8, 0, 0, 10, 0xff, 0xfd, 2, 0, 0, 0])

    def __init__(self, executable, event):
        self.executable = executable
        self.event = event
        self.process = None
        self.socket = None
        self.port = 8888
        self.state = 'unchecked'
        self.deadline = 0
        self.next_probe = 0
        self.verified_at = 0
        self.logs = queue.Queue(maxsize=100)

    def status(self, error=''):
        return {'event': 'agent', 'state': self.state, 'port': self.port,
                'owned': self.process is not None, 'verified': self.state == 'running',
                'pid': self.process.pid if self.process is not None else None,
                'error': error}

    def notify(self, error=''):
        self.event(self.status(error))

    def open_probe(self):
        if self.socket:
            self.socket.close()
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setblocking(False)
        self.socket.connect(('127.0.0.1', self.port))

    def start(self, port):
        port = int(port)
        if not 1 <= port <= 65535:
            raise ValueError('Agent port must be between 1 and 65535')
        if self.process is not None:
            if self.port != port:
                raise RuntimeError('Stop this runtime\'s Agent before changing the port')
            return self.status()
        # A port check is only a prerequisite. Reuse requires an XRCE reply.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as check:
            try:
                check.bind(('0.0.0.0', port))
                occupied = False
            except OSError as exc:
                if exc.errno != errno.EADDRINUSE:
                    raise
                occupied = True
        self.port = port
        if not occupied:
            if not self.executable:
                distro = os.environ.get('ROS_DISTRO', 'jazzy')
                raise RuntimeError(f'Agent missing; run Tools/scripts/build_agent.sh {distro}, then reconnect')
            self.process = subprocess.Popen(
                [str(self.executable), 'udp4', '--port', str(port), '-v', '4'],
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, errors='replace', bufsize=1)
            threading.Thread(target=self.read_logs, args=(self.process.stdout,), daemon=True).start()
        self.open_probe()
        self.state = 'starting'
        self.deadline = time.monotonic() + 5
        self.next_probe = 0
        self.verified_at = 0
        self.notify()
        return self.status()

    def stop(self):
        if self.process is None:
            raise RuntimeError('External Agent is reused; this runtime cannot stop it')
        if self.process.poll() is None:
            self.process.terminate()
        self.state = 'stopping'
        self.deadline = time.monotonic() + 2
        self.notify()
        return self.status()

    def tick(self):
        for _ in range(20):
            try:
                self.event({'event': 'agent_log', 'message': self.logs.get_nowait()})
            except queue.Empty:
                break
        now = time.monotonic()
        if self.process is not None and self.process.poll() is not None:
            code = self.process.returncode
            expected = self.state == 'stopping'
            self.process = None
            if self.socket:
                self.socket.close()
                self.socket = None
            self.state = 'stopped' if expected else 'error'
            self.notify('' if expected else f'Agent exited with code {code}')
            return
        if self.state == 'stopping':
            if now > self.deadline and self.process is not None:
                self.process.kill()
            return
        if self.state not in ('starting', 'running') or self.socket is None:
            return
        try:
            data = self.socket.recv(512)
            if (len(data) >= 14 and data[0] == 0x80 and data[4] == 6
                    and data[8:12] == self.PROBE[8:12] and data[12] == 0
                    and int.from_bytes(data[6:8], 'little') == len(data) - 8):
                self.verified_at = now
                if self.state != 'running':
                    self.state = 'running'
                    self.notify()
        except (BlockingIOError, ConnectionRefusedError):
            pass
        if now >= self.next_probe:
            try:
                self.socket.send(self.PROBE)
            except (BlockingIOError, ConnectionRefusedError):
                pass
            self.next_probe = now + 1
        if ((self.state == 'starting' and now > self.deadline)
                or (self.state == 'running' and now - self.verified_at > 5)):
            self.state = 'error'
            self.notify('Agent did not reply to XRCE probe; check port, process and firewall')
            if self.process is not None and self.process.poll() is None:
                self.process.terminate()

    def close(self):
        if self.socket:
            self.socket.close()
            self.socket = None
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=1)
            self.process = None

    def read_logs(self, stream):
        try:
            for line in stream:
                try:
                    self.logs.put_nowait(re.sub(r'\x1b\[[0-9;]*m', '', line).rstrip()[:2000])
                except queue.Full:
                    pass
        finally:
            stream.close()
