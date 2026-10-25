#!/bin/sh

ip route add 0.0.0.0/1 dev $1
ip route add 128.0.0.0/1 dev $1
ip -6 route add 2000::/3 dev $1 || true
