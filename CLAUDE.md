# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

pebblek is a SignalK app for Pebble — monitor a boat and control an autopilot from the wrist. It is a **Pebble Alloy / Moddable** project (`projectType: moddable` in `package.json`), meaning the on-watch app logic is written in JavaScript running on the Moddable XS engine, bootstrapped by a thin C shim. This is different from a classic Pebble C app.

## Commands

```sh
pebble build                          # build for all targetPlatforms (emery, gabbro)
pebble build --debug                  # build with PBL_DEBUG → enables the xsbug JS debugger
pebble install --emulator emery       # build (if needed) + run on the emery emulator
pebble install --phone <ip>           # install to a paired phone over the network
pebble logs                           # stream console.log output from app + pkjs
```

There is no separate lint/test tooling configured in this repo.

## Architecture: three runtimes

A single app spans three execution contexts. Understanding which file runs where is essential before editing:

1. **`src/c/mdbl.c`** — Native C, runs first on the watch. It is *only* a bootstrap: creates a window and calls `moddable_createMachine()` to start the Moddable XS JavaScript VM. In a debug build it passes `kModdableCreationFlagDebug` to attach xsbug. App logic does **not** live here.

2. **`src/embeddedjs/main.js`** — The actual watch app, JavaScript on the Moddable XS engine. Uses Moddable APIs (e.g. `commodetto/Poco` for graphics, the `screen` and `watch` globals) — *not* the classic PebbleKit C/JS APIs. `src/embeddedjs/manifest.json` is the Moddable manifest that declares which modules get compiled into the VM; new embedded JS modules must be wired up here.

3. **`src/pkjs/index.js`** — PebbleKit JS, runs on the **phone**, not the watch. Uses the classic `Pebble.addEventListener(...)` API. This is the side that has network/internet access (for SignalK communication). Watch ↔ phone messaging goes through `messageKeys` declared in `package.json`.

`enableMultiJS` is on, so `src/common/**/*.js` is shared into the pkjs bundle (see `wscript`).

## Build wiring (`wscript`)

`wscript` is a waf build script and rarely needs editing, but note how sources are routed — changing directory layout means changing these globs:
- C app sources: `src/c/**/*.c`
- pkjs bundle: `src/pkjs/**/*.js`, `src/pkjs/**/*.json`, `src/common/**/*.js`, entry `src/pkjs/index.js`
- A `worker_src/` directory, if present, triggers a background worker build automatically.

The embeddedjs sources are pulled in by the Moddable toolchain via `manifest.json`, not by the globs in `wscript`.

## Target platforms

Only **emery** (Pebble Time 2) and **gabbro** (Pebble Round 2) — the modern Alloy hardware — are in `targetPlatforms`. Don't assume older platforms (aplite/basalt/chalk/diorite) work.

## Reference

Full SDK docs: <https://developer.repebble.com>
