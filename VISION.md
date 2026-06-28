# Calcyon — Vision

Calcyon is a fork of the [Halcyon](https://github.com/freakified/halcyon) watchface.
The name is a portmanteau of **cal**endar + hal**cyon**.

## Core idea

Halcyon visualises the solar day as a 24-hour ring around the watch face — the sun
moves clockwise from top-left (9 AM) through the daytime arc to top-right (3 PM),
continuing around to bottom and back. Calcyon layers your personal calendar on top
of that same ring, so the face shows not just *where the sun is* but *what your day
looks like*: meetings, workouts, and appointments anchored to the same arc the sun
travels.

## Primary platform

**Pebble Time 2 (emery)** — colour, touch screen, rectangular 200 × 228 px display.
Designs should be considered first for this platform; other platforms (chalk, basalt,
diorite) may follow.

## Calendar feature goals

- Show events from **multiple calendars** simultaneously, each calendar assigned a
  distinct colour.
- Respect the **complications model**: the ring surface is the glanceable layer.
  Tapping an event (touch screen) drills down to show event details (title, time,
  location) in the centre of the face.
- **Preserve the solar context** as much as possible — sunrise, sunset, and the
  day/night divide should remain legible even on a busy calendar day.

## Design constraints

- The 24-hour ring is 20 px thick on emery. Any calendar overlay must work within or
  immediately adjacent to this band.
- Event data arrives from the phone via `AppMessage` (PebbleKit JS fetches calendar
  data and pushes up to N events per day).
- The face must degrade gracefully when no calendar data is available (falls back to
  stock Halcyon behaviour).

## Design exploration (2026-06-28)

Four directions were evaluated for how events are rendered on the ring.
See the design exploration artifact for visual mockups.

| Direction | Summary |
|-----------|---------|
| **A — Painted Ring** | Events replace the ring colour for their duration |
| **B — Inward Notches** | Coloured strips extend inward from the ring's inner edge |
| **C — Split Ring** | Outer half of ring stays solar; inner half shows calendar colour |
| **D — Orbit Beads** | Small coloured dots mark event start times on the sun's path |

## Implementation plan

**Phase 1 — Implement Design A (Painted Ring).** The rendering code is a near-direct
reuse of the existing day-arc fill loop; the bulk of the work is the data pipeline
(calendar fetch on the phone side, AppMessage to the watch, storage in C).

**Phase 2 — Implement Design C (Split Ring)** once A is working and on the watch.
C is the preferred visual direction long-term: it preserves solar context even on busy
days. Comparing A and C side-by-side on the live watch will inform the final call.
