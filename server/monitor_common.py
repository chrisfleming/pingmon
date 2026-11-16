"""
monitor_common — shared utilities for ping_client.py and wait_for_host.py
"""

import json
import os
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

try:
    from rich.console import Console
    from rich.live import Live
    from rich.panel import Panel
    from rich.table import Table
    from rich.text import Text
    from rich.align import Align
    from rich import box
    HAS_RICH = True
except ImportError:
    HAS_RICH = False


# Config 

CONFIG_PATH = Path.home() / ".config" / "ping-monitor" / "config.json"

DEFAULTS = {
    # Network
    "server":    "http://localhost:8765",  # client/wait: URL of the ping server
    "port":      8765,                     # server: port to listen on
    "history":   60,                       # server: rolling window (number of pings)
    "interval":  1.0,                      # server + client: seconds between pings / refresh
    "threshold": 3,                        # server: consecutive failures before marking DOWN
    # Display
    "latency_good_ms": 50,                 # green below this
    "latency_warn_ms": 150,                # yellow below this, red above
}


def load_config() -> dict:
    """
    Load ~/.config/ping-monitor/config.json, merged over DEFAULTS.
    Creates the file with defaults on first run.
    """
    if not CONFIG_PATH.exists():
        try:
            CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
            CONFIG_PATH.write_text(json.dumps(DEFAULTS, indent=2) + "\n")
            print(f"[ping-monitor] Created default config at {CONFIG_PATH}")
        except OSError:
            pass  # read-only filesystem or similar — just use defaults
        return dict(DEFAULTS)

    try:
        data = json.loads(CONFIG_PATH.read_text())
        # Unknown keys in the file are silently ignored; missing keys fall back to DEFAULTS
        return {**DEFAULTS, **{k: v for k, v in data.items() if k in DEFAULTS}}
    except (json.JSONDecodeError, OSError) as exc:
        print(f"[ping-monitor] Warning: could not read config ({exc}), using defaults.")
        return dict(DEFAULTS)


config = load_config()


# Display constants

_SPARK = " ▁▂▃▄▅▆▇█"
LATENCY_GOOD: int = config["latency_good_ms"]
LATENCY_WARN: int = config["latency_warn_ms"]

LEGEND = (
    f" ▁▂▃▄▅▆▇█ = latency (low→high)   ✗ = dropped"
    f"   green <{LATENCY_GOOD}ms · yellow <{LATENCY_WARN}ms · red ≥{LATENCY_WARN}ms"
)


# Formatting helpers

def latency_color(ms):
    if ms is None:
        return "red"
    if ms < LATENCY_GOOD:
        return "green"
    if ms < LATENCY_WARN:
        return "yellow"
    return "red"


def make_sparkline(history):
    """Return a rich Text sparkline for a list of latency values (None = dropped)."""
    display = list(history)[-40:]
    real = [v for v in display if v is not None]
    if not display:
        return Text("")
    if not real:
        return Text("✗" * len(display), style="bold red")
    max_val = max(real) or 1
    text = Text()
    for v in display:
        if v is None:
            text.append("✗", style="bold red")
        else:
            idx = int((v / max_val) * (len(_SPARK) - 1))
            text.append(_SPARK[max(0, min(idx, len(_SPARK) - 1))], style=latency_color(v))
    return text


def ms_str(val):
    return f"{val:.1f}" if val is not None else "—"


def elapsed_str(seconds):
    return f"{int(seconds // 60)}m {int(seconds % 60)}s"


# Server API

def _request(method, url, body=None, timeout=2):
    data = json.dumps(body).encode() if body else None
    req = urllib.request.Request(url, data=data, method=method)
    if data:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read()), None
    except urllib.error.URLError as e:
        return None, str(e.reason)
    except Exception as e:
        return None, str(e)


def server_alive(base_url):
    """Return True if the server responds to /health (any HTTP reply counts)."""
    try:
        with urllib.request.urlopen(base_url.rstrip("/") + "/health", timeout=2):
            return True
    except Exception:
        return False


def fetch_stats(base_url):
    return _request("GET", base_url.rstrip("/") + "/stats")


def add_host(base_url, host):
    return _request("POST", base_url.rstrip("/") + "/hosts", {"host": host})


def remove_host(base_url, host):
    return _request("DELETE", base_url.rstrip("/") + "/hosts/" + urllib.parse.quote(host, safe=""))


# Rendering

def build_stats_table(stats, highlight=None, title=None):
    """
    Build a rich Table from a stats dict returned by fetch_stats().

    highlight  — hostname to mark with ▶ and bold (used by wait_for_host)
    title      — optional markup string for the table title
    """
    table = Table(
        title=title,
        box=box.ROUNDED,
        show_header=True,
        header_style="bold cyan",
        border_style="bright_blue",
        show_lines=True,
    )
    table.add_column("Host", style="white", min_width=20, no_wrap=True)
    table.add_column("Status", min_width=8, justify="center")
    table.add_column("Last (ms)", justify="right", min_width=10)
    table.add_column("Avg (ms)", justify="right", min_width=10)
    table.add_column("Min (ms)", justify="right", min_width=10)
    table.add_column("Max (ms)", justify="right", min_width=10)
    table.add_column("Loss", justify="right", min_width=6)
    table.add_column("Graph (last 40 pings)", min_width=42)

    for host, d in stats.items():
        status = d.get("status", "unknown")
        consecutive = d.get("consecutive_failures", 0)
        history = d.get("history", [])
        is_target = host == highlight

        if status == "up":
            status_cell = Text("⬤ UP", style="bold green")
        elif status == "down":
            status_cell = Text("⬤ DOWN", style="bold red")
        else:
            status_cell = Text("⬤ …", style="bold yellow")

        last_val = next((v for v in reversed(history) if v is not None), None)
        avg = d.get("avg_latency")
        mn = d.get("min_latency")
        mx = d.get("max_latency")
        loss = d.get("packet_loss_pct", 0)
        loss_style = "green" if loss == 0 else ("yellow" if loss < 5 else "red")

        host_label = Text(
            ("▶ " if is_target else "  ") + host,
            style=("bold white" if is_target else "white"),
        )
        resolved_ip = d.get("resolved_ip")
        if resolved_ip:
            host_label.append(f"\n   {resolved_ip}", style="dim")

        table.add_row(
            host_label,
            status_cell,
            Text(ms_str(last_val), style=latency_color(last_val)),
            Text(ms_str(avg), style=latency_color(avg)),
            ms_str(mn),
            ms_str(mx),
            Text(f"{loss:.1f}%", style=loss_style),
            make_sparkline(history),
        )

        if status == "down":
            table.add_row(
                "", Text(f"  ⚠  {consecutive} consecutive failure(s)!", style="bold red"),
                "", "", "", "", "", "",
            )

    return table


def build_error_panel(server_url, error):
    msg = Text()
    msg.append("Cannot connect to server\n\n", style="bold red")
    msg.append(f"  URL : {server_url}\n", style="white")
    msg.append(f"  Err : {error}\n\n", style="dim red")
    msg.append("Make sure ping_server.py is running.", style="yellow")
    return Panel(msg, title="[red]No Connection[/red]", border_style="red", padding=(1, 4))
