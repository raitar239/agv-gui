#!/bin/bash
# End-to-end check of an installed agvd on the system bus, run as root:
#
#   sudo scripts/root-smoke-test.sh
#
# It takes over the running VPN: imports root's CLI login into agvd, stops the VPN that was
# started outside agvd, then connects, disconnects and connects again through agvd. Internet is
# down for a few seconds at each switch.
#
# Whatever fails, it rolls back by starting the original VPN the usual way, so the machine always
# ends up connected. On success agvd's VPN is left running (same location as before).

set -u

OBJ=(org.agv.Daemon1 /org/agv/Daemon org.agv.Daemon1)
CHECK_URL=https://api.anthropic.com/

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0" >&2
    exit 1
fi
if ! systemctl is-active --quiet agvd.service; then
    echo "agvd is not running; install it first: sudo scripts/dev-install.sh" >&2
    exit 1
fi

strip_ansi() { sed 's#\x1b\[[0-9;?]*[A-Za-z]##g'; }
prop() { busctl get-property "${OBJ[@]}" "$1" | cut -d' ' -f2- | tr -d '"'; }
call() { busctl --timeout=120 call "${OBJ[@]}" "$@"; }
step() { printf '\n== %s\n' "$*"; }
online() { curl -sS -m 10 -o /dev/null -w '%{http_code}' "$CHECK_URL" 2>/dev/null; }

# The CLI reports "connected" slightly before its DNS proxy is up, so give it a moment.
wait_online() {
    local code errfile
    errfile=$(mktemp)
    for i in $(seq 1 15); do
        code=$(curl -sS -m 5 -o /dev/null -w '%{http_code}' "$CHECK_URL" 2>"$errfile")
        if [ "$code" != "000" ]; then
            echo "Internet check: HTTP $code after ${i}s"
            rm -f "$errfile"
            return 0
        fi
        sleep 1
    done
    echo "Internet check failed: $(cat "$errfile")" >&2
    rm -f "$errfile"
    return 1
}

wait_state() {
    local want=$1 state
    for _ in $(seq 1 30); do
        state=$(prop State)
        [ "$state" = "$want" ] && return 0
        sleep 1
    done
    echo "State is '$state', expected '$want'" >&2
    return 1
}

# The VPN as it runs now, started with this shell's HOME (sudo's environment).
original_status=$(adguardvpn-cli status 2>&1 | strip_ansi)
original_location=$(sed -n 's/^Connected to \(.*\) in .* mode.*/\1/p' <<<"$original_status")

rollback() {
    step "FAILED: $1 - rolling back"
    call Disconnect >/dev/null 2>&1 || true
    if adguardvpn-cli status 2>&1 | strip_ansi | grep -q '^Connected to '; then
        echo "The original VPN is still running; nothing to restore"
    elif [ -n "$original_location" ]; then
        adguardvpn-cli connect -y -l "$original_location" || adguardvpn-cli connect -y
    else
        adguardvpn-cli connect -y
    fi
    adguardvpn-cli status | strip_ansi
    echo "Internet check: HTTP $(online)"
    exit 1
}

step "Before"
echo "Original VPN: ${original_status%%$'\n'*}"
echo "agvd: State=$(prop State) LoggedIn=$(prop LoggedIn)"
busctl get-property "${OBJ[@]}" ExternalVpns

if [ "$(prop LoggedIn)" != "true" ]; then
    step "ImportLogin (from root's adguardvpn-cli)"
    call ImportLogin || rollback "ImportLogin"
    echo "LoggedIn=$(prop LoggedIn)"
fi

step "StopExternalVpns"
call StopExternalVpns || rollback "StopExternalVpns"

step "Connect ${original_location:-<last used>}"
call Connect s "$original_location" || rollback "Connect"
wait_state connected || rollback "first connect"
echo "Location=$(prop Location) TunDevice=$(prop TunDevice)"
wait_online || rollback "no internet after connect"

step "Disconnect"
call Disconnect || rollback "Disconnect"
wait_state disconnected || rollback "disconnect"

step "Connect again"
call Connect s "$original_location" || rollback "second Connect"
wait_state connected || rollback "second connect"
wait_online || rollback "no internet after reconnect"

step "OK"
echo "agvd now owns the VPN: State=$(prop State) Location=$(prop Location)"
busctl get-property "${OBJ[@]}" ExternalVpns
