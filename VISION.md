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

## Event detail (drill-down) design (2026-06-28)

When the wearer touches an event arc on the ring, the face drills down to show that
event's details. Four directions were explored (see the event-detail artifact); the
chosen approach is **Detail in Panel**:

- The centre panel that normally shows the clock swaps to the event's **title, time,
  and location**, drawn in the existing flat face style (square white panel, hairline
  border, chunky bold font). No new visual language — it repaints content the face
  already draws.
- The tapped event is marked on the ring with the **Spotlight** highlight: the solar
  band and all other events **dim to muted tones while only the selected arc keeps full
  colour**. This gives the strongest figure/ground separation at emery's low resolution
  and is cheap to draw (an opacity change on everything-but-the-selected). Solar context
  is suppressed only *while* drilled in, which is acceptable since the wearer is focused
  on a single event.

Alternatives considered but not chosen: a dark outline (too subtle at 200 px), an inner
bulge / pointer tab (more prominent but adds geometry to a busy edge), and a colour-tied
panel (panel border adopts the calendar colour — a good optional accent to layer on top
later). The other drill-down directions (Anchored Callout, Countdown, Agenda Peek) remain
viable as secondary gestures but are out of scope for now.

## Implementation plan

**Phase 1 — Implement Design A (Painted Ring).** The rendering code is a near-direct
reuse of the existing day-arc fill loop; the bulk of the work is the data pipeline
(calendar fetch on the phone side, AppMessage to the watch, storage in C).

**Phase 2 — Implement Design C (Split Ring)** once A is working and on the watch.
C is the preferred visual direction long-term: it preserves solar context even on busy
days. Comparing A and C side-by-side on the live watch will inform the final call.
