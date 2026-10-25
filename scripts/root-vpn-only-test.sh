#!/bin/bash
# End-to-end check of the "vpn-only" app mode with an installed agvd whose VPN is connected:
#
#   sudo scripts/root-vpn-only-test.sh
#
# Switches to vpn-only for a few seconds and checks that a normal process now goes straight out
# while one started with agv-run (the builtin group) goes through the VPN. The previous mode is
# restored on exit, whatever happens. While the test runs, everything except the probe bypasses
# the VPN.

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
public_ip() { sudo -u "$USER_NAME" -- "$@" curl -sS -m 10 "$TRACE" | sed -n 's/^ip=//p'; }

[ "$(prop State)" = "connected" ] || fail "agvd's VPN is not connected (State=$(prop State))"
[ "$(prop AppExclusionsSupported)" = "true" ] || fail "app exclusions are not supported"

original_mode=$(prop AppExclusionMode)
restore() {
    busctl call "${OBJ[@]}" SetAppExclusionMode s "$original_mode" >/dev/null 2>&1
    echo "mode restored: $(prop AppExclusionMode)"
}
trap restore EXIT

echo "== mode $original_mode"
vpn_ip=$(public_ip)
direct_ip=$(public_ip agv-run)
echo "normal process:  $vpn_ip"
echo "through agv-run: $direct_ip"
[ -n "$vpn_ip" ] && [ -n "$direct_ip" ] && [ "$vpn_ip" != "$direct_ip" ] || fail "unexpected baseline"

echo "== mode vpn-only"
busctl call "${OBJ[@]}" SetAppExclusionMode s vpn-only || fail "SetAppExclusionMode failed"
ip -4 rule | grep -q "not from all fwmark 0x61677664 lookup main" || fail "the inverted ip rule is missing"
normal=$(public_ip)
listed=$(public_ip agv-run)
echo "normal process:  ${normal:-<none>}  (expected $direct_ip)"
echo "through agv-run: ${listed:-<none>}  (expected $vpn_ip)"
[ "$normal" = "$direct_ip" ] || fail "a normal process did not go straight out"
[ "$listed" = "$vpn_ip" ] || fail "agv-run did not go through the VPN"

echo "== OK: vpn-only sends only the listed apps through the VPN"
