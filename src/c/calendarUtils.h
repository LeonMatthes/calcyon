#pragma once

#include <pebble.h>

#define MAX_CALENDAR_EVENTS 64
#define CALENDAR_PERSIST_KEY 10
#define CALENDAR_COUNT_PERSIST_KEY 11

typedef struct __attribute__((__packed__)) {
  uint16_t start_min;
  uint16_t end_min;
  uint8_t  color;
  uint8_t  _pad;
} CalendarEvent;

extern CalendarEvent g_calendar_events[];
extern int g_calendar_event_count;

void calendar_load_from_storage(void);
