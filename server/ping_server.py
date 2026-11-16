#!/usr/bin/env python3
"""
Ping Monitor Server
Pings a list of hosts and exposes rolling stats via HTTP JSON endpoint.

Usage: python ping_server.py 8.8.8.8 1.1.1.1 192.168.1.1
       python ping_server.py --port 8765 --history 60 8.8.8.8
"""

import threading
import subprocess
import time
import json
import re
import argparse
import platform
import socket
import ipaddress
from http.server import HTTPServer, BaseHTTPRequestHandler
from collections import deque
from datetime import datetime

from monitor_common import config as _cfg


def resolve_host(host):
    """
    Return the resolved IP for a hostname, or None if `host` is already an
    IP literal or resolution fails. Resolving once at add-time (rather than
    letting the `ping` command re-resolve every probe) pins the target so a
    round-robin DNS name doesn't jump between IPs mid-history.
    """
    try:
        ipaddress.ip_address(host)
        return None  # already an IP literal, nothing to resolve
    except ValueError:
        pass
    try:
        return socket.gethostbyname(host)
    except socket.gaierror:
        return None


class PingMonitor:
    def __init__(self, hosts, history_size=60, interval=1.0, failure_threshold=3):
        self.hosts = hosts
        self.history_size = history_size
        self.interval = interval
        self.failure_threshold = failure_threshold
        self.data = {}
        self.lock = threading.Lock()
        self._running = True

        for host in hosts:
            resolved_ip = resolve_host(host)
            self.data[host] = {
                "history": deque(maxlen=history_size),  # None = timeout, float = ms
                "consecutive_failures": 0,
                "status": "unknown",
                "last_ping": None,
                "resolved_ip": resolved_ip,  # pinged instead of `host` when set
            }

    def ping_once(self, host):
        """Returns latency in ms, or None on timeout/failure."""
        is_windows = platform.system() == "Windows"
        if is_windows:
            cmd = ["ping", "-n", "1", "-w", "1000", host]
        else:
            cmd = ["ping", "-c", "1", "-W", "1", host]

        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=3,
            )
            if result.returncode == 0:
                # Match "time=12.3 ms" or "time<1ms" or "Zeit=5ms"
                match = re.search(r"[Tt]ime[=<](\d+\.?\d*)\s*ms", result.stdout)
                if match:
                    return float(match.group(1))
                # Windows: "Minimum = 1ms, Maximum = 1ms, Average = 1ms"
                match = re.search(r"Average\s*=\s*(\d+)ms", result.stdout)
                if match:
                    return float(match.group(1))
                return 0.5  # Replied but couldn't parse time
            return None
        except (subprocess.TimeoutExpired, FileNotFoundError, Exception):
            return None

    def monitor_host(self, host):
        target = self.data[host]["resolved_ip"] or host
        while self._running:
            start = time.monotonic()
            latency = self.ping_once(target)
            elapsed = time.monotonic() - start

            with self.lock:
                d = self.data[host]
                d["history"].append(latency)
                d["last_ping"] = datetime.now().isoformat()

                if latency is None:
                    d["consecutive_failures"] += 1
                else:
                    d["consecutive_failures"] = 0

                if d["consecutive_failures"] >= self.failure_threshold:
                    d["status"] = "down"
                elif latency is not None:
                    d["status"] = "up"

            # Sleep the remainder of the interval
            sleep_time = max(0, self.interval - elapsed)
            time.sleep(sleep_time)

    def get_stats(self):
        with self.lock:
            result = {}
            for host, d in self.data.items():
                history = list(d["history"])
                latencies = [x for x in history if x is not None]
                total = len(history)
                lost = sum(1 for x in history if x is None)

                result[host] = {
                    "status": d["status"],
                    "consecutive_failures": d["consecutive_failures"],
                    "history": history,
                    "avg_latency": sum(latencies) / len(latencies) if latencies else None,
                    "min_latency": min(latencies) if latencies else None,
                    "max_latency": max(latencies) if latencies else None,
                    "packet_loss_pct": (lost / total * 100) if total > 0 else 0,
                    "total_sent": total,
                    "last_ping": d["last_ping"],
                    "resolved_ip": d.get("resolved_ip"),
                }
            return result

    def add_host(self, host):
        """Dynamically add a host and start monitoring it. No-op if already present."""
        resolved_ip = resolve_host(host)
        with self.lock:
            if host in self.data:
                return False  # already monitored
            self.data[host] = {
                "history": deque(maxlen=self.history_size),
                "consecutive_failures": 0,
                "status": "unknown",
                "last_ping": None,
                "resolved_ip": resolved_ip,
            }
        t = threading.Thread(target=self.monitor_host, args=(host,), daemon=True)
        t.start()
        suffix = f" -> {resolved_ip}" if resolved_ip else ""
        print(f"[server] Added host: {host}{suffix}")
        return True

    def remove_host(self, host):
        """Remove a host from monitoring. Returns True if it existed."""
        with self.lock:
            if host not in self.data:
                return False
            del self.data[host]
        print(f"[server] Removed host: {host}")
        return True

    def start(self):
        for host in list(self.data.keys()):
            t = threading.Thread(target=self.monitor_host, args=(host,), daemon=True)
            t.start()
        labels = [
            f"{host} -> {d['resolved_ip']}" if d["resolved_ip"] else host
            for host, d in self.data.items()
        ]
        print(f"[server] Monitoring {len(self.data)} host(s): {', '.join(labels)}")

    def stop(self):
        self._running = False


class StatsHandler(BaseHTTPRequestHandler):
    monitor = None

    def _send_json(self, code, obj):
        body = json.dumps(obj, default=str).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", len(body))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/stats":
            self._send_json(200, self.monitor.get_stats())
        elif self.path == "/health":
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"ok")
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == "/hosts":
            length = int(self.headers.get("Content-Length", 0))
            try:
                body = json.loads(self.rfile.read(length))
                host = body.get("host", "").strip()
                if not host:
                    self._send_json(400, {"error": "missing 'host' field"})
                    return
                added = self.monitor.add_host(host)
                self._send_json(200, {"host": host, "added": added})
            except (json.JSONDecodeError, Exception) as e:
                self._send_json(400, {"error": str(e)})
        else:
            self.send_response(404)
            self.end_headers()

    def do_DELETE(self):
        # DELETE /hosts/<hostname>
        prefix = "/hosts/"
        if self.path.startswith(prefix):
            host = self.path[len(prefix):]
            removed = self.monitor.remove_host(host)
            self._send_json(200, {"host": host, "removed": removed})
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        pass  # Suppress per-request logs


def main():
    parser = argparse.ArgumentParser(
        description="Ping monitor server — pings hosts and serves stats over HTTP",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python ping_server.py 8.8.8.8 1.1.1.1
  python ping_server.py --port 9000 --history 120 192.168.1.1 8.8.8.8
        """,
    )
    parser.add_argument("hosts", nargs="+", help="Hostnames or IPs to monitor")
    parser.add_argument("--port",      type=int,   default=_cfg["port"],      help=f"HTTP port (default: {_cfg['port']})")
    parser.add_argument("--history",   type=int,   default=_cfg["history"],   help=f"Rolling window size in pings (default: {_cfg['history']})")
    parser.add_argument("--interval",  type=float, default=_cfg["interval"],  help=f"Seconds between pings per host (default: {_cfg['interval']})")
    parser.add_argument("--threshold", type=int,   default=_cfg["threshold"], help=f"Consecutive failures before marking DOWN (default: {_cfg['threshold']})")
    args = parser.parse_args()

    monitor = PingMonitor(
        args.hosts,
        history_size=args.history,
        interval=args.interval,
        failure_threshold=args.threshold,
    )
    monitor.start()

    StatsHandler.monitor = monitor

    server = HTTPServer(("0.0.0.0", args.port), StatsHandler)
    print(f"[server] Listening on http://0.0.0.0:{args.port}/stats")
    print("[server] Press Ctrl+C to stop.\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[server] Stopping...")
        monitor.stop()


if __name__ == "__main__":
    main()
