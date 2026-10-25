# agv-gui

A desktop client for [AdGuard VPN CLI](https://github.com/AdguardTeam/AdGuardVPNCLI) on Linux,
built with C++, Qt 6 and QML. Its main feature is the one the CLI lacks on Linux: **excluding
individual apps from the VPN**, the way the Windows client does.

> Early development. Done so far: the daemon core and its D-Bus API. The GUI and app
> exclusions are not built yet. See [docs/PLAN.md](docs/PLAN.md).

## Architecture

- `agvd` is a root daemon (systemd service). It runs `adguardvpn-cli`, owns routing, nftables and
  cgroups, and exposes the `org.agv.Daemon1` D-Bus API with polkit authorization.
- `agv-gui` is the unprivileged window and tray. It talks only to the daemon.
- `agv-run` starts a program already excluded from the VPN.

## Build

Requirements: CMake ≥ 3.22, a C++20 compiler, Qt ≥ 6.4 (Core, DBus, Test).

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

## Try the daemon without installing

On the session bus `agvd` skips polkit. Point `--data-dir` at a HOME whose
`~/.local/share/adguardvpn-cli` is logged in:

```sh
./build/src/daemon/agvd --session --data-dir "$HOME" &
busctl --user call org.agv.Daemon1 /org/agv/Daemon org.agv.Daemon1 ListLocations
busctl --user get-property org.agv.Daemon1 /org/agv/Daemon org.agv.Daemon1 State
```

Connecting needs root, so `Connect` only works with the installed system service.
