# pingmon

A desktop window for [ping-monitor](https://github.com/chrisfleming/ping-monitor). It starts the
server, or attaches to one that is already running, and shows each host's latency, loss and a live
bar graph. It follows the Omarchy theme and language.

## Shortcuts

| Key          | Action                    |
| ------------ | ------------------------- |
| `A`          | Add a host                |
| `Delete`/`X` | Remove the selected host  |
| `↑` `↓`/`K` `J` | Select a host          |
| `Space`      | Start or stop the server  |
| `?`          | Show shortcuts            |
| `Q`          | Quit (stops a server it started) |

## How it works

- The server is a bundled copy of `ping_server.py` and `monitor_common.py` in `server/`; refresh it
  with `bin/sync-server [path-to-ping-monitor]`. Set `PINGMON_SERVER_DIR` to run a different copy.
- It reads `~/.config/ping-monitor/config.json` for the port and the green/yellow/red thresholds.
- Hosts it started the server with are kept in `~/.config/pingmon/hosts.json`
  (default `1.1.1.1` and `8.8.8.8`). On a server it did not start, hosts are added and removed on
  that server and nothing is saved locally.

## Build

```sh
bin/build     # build/pingmon
bin/test      # unit tests, including a run against the real server
bin/install   # makepkg -fsi: installs the binary, icon, and launcher entry
```
