# pebblek — convenience wrapper around the `pebble` CLI.
# See CLAUDE.md for the underlying commands and architecture.

# Emulator platform: emery (Pebble Time 2) or gabbro (Pebble Round 2).
PLATFORM ?= emery

# Hardcode a SignalK server into the pkjs build (skips the Clay config step).
# Used by the dev-* targets; override the address as needed:
#   make dev DEV_SERVER=http://192.168.1.10:3000
DEV_SERVER ?= http://localhost:3000

# Static access-request clientId baked into the dev build, so the SignalK server
# recognises the watch across reinstalls (approve once, then it's auto-granted).
SIGNALK_DEVICE_UUID ?= 00000000-0000-0000-0000-000000000000

# Device token baked into the dev build so the watch skips the approval flow on
# every emulator launch. Minted from the running server's secretKey for the
# clientId above (see dev/signalk-token.sh); empty if the server is down or the
# device isn't approved yet, in which case the watch uses the access-request flow.
SIGNALK_DEVICE_TOKEN = $(shell sh dev/signalk-token.sh $(SIGNALK_DEVICE_UUID) 2>/dev/null)

.DEFAULT_GOAL := build

.PHONY: build up dev install-phone logs clean help

## build: build for all targetPlatforms (emery, gabbro)
build:
	pebble build

## up: start the SignalK server in Docker
up:
	docker compose up -d

## down: stop the SignalK server in Docker
down:
	docker compose down

## dev: run on the emulator with a hardcoded SignalK server ($(DEV_SERVER))
# PEBBLEK_DEV_SERVER must be set at *build* time (it is baked into the pkjs
# bundle via wscript), so build explicitly with it before installing. The var
# is kept on the install step too so its own "build if needed" pass doesn't
# regenerate buildconfig.js without it.
dev: up
	pebble kill || true
	PEBBLEK_DEV_SERVER=$(DEV_SERVER) PEBBLEK_DEVICE_UUID=$(SIGNALK_DEVICE_UUID) PEBBLEK_DEVICE_TOKEN="$(SIGNALK_DEVICE_TOKEN)" pebble build
	PEBBLEK_DEV_SERVER=$(DEV_SERVER) PEBBLEK_DEVICE_UUID=$(SIGNALK_DEVICE_UUID) PEBBLEK_DEVICE_TOKEN="$(SIGNALK_DEVICE_TOKEN)" pebble install --emulator $(PLATFORM)

## install-phone: install to a real Pebble device (requires a paired phone)
install-phone:
	pebble install --cloudpebble

## logs: stream watch APP_LOG + pkjs console.log
logs:
	pebble logs

## clean: remove build output
clean: down
	pebble clean

## help: list available targets
help:
	@grep -E '^## ' $(MAKEFILE_LIST) | sed 's/^## /  /'
