#ifdef PBL_RECT

#include "drawUtils.h"
#include "settings.h"
#include "solarUtils.h"
#include "utils.h"
#include "calendarUtils.h"

static ColorTheme currentTheme;

GPoint get_rect_position(float progress, GRect bounds) {
  if (progress < 0.25f) {
    float t = progress / 0.25f;
    return GPoint(bounds.origin.x + (int)(t * bounds.size.w), bounds.origin.y);
  } else if (progress < 0.5f) {
    float t = (progress - 0.25f) / 0.25f;
    return GPoint(bounds.origin.x + bounds.size.w,
                  bounds.origin.y + (int)(t * bounds.size.h));
  } else if (progress < 0.75f) {
    float t = (progress - 0.5f) / 0.25f;
    return GPoint(bounds.origin.x + bounds.size.w - (int)(t * bounds.size.w),
                  bounds.origin.y + bounds.size.h);
  } else {
    float t = (progress - 0.75f) / 0.25f;
    return GPoint(bounds.origin.x,
                  bounds.origin.y + bounds.size.h - (int)(t * bounds.size.h));
  }
}

GPoint getPipPosition(int id, int numPips, GRect bounds) {
  float progress = (float)id / (float)numPips;
  return get_rect_position(progress, bounds);
}

GRect snap_to_edges(GRect rect, GRect bounds, int thickness) {
  // vertical snapping
  if (rect.origin.y + rect.size.h > bounds.size.h - thickness) {
    rect.size.h = bounds.size.h - rect.origin.y;
  }
  if (rect.origin.y < thickness) {
    rect.size.h += rect.origin.y;
    rect.origin.y = 0;
  }

  // horizontal snapping
  if (rect.origin.x + rect.size.w > bounds.size.w - thickness) {
    rect.size.w = bounds.size.w - rect.origin.x;
  }
  if (rect.origin.x < thickness) {
    rect.size.w += rect.origin.x;
    rect.origin.x = 0;
  }

  return rect;
}

void draw_center_layer(Layer *layer, GContext *ctx) {
  currentTheme = getCurrentColorTheme();
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, currentTheme.bgColor);

  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int innerPadding = 0;

  bounds.origin.x += innerPadding;
  bounds.origin.y += innerPadding;
  bounds.size.w -= innerPadding * innerPadding + 1;
  bounds.size.h -= innerPadding * innerPadding + 1;

  int numPips = 24;
  int pip_length = 2;
  int long_pip_length = 3;

  for (int i = 0; i < numPips; i++) {
    bool is_main_pip = ((i - (numPips / 8)) % (numPips / 4) == 0);

    // Check pip visibility setting
    bool should_draw_pip = false;
    switch (globalSettings.pipVisibility) {
    case PIP_SHOW_ALL:
      should_draw_pip = true;
      break;
    case PIP_SHOW_MAJOR:
      should_draw_pip = is_main_pip;
      break;
    case PIP_HIDDEN:
      should_draw_pip = false;
      break;
    }

    if (!should_draw_pip)
      continue;

    int length = is_main_pip ? long_pip_length : pip_length;

    graphics_context_set_stroke_color(
        ctx, is_main_pip ? currentTheme.pipColorPrimary
                         : currentTheme.pipColorSecondary);
    graphics_context_set_stroke_width(ctx, 3);

    GPoint start = getPipPosition(i, numPips, bounds);

    GRect contracted_bounds = bounds;
    contracted_bounds.origin.x += length;
    contracted_bounds.origin.y += length;
    contracted_bounds.size.w -= 2 * length;
    contracted_bounds.size.h -= 2 * length;

    GPoint end = getPipPosition(i, numPips, contracted_bounds);

    graphics_draw_line(ctx, start, end);
  }
}

void snap_to_bounds(GRect *rect, GRect bounds, int thickness) {
  // snap origin
  if (rect->origin.x - thickness <= bounds.origin.x) {
    rect->size.w += (rect->origin.x - bounds.origin.x);
    rect->origin.x = bounds.origin.x;
  }
  if (rect->origin.y - thickness <= bounds.origin.y) {
    rect->size.h += (rect->origin.y - bounds.origin.y);
    rect->origin.y = bounds.origin.y;
  }

  // snap size
  if (rect->origin.x + rect->size.w + thickness >= bounds.origin.x + bounds.size.w) {
    rect->size.w += (bounds.origin.x + bounds.size.w) - (rect->origin.x + rect->size.w);
  }
  if (rect->origin.y + rect->size.h + thickness >= bounds.origin.y + bounds.size.h) {
    rect->size.h += (bounds.origin.y + bounds.size.h) - (rect->origin.y + rect->size.h);
  }
}

void draw_ring_layer(Layer *layer, GContext *ctx) {
  currentTheme = getCurrentColorTheme();
  GRect bounds = layer_get_bounds(layer);
  int thickness = RING_THICKNESS;
  int strokeWidth = RING_STROKE_WIDTH;
  GRect innerBounds =
      GRect(bounds.origin.x + thickness / 2, bounds.origin.y + thickness / 2,
            bounds.size.w - thickness, bounds.size.h - thickness);

  GRect sunBounds = GRect(bounds.origin.x + SUN_INSET, bounds.origin.y + SUN_INSET,
                          bounds.size.w - SUN_INSET * 2,
                          bounds.size.h - SUN_INSET * 2);

  int numPositions = 96;
  int width = bounds.size.w;
  int height = bounds.size.h;

  // Draw outer rectangular ring
  graphics_context_set_fill_color(ctx, currentTheme.ringNightColor);

  // Top bar
  graphics_fill_rect(ctx, GRect(0, 0, width, thickness), 0, GCornerNone);
  // Bottom bar
  graphics_fill_rect(ctx, GRect(0, height - thickness, width, thickness), 0,
                     GCornerNone);
  // Left bar
  graphics_fill_rect(ctx, GRect(0, 0, thickness, height), 0, GCornerNone);
  // Right bar
  graphics_fill_rect(ctx, GRect(width - thickness, 0, thickness, height), 0,
                     GCornerNone);

  // Get time and sun position
  struct tm *timeInfo = getCurrentTime();
  int hour = timeInfo->tm_hour;
  int minute = timeInfo->tm_min;

  // Shift time by 15 hours so that it starts at the bottom
  int shiftedHour = (hour + 15) % 24;
  float progress = ((shiftedHour % 24) + (minute / 60.0f)) / 24.0f;

  GPoint sunPos = get_rect_position(progress, sunBounds);

  // Apply the same 15-hour shift to the sunrise and sunset times
  int shiftedSunriseMinute =
      (currentSolarInfo.sunriseMinute + 15 * 60) % (24 * 60);
  int shiftedSunsetMinute =
      (currentSolarInfo.sunsetMinute + 15 * 60) % (24 * 60);

  // Calculate sunrise and sunset positions on the ring using getPipPosition
  int dayStartPos =
      (int)(((shiftedSunriseMinute / 1440.0f) * numPositions) + 0.5);
  int dayEndPos = (int)(((shiftedSunsetMinute / 1440.0f) * numPositions) + 0.5);

  GPoint dayStart = getPipPosition(dayStartPos, numPositions, innerBounds);
  GPoint dayEnd = getPipPosition(dayEndPos, numPositions, innerBounds);

  // Fill the day section
  graphics_context_set_fill_color(ctx, currentTheme.ringDayColor);
  for (int i = dayStartPos; i != dayEndPos; i = (i + 1) % numPositions) {
    GPoint pipPos = getPipPosition(i, numPositions, innerBounds);
    graphics_fill_rect(ctx,
                       GRect(pipPos.x - thickness / 2, pipPos.y - thickness / 2,
                             thickness, thickness),
                       0, GCornerNone);
  }

  // Calculate positioning of twilight interface blocks
  int boxSize = thickness;
  graphics_context_set_stroke_color(ctx, currentTheme.ringStrokeColor);
  graphics_context_set_stroke_width(ctx, strokeWidth);

  GRect twilightStartRect = GRect(dayStart.x - boxSize / 2,
                                  dayStart.y - boxSize / 2, boxSize, boxSize);
  GRect twilightEndRect =
      GRect(dayEnd.x - boxSize / 2, dayEnd.y - boxSize / 2, boxSize, boxSize);

  // Correct boundaries of the twilight blocks, if necessary
  // (this ensures that the ring doesn't "break" if a block is on an edge)
  twilightStartRect = snap_to_edges(twilightStartRect, bounds, thickness);
  twilightEndRect = snap_to_edges(twilightEndRect, bounds, thickness);

  // draw sunrise block
  graphics_context_set_fill_color(ctx, currentTheme.ringSunriseColor);
  graphics_fill_rect(ctx, twilightStartRect, 0, GCornerNone);
  graphics_draw_rect(
      ctx, GRect(twilightStartRect.origin.x - 2, twilightStartRect.origin.y - 2,
                 twilightStartRect.size.w + 4, twilightStartRect.size.h + 4));

  // draw sunset block
  graphics_context_set_fill_color(ctx, currentTheme.ringSunsetColor);
  graphics_fill_rect(ctx, twilightEndRect, 0, GCornerNone);
  graphics_draw_rect(
      ctx, GRect(twilightEndRect.origin.x - 2, twilightEndRect.origin.y - 2,
                 twilightEndRect.size.w + 4, twilightEndRect.size.h + 4));

  // Draw calendar event arcs over the ring
  GRect centerBounds =
      GRect(bounds.origin.x + thickness, bounds.origin.y + thickness,
            bounds.size.w - thickness * 2, bounds.size.h - thickness * 2);

  int eventMargin = 3;
  int eventThickness = thickness - 2 * eventMargin;
  GRect eventBounds = 
      GRect(bounds.origin.x + eventMargin, bounds.origin.y + eventMargin,
            bounds.size.w - eventMargin * 2, bounds.size.h - eventMargin * 2);

  for (int e = 0; e < g_calendar_event_count; e++) {
    CalendarEvent *ev = &g_calendar_events[e];
    int shiftedStartMin = (ev->start_min + 15 * 60) % (24 * 60);
    int shiftedEndMin   = (ev->end_min   + 15 * 60) % (24 * 60);
    int startPip = (int)((shiftedStartMin / 1440.0f) * numPositions + 0.5f);
    int endPip   = (int)((shiftedEndMin   / 1440.0f) * numPositions + 0.5f);
    GColor evColor = (GColor){.argb = ev->color};

    // Event arc in event color
    graphics_context_set_fill_color(ctx, evColor);
    graphics_context_set_stroke_color(ctx, evColor);
    for (int i = startPip; i != endPip; i = (i + 1) % numPositions) {
      GPoint pipPos = getPipPosition(i, numPositions, centerBounds);
      GPoint nextPipPos = getPipPosition((i + 1) % numPositions, numPositions, centerBounds);

      bool horizontal = pipPos.x != nextPipPos.x;
      int width = horizontal ? abs(pipPos.x - nextPipPos.x) : eventThickness;
      int height = horizontal ? eventThickness : abs(pipPos.y - nextPipPos.y);
      int x = horizontal ? int_min(pipPos.x, nextPipPos.x) : (pipPos.x < bounds.size.w / 2 ? eventMargin : pipPos.x + eventMargin);
      int y = horizontal ? (pipPos.y < bounds.size.h / 2 ? eventMargin : pipPos.y + eventMargin) : int_min(pipPos.y, nextPipPos.y);
      GRect pipRect = GRect(x, y, width, height);
      snap_to_bounds(&pipRect, eventBounds, thickness);
      graphics_fill_rect(ctx, pipRect, 1, GCornerNone);
    }
  }

  // cue the sun!
  graphics_context_set_fill_color(ctx, currentTheme.sunFillColor);
  graphics_context_set_stroke_color(ctx, currentTheme.sunStrokeColor);
  graphics_fill_circle(ctx, sunPos, SUN_DIAMETER);
  graphics_draw_circle(ctx, sunPos, SUN_DIAMETER);
}

// Hit-test a tap point against the 24-hour ring and return the index of the calendar
// event whose arc is closest to the tap (circular minute distance).  Returns -1 when
// there are no events.  Uses the same geometry as the event arc drawing loop above, so
// the hit-test is guaranteed to match what the user sees.
int calendar_find_event_at_point(GPoint tap, GRect layer_bounds) {
  if (g_calendar_event_count == 0) return -1;

  int thickness = RING_THICKNESS;
  int numPositions = 96;

  // Recompute centerBounds exactly as in draw_ring_layer
  GRect centerBounds = GRect(
      layer_bounds.origin.x + thickness,
      layer_bounds.origin.y + thickness,
      layer_bounds.size.w - thickness * 2,
      layer_bounds.size.h - thickness * 2);

  // Step 1: find the pip position closest to the tap (squared distance, no sqrt needed)
  int best_pip = 0;
  int best_dist_sq = 100000; // larger than any possible squared distance on the screen
  for (int i = 0; i < numPositions; i++) {
    GPoint p = getPipPosition(i, numPositions, centerBounds);
    int dx = p.x - tap.x;
    int dy = p.y - tap.y;
    int dist_sq = dx * dx + dy * dy;
    if (dist_sq < best_dist_sq) {
      best_dist_sq = dist_sq;
      best_pip = i;
    }
  }

  // Step 2: convert best pip → minute-of-day (inverse of the +15h ring shift)
  // pip * (1440/96) = pip * 15 = shiftedMin; then un-shift.
  int shifted_min = best_pip * 15;
  int tapped_minute = (shifted_min - 15 * 60 + 24 * 60) % (24 * 60);

  // Step 3: find the event with the smallest circular minute-distance to tapped_minute
  int best_event = 0;
  int best_event_dist = 100000;
  for (int e = 0; e < g_calendar_event_count; e++) {
    CalendarEvent *ev = &g_calendar_events[e];
    int dist;
    bool wraps = ev->end_min < ev->start_min; // event crosses midnight
    bool inside = wraps
        ? (tapped_minute >= ev->start_min || tapped_minute <= ev->end_min)
        : (tapped_minute >= ev->start_min && tapped_minute <= ev->end_min);

    if (inside) {
      dist = 0;
    } else {
      int d_start = abs(tapped_minute - (int)ev->start_min);
      int d_end   = abs(tapped_minute - (int)ev->end_min);
      d_start = int_min(d_start, 1440 - d_start); // take the shorter arc around the clock
      d_end   = int_min(d_end,   1440 - d_end);
      dist = int_min(d_start, d_end);
    }
    if (dist < best_event_dist) {
      best_event_dist = dist;
      best_event = e;
    }
  }

  return best_event;
}

#endif
