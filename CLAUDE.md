# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Calcyon is a Pebble watchface forked from [Halcyon](https://github.com/freakified/halcyon). It shows a 24-hour solar ring around the watch edge (sun moves along the perimeter), and extends it with calendar event overlays on that ring. The goal: your day as both a solar day and a personal schedule.

## Build

Requires the [Pebble SDK](https://developer.rebble.io/developer.pebble.com/sdk/index.html) (`pebble` CLI).

On Fedora, use `./pebble.sh` instead of `pebble` directly — it patches missing `.so` symlinks (`libbz2.so.1.0`, `libsndio.so.7`) that the SDK expects but Fedora doesn't provide at those names.

```bash
./pebble.sh build               # compile all platforms
./pebble.sh install --emulator aplite   # run on rect emulator
./pebble.sh install --emulator chalk    # run on round emulator
./pebble.sh install --emulator emery    # run on large rect emulator
./pebble.sh logs --emulator aplite      # stream logs
```

The config page (separate Vite/React app, deployed independently):

```bash
cd config-page
npm install
npm run dev       # local dev server
npm run build     # production build
```

To test the config page against the emulator:
1. Set `USE_LOCAL_CONFIG = true` in `src/pkjs/index.js` (points to `http://localhost:3000/index.html`)
2. Run the config page dev server: `cd config-page && npm run dev`
3. Build and install on the emulator: `./pebble.sh build && ./pebble.sh install --emulator emery`
4. Open the config UI from the emulator: `./pebble.sh emu-app-config --emulator emery`

Remember to set `USE_LOCAL_CONFIG = false` before committing.

## Architecture

The app has two distinct runtime environments that communicate via `AppMessage`:

### Watch (C) — `src/c/`

| File | Role |
|---|---|
| `main.c` | Entry point; owns the window, layers, tick handler, and the 30-minute heartbeat that requests updates from the phone |
| `drawUtils.h` + `drawUtils_rect.c` / `drawUtils_round.c` | Platform-split rendering. `draw_ring_layer()` draws the 24-hour perimeter ring, sun position, and sunrise/sunset markers. `draw_center_layer()` draws the background fill and hour pips. The rect and round implementations share the same function signatures but use completely different geometry. |
| `solarUtils.c/.h` | Manages `currentSolarInfo` (sunrise/sunset minutes). Loaded from persistent storage; updated when the phone sends new values. |
| `settings.c/.h` | `globalSettings` struct; persisted across reboots via two persist keys (`SETTINGS_PERSIST_KEY`, `SETTINGS_EXTRA_PERSIST_KEY`). Color themes, widget format strings, alt-city config. |
| `messaging.c/.h` | `AppMessage` inbox/outbox handlers. The watch sends `REQUEST_UPDATE` every 30 min; the phone responds with location, solar, weather, and settings data. Inbox size is 768 bytes. |
| `widgets.c/.h` | Expands widget format strings (steps, heart rate, date, alt-city time, etc.) at render time. |

**Layer stack** (back → front): `shiftingLayer` → `centerLayer` (background + pips) → `infoLayer` (text) → `ringLayer` (the perimeter ring + sun, drawn on top so it overlaps the edge).

**Time origin on the ring**: The 24-hour clock is shifted so midnight appears at the bottom. Rect uses a 15-hour shift; round uses a 12-hour shift. All time-to-position math applies this same shift before converting to a perimeter position or angle.

### Phone (JS) — `src/pkjs/`

`index.js` is the PebbleKit JS entry point. It:
1. Gets GPS location → computes sunrise/sunset via `suncalc.js`
2. Fetches weather via `weather.js`
3. Applies JS-side token substitution to widget format strings (`{temp}`, `{cond}`, `{sunrise}`, etc.) — C-side tokens (`{date}`, `{steps}`) pass through untouched
4. Sends everything to the watch via `Pebble.sendAppMessage()`

The watch drives the update cadence — it sends `REQUEST_UPDATE` every 30 minutes and the JS responds. A JS-side backoff (1m → 5m → 15m) handles transient failures.

### Config page — `config-page/src/`

React/TypeScript SPA built with Vite. Opened by the Pebble app via `Pebble.openURL()`. Returns settings as a URL-encoded JSON payload to the `webviewclosed` handler in `index.js`. Deployed to `halcyon.freakified.net` (the upstream URL; update `configDataUri` in `index.js` for Calcyon's own deployment).

## Key data flows

**Calendar events on the ring** (the new Calcyon feature):
- Phone JS fetches calendar events and encodes them as minute-offsets (start/end in minutes since midnight)
- Sent to the watch via new `AppMessage` keys defined in `package.json` → `messageKeys`
- `draw_ring_layer()` in both `drawUtils_rect.c` and `drawUtils_round.c` renders event arcs at the corresponding positions, using the same time-shift and position math as the solar ring

**Adding a new `AppMessage` key**: declare it in `package.json` → `pebble.messageKeys`, then use `MESSAGE_KEY_<NAME>` in C and the string key name in JS.

**Settings persistence**: `StoredSettings` and `StoredSettingsExtra` are raw-memcpy'd to Pebble persistent storage. Adding fields requires a version bump (`CURRENT_SETTINGS_VERSION`) and a migration in `Settings_loadFromStorage()`.
