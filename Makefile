# pebblek — convenience wrapper around the `pebble` CLI.
# See CLAUDE.md for the underlying commands and architecture.

# Emulator platform: emery (Pebble Time 2) or gabbro (Pebble Round 2).
PLATFORM ?= emery

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

## dev: run on the emulator against the local mock SignalK server
# SIGNALK_MOCKUP=true tells wscript to bake the dev SignalK config (server
# address, static clientId, pre-minted token) into the pkjs bundle. It must be
# set at *build* time, and is kept on the install step too so its own "build if
# needed" pass doesn't regenerate buildconfig.js without it.
dev: up
	pebble kill || true
	SIGNALK_MOCKUP=true pebble build
	SIGNALK_MOCKUP=true pebble install --emulator $(PLATFORM)

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
