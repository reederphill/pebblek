# pebblek — convenience wrapper around the `pebble` CLI.
# See CLAUDE.md for the underlying commands and architecture.

# Emulator platform: emery (Pebble Time 2) or gabbro (Pebble Round 2).
PLATFORM ?= emery

# Hardcode a SignalK server into the pkjs build (skips the Clay config step).
# Used by the dev-* targets; override the address as needed:
#   make dev DEV_SERVER=http://192.168.1.10:3000
DEV_SERVER ?= http://localhost:3000

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
	PEBBLEK_DEV_SERVER=$(DEV_SERVER) pebble build
	PEBBLEK_DEV_SERVER=$(DEV_SERVER) pebble install --emulator $(PLATFORM)

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
