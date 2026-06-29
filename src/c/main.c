#include <pebble.h>

// #define FORCE_BACKLIGHT

#include "drawUtils.h"
#include "messaging.h"
#include "settings.h"
#include "solarUtils.h"
#include "calendarUtils.h"
#include "text_metrics.h"
#include "utils.h"
#include "widgets.h"

#define FORCE_12H false
#define TIME_STR_LEN 6

// Heartbeat timer: the watch requests data from the phone on this interval
#define UPDATE_REQUEST_INTERVAL_MS (30 * 60 * 1000) // 30 minutes
#define UPDATE_REQUEST_INITIAL_DELAY_MS (3 * 1000)   // 3 seconds after startup
#define UPDATE_REQUEST_RETRY_MS (30 * 1000)          // fast retry after outbox fail

// windows and layers
static Window *mainWindow;
static Layer *windowLayer;
static Layer *shiftingLayer;
static Layer *centerLayer;
static Layer *ringLayer;
static Layer *infoLayer;
static bool s_quick_view_visible = false;

// Time string (populated each tick)
static char timeText[TIME_STR_LEN];

// Heartbeat timer for requesting data updates from the phone
static AppTimer *s_update_request_timer = NULL;

// ============================================================
// Slot descriptor — one per visible row in the layout
// ============================================================
typedef struct {
  const char *text;
  GFont font;
  int height; // true pixel height from metrics
  int offset; // top dead-space offset from metrics
  GColor color;
} SlotDescriptor;

// Buffer storage for each of the 4 widget slots. Populated by
// update_widget_text() on the minute tick / settings change, then read by
// draw_center_text() on every redraw — so redraws stay allocation-free.
static char widgetTextUS[WIDGET_TEXT_LEN]; // upper secondary
static char widgetTextUP[WIDGET_TEXT_LEN]; // upper primary
static char widgetTextLP[WIDGET_TEXT_LEN]; // lower primary
static char widgetTextLS[WIDGET_TEXT_LEN]; // lower secondary

// --- Event detail / drill-down state (emery / touch screen only) ---
#if defined(PBL_PLATFORM_EMERY)
// s_selected_event == -1  → clock mode (normal face)
// s_selected_event >= 0   → detail mode (shows event info in center panel)
static int s_selected_event = -1;
static char s_detail_title[EVENT_TITLE_LEN];
static char s_detail_time[24]; // e.g. "9:00 - 10:30" or "9:00 AM - 10:30 AM"
static char s_detail_location[EVENT_LOC_LEN];
static AppTimer *s_detail_dismiss_timer = NULL;

// Touch double-tap detection state.
static int16_t  s_touchdown_x = 0, s_touchdown_y = 0;
static uint32_t s_tap1_ms = 0;  // timestamp (ms) of first pending tap; 0 = none
static int16_t  s_tap1_x = 0, s_tap1_y = 0;

#define DETAIL_AUTO_DISMISS_MS (20 * 1000)
#define SWIPE_THRESHOLD_PX     30
#define DOUBLE_TAP_WINDOW_MS   400
#define DOUBLE_TAP_RADIUS_SQ   (30 * 30)
#endif // PBL_PLATFORM_EMERY

static void update_widget_text(void) {
  if (globalSettings.widgetUpperSecondary[0] != '\0') {
    widget_get_text(globalSettings.widgetUpperSecondary, widgetTextUS,
                    WIDGET_TEXT_LEN);
  } else {
    widgetTextUS[0] = '\0';
  }
  if (globalSettings.widgetUpperPrimary[0] != '\0') {
    widget_get_text(globalSettings.widgetUpperPrimary, widgetTextUP,
                    WIDGET_TEXT_LEN);
  } else {
    widgetTextUP[0] = '\0';
  }
  if (globalSettings.widgetLowerPrimary[0] != '\0') {
    widget_get_text(globalSettings.widgetLowerPrimary, widgetTextLP,
                    WIDGET_TEXT_LEN);
  } else {
    widgetTextLP[0] = '\0';
  }
  if (globalSettings.widgetLowerSecondary[0] != '\0') {
    widget_get_text(globalSettings.widgetLowerSecondary, widgetTextLS,
                    WIDGET_TEXT_LEN);
  } else {
    widgetTextLS[0] = '\0';
  }
}

#if defined(PBL_PLATFORM_EMERY)

// Populate s_detail_* buffers from the event at idx.  Called whenever
// s_selected_event changes so that draw_event_detail() can stay allocation-free.
static void event_detail_refresh(int idx) {
  if (idx < 0 || idx >= g_calendar_event_count) return;
  CalendarEvent *ev = &g_calendar_events[idx];
  CalendarEventDetail *det = &g_event_details[idx];

  // Title (show placeholder when details haven't arrived yet)
  if (det->title[0] != '\0') {
    strncpy(s_detail_title, det->title, sizeof(s_detail_title) - 1);
  } else {
    strncpy(s_detail_title, "(loading...)", sizeof(s_detail_title) - 1);
  }
  s_detail_title[sizeof(s_detail_title) - 1] = '\0';

  // Time range: "H:MM - H:MM" (24h) or "H:MM AM - H:MM PM" (12h)
  int s_h = ev->start_min / 60, s_m = ev->start_min % 60;
  int e_h = ev->end_min   / 60, e_m = ev->end_min   % 60;
  if (clock_is_24h_style() && !FORCE_12H) {
    snprintf(s_detail_time, sizeof(s_detail_time),
             "%d:%02d - %d:%02d", s_h, s_m, e_h, e_m);
  } else {
    const char *s_ap = s_h < 12 ? "AM" : "PM";
    const char *e_ap = e_h < 12 ? "AM" : "PM";
    int sh12 = s_h % 12; if (sh12 == 0) sh12 = 12;
    int eh12 = e_h % 12; if (eh12 == 0) eh12 = 12;
    snprintf(s_detail_time, sizeof(s_detail_time),
             "%d:%02d%s - %d:%02d%s", sh12, s_m, s_ap, eh12, e_m, e_ap);
  }
  s_detail_time[sizeof(s_detail_time) - 1] = '\0';

  // Location (or URL fallback; may be empty — panel collapses gracefully)
  strncpy(s_detail_location, det->location, sizeof(s_detail_location) - 1);
  s_detail_location[sizeof(s_detail_location) - 1] = '\0';
}

// Draw the event detail center panel (replaces the clock when s_selected_event >= 0).
// Reuses the same slot/PUSH_SLOT pattern as draw_center_text so layout is consistent.
static void draw_event_detail(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  CalendarEvent *ev = &g_calendar_events[s_selected_event];
  GColor ev_color = (GColor){.argb = ev->color};

  GFont primary_font   = fonts_get_system_font(FONT_WIDGET_PRIMARY);
  GFont secondary_font = fonts_get_system_font(FONT_WIDGET_SECONDARY);

  SlotDescriptor slots[3];
  int num_slots = 0;

#define PUSH_SLOT_D(txt, fnt, h, off, col)  \
  do {                                      \
    slots[num_slots].text   = (txt);        \
    slots[num_slots].font   = (fnt);        \
    slots[num_slots].height = (h);          \
    slots[num_slots].offset = (off);        \
    slots[num_slots].color  = (col);        \
    num_slots++;                            \
  } while (0)

  // Title — tinted with the calendar color to tie it to the ring arc
  PUSH_SLOT_D(s_detail_title,
              primary_font, FONT_WIDGET_PRIMARY_HEIGHT, FONT_WIDGET_PRIMARY_OFFSET,
              ev_color);
  // Time range
  PUSH_SLOT_D(s_detail_time,
              primary_font, FONT_WIDGET_PRIMARY_HEIGHT, FONT_WIDGET_PRIMARY_OFFSET,
              globalSettings.subtextPrimaryColor);
  // Location / URL fallback — omit row entirely when empty
  if (s_detail_location[0] != '\0') {
    PUSH_SLOT_D(s_detail_location,
                secondary_font, FONT_WIDGET_SECONDARY_HEIGHT, FONT_WIDGET_SECONDARY_OFFSET,
                globalSettings.subtextSecondaryColor);
  }

#undef PUSH_SLOT_D

  // Vertically center the slot block (same math as draw_center_text)
  int total_height = 0;
  for (int i = 0; i < num_slots; i++) {
    total_height += slots[i].height;
    if (i < num_slots - 1) total_height += LINE_PADDING;
  }
  int y = (bounds.size.h - total_height) / 2;

  for (int i = 0; i < num_slots; i++) {
    SlotDescriptor *s = &slots[i];
    graphics_context_set_text_color(ctx, s->color);
    graphics_draw_text(ctx, s->text, s->font,
                       GRect(0, y - s->offset, bounds.size.w, s->height),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    y += s->height + LINE_PADDING;
  }
}

#endif // PBL_PLATFORM_EMERY

static void draw_center_text(Layer *layer, GContext *ctx) {
#if defined(PBL_PLATFORM_EMERY)
  // In detail mode, replace the clock with the selected event's info.
  if (s_selected_event >= 0) {
    draw_event_detail(layer, ctx);
    return;
  }
#endif

  GRect bounds = layer_get_bounds(layer);
  bool useLargeFont = globalSettings.useLargeFonts;

  bool useNightColors = false;
  if (globalSettings.useNightTheme) {
    struct tm *timeInfo = getCurrentTime();
    int currentMinutes = timeInfo->tm_hour * 60 + timeInfo->tm_min;
    useNightColors = isNightTime(currentMinutes);
  }

  // ---- Font selection ----
  GFont time_font = fonts_get_system_font(FONT_TIME);
  int time_height = FONT_TIME_HEIGHT;
  int time_offset = FONT_TIME_OFFSET;

  GFont primary_font = fonts_get_system_font(
      useLargeFont ? FONT_WIDGET_PRIMARY_LARGE : FONT_WIDGET_PRIMARY);
  int primary_height = useLargeFont ? FONT_WIDGET_PRIMARY_LARGE_HEIGHT
                                    : FONT_WIDGET_PRIMARY_HEIGHT;
  int primary_offset = useLargeFont ? FONT_WIDGET_PRIMARY_LARGE_OFFSET
                                    : FONT_WIDGET_PRIMARY_OFFSET;

  GFont secondary_font = fonts_get_system_font(
      useLargeFont ? FONT_WIDGET_SECONDARY_LARGE : FONT_WIDGET_SECONDARY);
  int secondary_height = useLargeFont ? FONT_WIDGET_SECONDARY_LARGE_HEIGHT
                                      : FONT_WIDGET_SECONDARY_HEIGHT;
  int secondary_offset = useLargeFont ? FONT_WIDGET_SECONDARY_LARGE_OFFSET
                                      : FONT_WIDGET_SECONDARY_OFFSET;

  // ---- Color selection ----
  GColor timeColor =
      useNightColors ? globalSettings.nightTimeColor : globalSettings.timeColor;
  GColor primaryColor = useNightColors ? globalSettings.nightSubtextPrimaryColor
                                       : globalSettings.subtextPrimaryColor;
  GColor secondaryColor = useNightColors
                              ? globalSettings.nightSubtextSecondaryColor
                              : globalSettings.subtextSecondaryColor;

  // ---- Build ordered slot list (top to bottom) ----
  // We use a fixed-size array and fill only active (non-empty) slots.
#define MAX_SLOTS 5
  SlotDescriptor slots[MAX_SLOTS];
  int num_slots = 0;

// Helper macro to push a slot
#define PUSH_SLOT(txt, fnt, h, off, col)                                       \
  do {                                                                         \
    slots[num_slots].text = (txt);                                             \
    slots[num_slots].font = (fnt);                                             \
    slots[num_slots].height = (h);                                             \
    slots[num_slots].offset = (off);                                           \
    slots[num_slots].color = (col);                                            \
    num_slots++;                                                               \
  } while (0)

  // Upper secondary (topmost)
  if (!s_quick_view_visible && widgetTextUS[0] != '\0') {
    PUSH_SLOT(widgetTextUS,
              globalSettings.usePrimaryFontForAllWidgets ? primary_font
                                                          : secondary_font,
              globalSettings.usePrimaryFontForAllWidgets ? primary_height
                                                          : secondary_height,
              globalSettings.usePrimaryFontForAllWidgets ? primary_offset
                                                          : secondary_offset,
              secondaryColor);
  }

  // Upper primary
  if (widgetTextUP[0] != '\0') {
    PUSH_SLOT(widgetTextUP, primary_font, primary_height, primary_offset,
              primaryColor);
  }

  // Time (always present)
  PUSH_SLOT(timeText, time_font, time_height, time_offset, timeColor);

  // Lower primary
  if (widgetTextLP[0] != '\0') {
    PUSH_SLOT(widgetTextLP, primary_font, primary_height, primary_offset,
              primaryColor);
  }

  // Lower secondary (bottommost)
  if (!s_quick_view_visible && widgetTextLS[0] != '\0') {
    PUSH_SLOT(widgetTextLS,
              globalSettings.usePrimaryFontForAllWidgets ? primary_font
                                                          : secondary_font,
              globalSettings.usePrimaryFontForAllWidgets ? primary_height
                                                          : secondary_height,
              globalSettings.usePrimaryFontForAllWidgets ? primary_offset
                                                          : secondary_offset,
              secondaryColor);
  }

#undef PUSH_SLOT
#undef MAX_SLOTS

  // ---- Compute total height (with padding between each slot) ----
  int total_height = 0;
  for (int i = 0; i < num_slots; i++) {
    total_height += slots[i].height;
    if (i < num_slots - 1) {
      total_height += LINE_PADDING;
    }
  }

  // ---- Vertically center the block ----
  int y = (bounds.size.h - total_height) / 2;

  // ---- Draw each slot ----
  for (int i = 0; i < num_slots; i++) {
    SlotDescriptor *s = &slots[i];
    graphics_context_set_text_color(ctx, s->color);
    graphics_draw_text(ctx, s->text, s->font,
                       GRect(0, y - s->offset, bounds.size.w, s->height),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                       NULL);
    y += s->height + LINE_PADDING;
  }
}

// Resize literally everything on quick view
static void quickViewLayerReposition() {
  GRect full_bounds = layer_get_bounds(windowLayer);
  GRect bounds = layer_get_unobstructed_bounds(windowLayer);
  s_quick_view_visible = bounds.size.h < full_bounds.size.h;

  // Resize the shifting and center layer based on the current unobstructed
  // bounds
  layer_set_frame(shiftingLayer,
                  GRect(0, 0, full_bounds.size.w, bounds.size.h));
  layer_set_frame(ringLayer, GRect(0, 0, full_bounds.size.w, bounds.size.h));

  GRect centerFrame = GRect(EDGE_THICKNESS, EDGE_THICKNESS,
                            full_bounds.size.w - 2 * EDGE_THICKNESS,
                            bounds.size.h - 2 * EDGE_THICKNESS);
  layer_set_frame(centerLayer, centerFrame);

  layer_set_frame(infoLayer, GRect(0, centerFrame.origin.y, full_bounds.size.w,
                                   centerFrame.size.h));

  // Mark everything as dirty to redraw
  layer_mark_dirty(ringLayer);
  layer_mark_dirty(centerLayer);
  layer_mark_dirty(infoLayer);
}

static void update_clock() {
  Settings_updateDynamicSettings();

  struct tm *timeInfo = getCurrentTime();

  // set time string
  if (clock_is_24h_style() && !FORCE_12H) {
    strftime(timeText, TIME_STR_LEN, "%H:%M", timeInfo);
  } else {
    strftime(timeText, TIME_STR_LEN, "%I:%M", timeInfo);
  }

  if (!globalSettings.showLeadingZero) {
    if (timeText[0] == '0') {
      for (int i = 0; i < TIME_STR_LEN - 1; i++) {
        timeText[i] = timeText[i + 1];
      }
    }
  }

  // Re-parse widget format strings into static buffers; the layer update
  // callback only reads them, so redraws (e.g. obstruction animations) don't
  // re-parse on every frame.
  update_widget_text();

  // ensure colors are updated based on settings
  window_set_background_color(mainWindow,
                              getCurrentColorTheme().ringStrokeColor);

  // if sunrise/sunset has not yet been calculated, do that
  if (currentSolarInfo.sunriseMinute == DEFAULT_SUNRISE_TIME &&
      currentSolarInfo.sunsetMinute == DEFAULT_SUNSET_TIME) {
    solarUtils_recalculateSolarData();
  }

  // redraw solar ring layer
  layer_mark_dirty(ringLayer);
  layer_mark_dirty(centerLayer);
}

// settings might have changed, so recalculate solar data and refresh screen
void onSettingsChanged() {
  solarUtils_recalculateSolarData();

  APP_LOG(APP_LOG_LEVEL_INFO, "I guess settings changed");

  update_clock();
}

// Event fires frequently, while obstruction is appearing or disappearing
static void onUnobstructedAreaChange(AnimationProgress progress,
                                     void *context) {
  quickViewLayerReposition();
}

// Event fires once, after obstruction appears or disappears
static void onUnobstructedAreaDidChange(void *context) {
  quickViewLayerReposition();
}

static void main_window_load(Window *window) {
  // get information about the Window
  windowLayer = window_get_root_layer(window);
  window_set_background_color(window, getCurrentColorTheme().ringStrokeColor);
  GRect bounds = layer_get_bounds(windowLayer);

  shiftingLayer = layer_create(bounds);
  layer_add_child(windowLayer, shiftingLayer);

  // create central rectangle
  GRect centerFrame = GRect(
      bounds.origin.x + EDGE_THICKNESS, bounds.origin.y + EDGE_THICKNESS,
      bounds.size.w - EDGE_THICKNESS * 2, bounds.size.h - EDGE_THICKNESS * 2);
  centerLayer = layer_create(centerFrame);
  layer_set_update_proc(centerLayer, draw_center_layer);

  layer_add_child(shiftingLayer, centerLayer);

  infoLayer = layer_create(
      GRect(0, centerFrame.origin.y, bounds.size.w, centerFrame.size.h));
  layer_set_update_proc(infoLayer, draw_center_text);
  layer_add_child(shiftingLayer, infoLayer);

  // create ring layer
  ringLayer = layer_create(bounds);
  layer_set_update_proc(ringLayer, draw_ring_layer);
  layer_add_child(shiftingLayer, ringLayer);

  // subscribe to the unobstructed area events
  UnobstructedAreaHandlers handlers = {.change = onUnobstructedAreaChange,
                                       .did_change =
                                           onUnobstructedAreaDidChange};
  unobstructed_area_service_subscribe(handlers, NULL);

  // just in case quick view is open on load
  quickViewLayerReposition();

  // make sure the time is displayed from the start
  update_clock();
}

static void main_window_unload(Window *window) {
  // destroy everything
  layer_destroy(ringLayer);
  layer_destroy(infoLayer);
  layer_destroy(centerLayer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_clock();
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHeartRateUpdate) {
    update_clock();
  }
}
#endif

static void update_request_timer_callback(void *data);

static void schedule_next_update_request(uint32_t delay_ms) {
  if (s_update_request_timer) {
    app_timer_cancel(s_update_request_timer);
  }
  s_update_request_timer =
      app_timer_register(delay_ms, update_request_timer_callback, NULL);
}

static void update_request_timer_callback(void *data) {
  // Schedule the next normal-interval tick *first*, so that a sync outbox
  // failure inside messaging_request_update() can call on_request_failed()
  // and pull the next attempt forward without us clobbering it afterward.
  schedule_next_update_request(UPDATE_REQUEST_INTERVAL_MS);
  messaging_request_update();
}

static void on_request_failed(void) {
  schedule_next_update_request(UPDATE_REQUEST_RETRY_MS);
}

// ============================================================
// Event detail drill-down — auto-dismiss and touch handling
// (emery / touch screen only)
// ============================================================
#if defined(PBL_PLATFORM_EMERY)

static void detail_dismiss(void) {
  s_selected_event = -1;
  layer_mark_dirty(infoLayer);
}

static void detail_auto_dismiss_callback(void *data) {
  s_detail_dismiss_timer = NULL;
  detail_dismiss();
}

static void schedule_auto_dismiss(void) {
  if (s_detail_dismiss_timer) {
    app_timer_reschedule(s_detail_dismiss_timer, DETAIL_AUTO_DISMISS_MS);
  } else {
    s_detail_dismiss_timer =
        app_timer_register(DETAIL_AUTO_DISMISS_MS, detail_auto_dismiss_callback, NULL);
  }
}

static void cancel_auto_dismiss(void) {
  if (s_detail_dismiss_timer) {
    app_timer_cancel(s_detail_dismiss_timer);
    s_detail_dismiss_timer = NULL;
  }
}

// Forward-declare the hit-test function defined in drawUtils_rect.c.
int calendar_find_event_at_point(GPoint tap, GRect layer_bounds);

static void touch_handler(const TouchEvent *event, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Touch event: %d@%d", event->x, event->y);

  if (event->type == TouchEvent_Touchdown) {
    s_touchdown_x = event->x;
    s_touchdown_y = event->y;
    return;
  }

  if (event->type != TouchEvent_Liftoff) return;

  int dx = (int)event->x - (int)s_touchdown_x;
  int dy = (int)event->y - (int)s_touchdown_y;

  // --- Horizontal swipe (prev/next event while in detail mode) ---
  if (abs(dx) > SWIPE_THRESHOLD_PX && abs(dx) > abs(dy) && s_selected_event >= 0) {
    if (g_calendar_event_count > 0) {
      if (dx > 0) {
        // Swipe right → previous event (earlier in day)
        s_selected_event = (s_selected_event - 1 + g_calendar_event_count)
                           % g_calendar_event_count;
      } else {
        // Swipe left → next event (later in day)
        s_selected_event = (s_selected_event + 1) % g_calendar_event_count;
      }
      event_detail_refresh(s_selected_event);
      layer_mark_dirty(infoLayer);
      schedule_auto_dismiss();
    }
    s_tap1_ms = 0; // swipe consumed — reset pending-tap state
    return;
  }

  // --- Tap: accumulate toward double-tap ---
  time_t now_sec;
  uint16_t now_ms_frac;
  time_ms(&now_sec, &now_ms_frac);
  uint32_t now_ms = (uint32_t)(now_sec % 86400) * 1000 + (uint32_t)now_ms_frac;

  int tdx = (int)event->x - (int)s_tap1_x;
  int tdy = (int)event->y - (int)s_tap1_y;
  bool near_prev  = (tdx * tdx + tdy * tdy) <= DOUBLE_TAP_RADIUS_SQ;
  bool in_window  = (s_tap1_ms != 0) &&
                    ((now_ms - s_tap1_ms) <= DOUBLE_TAP_WINDOW_MS);

  if (in_window && near_prev) {
    // Second tap confirmed → fire double-tap action.
    s_tap1_ms = 0;

    if (g_calendar_event_count == 0) return;

    int found = calendar_find_event_at_point(
        GPoint(event->x, event->y),
        layer_get_bounds(ringLayer));

    if (found < 0) return;

    if (found == s_selected_event) {
      // Double-tapping the same event dismisses the detail panel.
      cancel_auto_dismiss();
      detail_dismiss();
    } else {
      s_selected_event = found;
      event_detail_refresh(found);
      layer_mark_dirty(infoLayer);
      schedule_auto_dismiss();
    }
  } else {
    // First tap — record and wait for the second.
    s_tap1_ms = now_ms;
    s_tap1_x  = event->x;
    s_tap1_y  = event->y;
  }
}

#endif // PBL_PLATFORM_EMERY

static void init() {
#ifdef FORCE_BACKLIGHT
  light_enable(true);
#endif

  // load those settings
  Settings_init();

  // init solar stuff
  solarUtils_init();

  // init the messaging thing
  messaging_init(onSettingsChanged, on_request_failed);

  // load calendar events from storage
  calendar_load_from_storage();

  // Create main Window element and assign to pointer
  mainWindow = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(
      mainWindow,
      (WindowHandlers){.load = main_window_load, .unload = main_window_unload});

  window_stack_push(mainWindow, true);

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif

#if defined(PBL_PLATFORM_EMERY)
  // Subscribe early so the wake double-tap's touches are delivered to the app.
  touch_service_subscribe(touch_handler, NULL);
  APP_LOG(APP_LOG_LEVEL_INFO, "Touch service: %d", (int)touch_service_is_enabled());
#endif

  // Schedule initial update request with short delay to give PKJS time to start
  schedule_next_update_request(UPDATE_REQUEST_INITIAL_DELAY_MS);
}

static void deinit() {
  if (s_update_request_timer) {
    app_timer_cancel(s_update_request_timer);
  }
#if defined(PBL_PLATFORM_EMERY)
  cancel_auto_dismiss();
  touch_service_unsubscribe();
  APP_LOG(APP_LOG_LEVEL_INFO, "disabling touch service");
#endif
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(mainWindow);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
