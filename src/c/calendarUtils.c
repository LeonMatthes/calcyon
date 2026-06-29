#include "calendarUtils.h"
#include <pebble.h>

CalendarEvent g_calendar_events[MAX_CALENDAR_EVENTS];
int g_calendar_event_count = 0;


void calendar_load_from_storage(void) {
  g_calendar_event_count = persist_read_int(CALENDAR_COUNT_PERSIST_KEY);
  if (g_calendar_event_count < 0 || g_calendar_event_count > MAX_CALENDAR_EVENTS) {
    g_calendar_event_count = 0;
    return;
  }

  int bytes_read = persist_read_data(CALENDAR_PERSIST_KEY, g_calendar_events,
                                      g_calendar_event_count * sizeof(CalendarEvent));
  if (bytes_read < 0) {
    g_calendar_event_count = 0;
  }
}

#if defined(PBL_PLATFORM_EMERY)
CalendarEventDetail g_event_details[MAX_CALENDAR_EVENTS];

void calendar_clear_details(void) {
  memset(g_event_details, 0, sizeof(g_event_details));
}
#endif // PBL_PLATFORM_EMERY
