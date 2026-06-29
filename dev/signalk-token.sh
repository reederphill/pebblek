#!/bin/sh
# Mint a SignalK device token for the given clientId, signed with the running
# dev server's secretKey (read straight from its security.json in the Docker
# volume). `make dev` bakes this into the build as PEBBLEK_DEVICE_TOKEN so the
# watch authenticates without the manual access-request approval flow on every
# emulator launch.
#
# This mirrors how signalk-server mints device tokens on approval:
#   jwt.sign({ device: <clientId> }, secretKey)   (no expiry; expiration=NEVER)
#
# The clientId must already be an approved device on the server for the token to
# grant readwrite (approval persists in the Docker volume, so it's a one-time
# step). If the server is down or the device is unknown this prints nothing and
# the build falls back to the normal access-request flow.
#
# Usage: dev/signalk-token.sh <clientId>
set -e

CLIENT_ID="$1"
if [ -z "$CLIENT_ID" ]; then
  echo "usage: $0 <clientId>" >&2
  exit 1
fi

docker compose exec -T -e CLIENT_ID="$CLIENT_ID" signalk node -e '
  const jwt = require("/home/node/signalk/node_modules/jsonwebtoken");
  const fs = require("fs");
  const cfg = JSON.parse(fs.readFileSync("/home/node/.signalk/security.json"));
  process.stdout.write(jwt.sign({ device: process.env.CLIENT_ID }, cfg.secretKey));
'
