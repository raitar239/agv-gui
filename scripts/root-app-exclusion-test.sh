#!/bin/bash
# End-to-end check of automatic app exclusions (exec watcher and running-process scan) with an
# installed agvd whose VPN is connected:
#
#   sudo scripts/root-app-exclusion-test.sh
#
# Adds a temporary rule matching the marker "agv-probe" in a command line, then checks that
#   - a process started before the rule is moved when the rule is added,
#   - a process started after the rule is caught by the exec watcher,
#   - a process without the marker stays on the VPN.
# The rule is removed again on exit. Only the probe processes ever leave the VPN.

set -u

OBJ=(org.agv.Daemon1 /org/agv/Daemon org.agv.Daemon1)
TRACE=https://1.1.1.1/cdn-cgi/trace
USER_NAME=${SUDO_USER:-}

if [ "$(id -u)" -ne 0 ] || [ -z "$USER_NAME" ]; then
    echo "Run with sudo from your user account: sudo $0" >&2
    exit 1
fi

prop() { busctl get-property "${OBJ[@]}" "$1" | cut -d' ' -f2- | tr -d '"'; }
fail() { echo "FAILED: $*" >&2; exit 1; }
# Runs the IP check as the user after a delay, with a marker in its command line.
probe() { # probe <marker> <delay> <output file>
    sudo -u "$USER_NAME" -- sh -c "sleep $2; curl -sS -m 10 $TRACE | sed -n 's/^ip=//p' > $3" "$1"
}

work=$(mktemp -d)
# The probes run as the user and write their results here.
chown "$USER_NAME" "$work"
rule_id=""
cleanup() {
    [ -n "$rule_id" ] && busctl call "${OBJ[@]}" RemoveAppExclusion s "$rule_id" >/dev/null 2>&1
    rm -rf "$work"
}
trap cleanup EXIT

echo "== agvd"
[ "$(prop State)" = "connected" ] || fail "agvd's VPN is not connected (State=$(prop State))"
[ "$(prop AppExclusionsSupported)" = "true" ] || fail "app exclusions are not supported; see: journalctl -u agvd"
journalctl -u agvd -b --no-pager | grep -q "exec watcher unavailable" \
    && fail "the exec watcher did not start; see: journalctl -u agvd"
echo "connected to $(prop Location)"

echo "== baseline"
probe agv-normal 0 "$work/vpn"
vpn_ip=$(cat "$work/vpn")
[ -n "$vpn_ip" ] || fail "no internet for a normal process"
echo "normal process: $vpn_ip"

echo "== a process already running when the rule is added"
probe agv-probe-running 4 "$work/running" &
running=$!
sleep 1

rule_id=$(busctl call "${OBJ[@]}" AddAppExclusion "a{sv}" 2 name s "agv probe" cmdline_match s agv-probe \
          | sed -n 's/^s "\(.*\)"$/\1/p')
[ -n "$rule_id" ] || fail "AddAppExclusion did not return an id"
echo "rule $rule_id added"

echo "== a process started after the rule"
probe agv-probe-new 1 "$work/new"
probe agv-control 1 "$work/control"
wait "$running"

new_ip=$(cat "$work/new")
running_ip=$(cat "$work/running")
control_ip=$(cat "$work/control")
echo "started after the rule:  ${new_ip:-<none>}"
echo "running before the rule: ${running_ip:-<none>}"
echo "without the marker:      ${control_ip:-<none>}"

[ -n "$new_ip" ] && [ "$new_ip" != "$vpn_ip" ] || fail "the exec watcher did not exclude the new process"
[ -n "$running_ip" ] && [ "$running_ip" != "$vpn_ip" ] || fail "the running process was not moved"
[ "$control_ip" = "$vpn_ip" ] || fail "a process without the marker left the VPN"

echo "== counters"
busctl call "${OBJ[@]}" GetAppTraffic

echo "== OK: matching processes bypass the VPN automatically, others stay on it"
