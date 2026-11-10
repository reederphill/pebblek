# pebblek — convenience wrapper around the `pebble` CLI.
# See CLAUDE.md for the underlying commands and architecture.

# Emulator platform: emery (Pebble Time 2) or gabbro (Pebble Round 2).
PLATFORM ?= emery

.DEFAULT_GOAL := build

.PHONY: build up down dev dev-phone install-phone logs clean help

## build: build for all targetPlatforms (emery, gabbro)
build:
	pebble build

## up: start the SignalK server in Docker
up:
	docker compose up -d

## down: stop the SignalK server in Docker
down:
	docker compose down

## dev: run on the emulator against the local mock SignalK server
# SIGNALK_MOCKUP=true tells wscript to bake the dev SignalK config (server
# address, static clientId, pre-minted token) into the pkjs bundle. It must be
# set at *build* time, and is kept on the install step too so its own "build if
# needed" pass doesn't regenerate buildconfig.js without it.
dev: up
	pebble kill || true
	SIGNALK_MOCKUP=true pebble build
	SIGNALK_MOCKUP=true pebble install --emulator $(PLATFORM)

## dev-phone: run on a real watch against the local mock SignalK server over the LAN.
## Uses the phone app's Developer Connection via CloudPebble by default (needs
## `pebble login`); pass PHONE_IP=10.0.0.x to connect directly instead.
# The phone runs the pkjs side, so the baked server address must be this Mac's LAN
# IP (override with LAN_IP=...), not localhost.
LAN_IP ?= $(shell ipconfig getifaddr en0)
PHONE_TARGET = $(if $(PHONE_IP),--phone $(PHONE_IP),--cloudpebble)
dev-phone: up
	@test -n "$(LAN_IP)" || (echo "no LAN IP found; pass LAN_IP=<this Mac's ip>"; exit 1)
	SIGNALK_MOCKUP=true PEBBLEK_DEV_SERVER=http://$(LAN_IP):3000 pebble build
	SIGNALK_MOCKUP=true PEBBLEK_DEV_SERVER=http://$(LAN_IP):3000 pebble install $(PHONE_TARGET)

## install-phone: install to a real Pebble device (requires a paired phone)
install-phone: clean build
	pebble install --cloudpebble

## logs: stream watch APP_LOG + pkjs console.log
logs:
	pebble logs

## clean: remove build output
clean:
	pebble clean

## help: list available targets
help:
	@grep -E '^## ' $(MAKEFILE_LIST) | sed 's/^## /  /'
