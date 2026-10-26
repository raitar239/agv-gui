# agv-gui

A desktop client for [AdGuard VPN CLI](https://github.com/AdguardTeam/AdGuardVPNCLI) on Linux,
built with C++, Qt 6 and QML. Its main feature is the one the CLI lacks on Linux: **excluding
individual apps from the VPN**, the way the Windows client does.

> Early development. Working: the daemon, the GUI (connect, locations with favorites, website
> exclusions, account, tray, statistics) and per-app exclusions: pick apps in the GUI and they bypass
> the VPN however they are started, or run anything outside it with `agv-run <command>`.
> See [docs/PLAN.md](docs/PLAN.md).

## Architecture

- `agvd` is a root daemon (systemd service). It runs `adguardvpn-cli`, owns routing, nftables and
  cgroups, and exposes the `org.agv.Daemon1` D-Bus API with polkit authorization.
- `agv-gui` is the unprivileged window and tray. It talks only to the daemon.
- `agv-run` starts a program already excluded from the VPN.

## Install

Arch Linux (builds this checkout into a package):

```sh
cd packaging/arch
makepkg -si
sudo systemctl enable --now agvd
```

Then open "AGV" from the application menu. The AdGuard VPN CLI itself comes from
[adguard-vpn.com](https://adguard-vpn.com/) or the AUR (`adguardvpn-cli-bin`).

## Build

Requirements: CMake ≥ 3.22, a C++20 compiler, Qt ≥ 6.4 (Core, DBus, Network, Gui, Widgets,
Quick, QuickControls2, Test). `-DAGV_BUILD_GUI=OFF` builds only the daemon and `agv-run`.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

## Run the GUI

With agvd installed (see below), start `./build/src/gui/agv-gui`. It lives in the tray; closing
the window frees the interface. `--hidden` starts in the tray only. Against a development daemon
on the session bus: `AGV_SESSION_BUS=1 ./build/src/gui/agv-gui`.

## Try the daemon without installing

On the session bus `agvd` skips polkit. Point `--data-dir` at a HOME whose
`~/.local/share/adguardvpn-cli` is logged in:

```sh
./build/src/daemon/agvd --session --data-dir "$HOME" &
busctl --user call org.agv.Daemon1 /org/agv/Daemon org.agv.Daemon1 ListLocations
busctl --user get-property org.agv.Daemon1 /org/agv/Daemon org.agv.Daemon1 State
```

Connecting needs root, so `Connect` only works with the installed system service. To install a
development build (to `/usr/local` and `/etc`) and remove it again:

```sh
sudo scripts/dev-install.sh
sudo scripts/dev-install.sh --uninstall
```

## Kill switch

With the kill switch on (Settings → Protection), agvd keeps traffic inside the VPN while it should
be up, including while it reconnects after a drop. Turning the VPN off in AGV lifts it, and so
does stopping agvd. If you are ever cut off and AGV does not respond:

```sh
sudo nft delete table inet agvd-killswitch
```

## License

[MIT](LICENSE). AGV is an independent project, not affiliated with or endorsed by AdGuard.
