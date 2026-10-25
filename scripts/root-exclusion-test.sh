#!/bin/bash
# End-to-end check of app exclusions with an installed agvd whose VPN is connected:
#
#   sudo scripts/root-exclusion-test.sh
#
# Compares the public IP seen by a normal process (should be the VPN's) with one started through
# agv-run (should be the ISP's), then checks the per-app traffic counters. Only processes started
# through agv-run are affected, so the rest of the system stays on the VPN throughout.

set -u

OBJ=(org.agv.Daemon1 /org/agv/Daemon org.agv.Daemon1)
# Cloudflare's trace endpoint reports the caller's address as "ip=…"; addressed by IP so DNS
# cannot be the difference.
TRACE=https://1.1.1.1/cdn-cgi/trace
USER_NAME=${SUDO_USER:-}

if [ "$(id -u)" -ne 0 ] || [ -z "$USER_NAME" ]; then
    echo "Run with sudo from your user account: sudo $0" >&2
    exit 1
fi

prop() { busctl get-property "${OBJ[@]}" "$1" | cut -d' ' -f2- | tr -d '"'; }
as_user() { sudo -u "$USER_NAME" -- "$@"; }
public_ip() { "$@" curl -sS -m 10 "$TRACE" | sed -n 's/^ip=//p'; }
fail() { echo "FAILED: $*" >&2; exit 1; }

echo "== agvd"
[ "$(prop State)" = "connected" ] || fail "agvd's VPN is not connected (State=$(prop State))"
[ "$(prop AppExclusionsSupported)" = "true" ] || fail "app exclusions are not supported; see: journalctl -u agvd"
echo "connected to $(prop Location), app exclusions supported"

echo "== kernel state"
nft list table inet agvd >/dev/null || fail "nftables table inet agvd is missing"
ip -4 rule | grep -q "fwmark 0x61677664 lookup main" || fail "ip rule for the mark is missing"
echo "nft table and ip rule present; src_valid_mark=$(cat /proc/sys/net/ipv4/conf/all/src_valid_mark)"

echo "== public addresses"
vpn_ip=$(public_ip as_user)
[ -n "$vpn_ip" ] || fail "no internet for a normal process"
echo "normal process:   $vpn_ip"

direct_ip=$(public_ip as_user agv-run)
[ -n "$direct_ip" ] || fail "no internet through agv-run"
echo "through agv-run:  $direct_ip"

[ "$vpn_ip" != "$direct_ip" ] || fail "both go the same way; the exclusion has no effect"

after_ip=$(public_ip as_user)
[ "$after_ip" = "$vpn_ip" ] || fail "a normal process now uses $after_ip instead of the VPN"
echo "normal process again: $after_ip (still the VPN)"

echo "== counters"
busctl call "${OBJ[@]}" GetAppTraffic

echo "== OK: agv-run bypasses the VPN, everything else stays on it"
