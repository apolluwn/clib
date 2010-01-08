/*
  Copyright (c) 2005,2009 Eliot Dresselhaus

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/* High speed event logging with much thanks to Dave Barach. */

#ifndef included_clib_elog_h
#define included_clib_elog_h

#include <clib/cache.h>
#include <clib/error.h>		/* for ASSERT */
#include <clib/serialize.h>
#include <clib/time.h>		/* for clib_cpu_time_now */

typedef struct{
  union {
    /* Absolute time stamp in CPU clock cycles. */
    u64 time_cycles;

    /* Absolute time as floating point number in seconds. */
    f64 time;
  };

  /* Event type index. */
  u16 type;

  /* Track for this event.  Tracks allow events to be sorted and
     displayed by track.  Think of 2 dimensional display with time and
     track being the x and y axes.*/
  u16 track;

  /* 20-bytes of data follows and pads to 32 bytes. */
  u8 data[20];
} elog_event_t;

typedef struct {
  /* Type index plus one assigned to this type.
     This is used to mark type as seen. */
  u32 type_index_plus_one;

  /* String table as a vector constructed when type is registered. */
  char ** string_table;

  /* Format string. (example: "my-event (%d,%d)"). */
  char * format;

  /* Specifies how arguments to format are parsed from event data.
     String of characters '0' '1' or '2' '3' to specify log2 size of data
     (e.g. for u8, u16, u32 or u64),
     's' means argument is an index into string table for this type.
     'e' is a float,
     'f' is a double. */
  char * format_args;

  /* Function name generating event. */
  char * function;

  u32 n_data_bytes;

  /* Number of elements in string table. */
  u32 n_strings;

  /* String table for enum/number to string formatting. */
  char * strings[];
} elog_event_type_t;

typedef struct {
  /* Track name vector. */
  char * name;

  /* Set to one when track has been added to
     main structure. */
  u32 track_index_plus_one;
} elog_track_t;

typedef struct {
  /* CPU cycle counter. */
  u64 cpu;

  /* OS timer in nano secs since epoch Jan 1 1970. */
  u64 os_nsec;
} elog_time_stamp_t;

typedef struct {
  /* Total number of events in buffer (long/short form both count
     as 1 event). */
  u64 n_total_events;

  /* When count reaches limit logging is disabled.  This is
     used for event triggers. */
  u64 n_total_events_disable_limit;

  /* Dummy event to use when logger is disabled. */
  elog_event_t dummy_event;

  /* Vector of events (circular buffer).  Power of 2 size.
     Used when events are being collected. */
  elog_event_t * event_ring;

  /* Power of 2 number of elements in ring. */
  u32 event_ring_size;

  /* Set/unset to globally enable/disable logging of events. */
  u32 is_enabled;

  /* Vector of event types. */
  elog_event_type_t * event_types;

  /* Hash table mapping type format to type index. */
  uword * event_type_by_format;

  /* Vector of tracks. */
  elog_track_t * tracks;

  /* Default track. */
  elog_track_t default_track;

  /* Place holder for CPU clock frequency. */
  clib_time_t cpu_timer;

  elog_time_stamp_t init_time, serialize_time;

  /* Use serialize_time and init_time to give estimate for
     cpu clock frequency. */
  f64 nsec_per_cpu_clock;

  /* Vector of events converted to generic form after collection. */
  elog_event_t * events;
} elog_main_t;

static always_inline uword
elog_n_events_in_buffer (elog_main_t * em)
{ return clib_min (em->n_total_events, em->event_ring_size); }

static always_inline uword
elog_buffer_capacity (elog_main_t * em)
{ return em->event_ring_size; }

static always_inline void
elog_enable_disable (elog_main_t * em, int is_enabled)
{ em->is_enabled = is_enabled; }

static always_inline void
elog_reset_buffer (elog_main_t * em)
{
  em->n_total_events = 0;
  em->n_total_events_disable_limit = ~0ULL;
}

/* Disable logging after specified number of ievents have been logged.
   This is used as a "debug trigger" when a certain event has occurred.
   Events will be logged both before and after the "event" but the
   event will not be lost as long as N < RING_SIZE. */
static always_inline void
elog_disable_after_events (elog_main_t * em, uword n)
{ em->n_total_events_disable_limit = em->n_total_events + n; }

/* Signal a trigger.  We do this when we encounter an event that we want to save
   context around (before and after). */
static always_inline void
elog_disable_trigger (elog_main_t * em)
{ em->n_total_events_disable_limit = em->n_total_events + vec_len (em->event_ring) / 2; }

/* External function to register types/tracks. */
word elog_event_type_register (elog_main_t * em, elog_event_type_t * t);
word elog_track_register (elog_main_t * em, elog_track_t * t);

/* Add an event to the log.  Returns a pointer to the
   data for caller to write into. */
static always_inline void *
elog_event_data_inline (elog_main_t * em,
			elog_event_type_t * type,
			elog_track_t * track,
			u64 cpu_time,
			uword n_data_bytes)
{
  elog_event_t * e;
  word type_index, track_index;

  /* Return the user dummy memory to scribble data into. */
  if (PREDICT_FALSE (! em->is_enabled))
    return em->dummy_event.data;

  type_index = (word) type->type_index_plus_one - 1;
  track_index = (word) track->track_index_plus_one - 1;
  if (PREDICT_FALSE ((type_index | track_index) < 0))
    {
      if (type_index < 0)
	type_index = elog_event_type_register (em, type);
      if (track_index < 0)
	track_index = elog_track_register (em, track);
    }

  ASSERT (type_index < vec_len (em->event_types));
  ASSERT (track_index < vec_len (em->tracks));
  ASSERT (is_pow2 (vec_len (em->event_ring)));

  e = vec_elt_at_index (em->event_ring,
			em->n_total_events & (em->event_ring_size - 1));

  e->time_cycles = cpu_time;
  e->type = type_index;
  e->track = track_index;

  em->n_total_events += 1;

  /* Keep logging enabled as long as we are below trigger limit. */
  ASSERT (em->n_total_events_disable_limit != 0);
  em->is_enabled &= em->n_total_events < em->n_total_events_disable_limit;

  /* Return user data for caller to fill in. */
  return e->data;
}

/* External version of inline. */
void *
elog_event_data (elog_main_t * em,
		 elog_event_type_t * type,
		 elog_track_t * track,
		 u64 cpu_time,
		 uword n_data_bytes);

/* Non-inline version. */
static always_inline void *
elog_event_data_not_inline (elog_main_t * em,
			    elog_event_type_t * type,
			    elog_track_t * track,
			    u64 cpu_time,
			    uword n_data_bytes)
{
  /* Return the user dummy memory to scribble data into. */
  if (PREDICT_FALSE (! em->is_enabled))
    return em->dummy_event.data;
  return elog_event_data (em, type, track, cpu_time, n_data_bytes);
}

/* Most common form: log a single 32 bit datum. */
static always_inline void
elog (elog_main_t * em, elog_event_type_t * type, u32 data)
{
  u32 * d = elog_event_data_not_inline
    (em,
     type,
     &em->default_track,
     clib_cpu_time_now (),
     sizeof (d[0]));
  d[0] = data;
}

/* Inline version of above. */
static always_inline void
elog_inline (elog_main_t * em, elog_event_type_t * type, u32 data)
{
  u32 * d = elog_event_data_inline
    (em,
     type,
     &em->default_track,
     clib_cpu_time_now (),
     sizeof (d[0]));
  d[0] = data;
}

static always_inline void *
elog_data (elog_main_t * em, elog_event_type_t * type, elog_track_t * track)
{
  return elog_event_data_not_inline
    (em, type, track,
     clib_cpu_time_now (),
     type->n_data_bytes);
}

static always_inline void *
elog_data_inline (elog_main_t * em, elog_event_type_t * type, elog_track_t * track)
{
  return elog_event_data_inline
    (em, type, track,
     clib_cpu_time_now (),
     type->n_data_bytes);
}

/* Macro shorthands for generating/declaring events. */
#define __ELOG_TYPE_VAR(f) f
#define __ELOG_TRACK_VAR(f) f

#define ELOG_TYPE_DECLARE(f) static elog_event_type_t __ELOG_TYPE_VAR(f)

#define ELOG_TYPE_INIT_FORMAT_AND_FUNCTION(fmt,func) \
  { .format = fmt, .function = func, }

#define ELOG_TYPE_INIT(fmt) \
  ELOG_TYPE_INIT_FORMAT_AND_FUNCTION(fmt,(char *) __FUNCTION__)

#define ELOG_TYPE_DECLARE_HELPER(f,fmt,func)		\
  static elog_event_type_t __ELOG_TYPE_VAR(f) =		\
    ELOG_TYPE_INIT_FORMAT_AND_FUNCTION (fmt, func)

#define ELOG_TYPE_DECLARE_FORMAT_AND_FUNCTION(f,fmt)		\
  ELOG_TYPE_DECLARE_HELPER (f, fmt, (char *) __FUNCTION__)

#define ELOG_TYPE_DECLARE_FORMAT(f,fmt)		\
  ELOG_TYPE_DECLARE_HELPER (f, fmt, 0)

/* Shorthands with and without __FUNCTION__.
   D for decimal; X for hex.  F for __FUNCTION__. */
#define ELOG_TYPE(f,fmt) ELOG_TYPE_DECLARE_FORMAT_AND_FUNCTION(f,fmt)
#define ELOG_TYPE_D(f)  ELOG_TYPE_DECLARE_FORMAT (f, #f " %d")
#define ELOG_TYPE_X(f)  ELOG_TYPE_DECLARE_FORMAT (f, #f " 0x%x")
#define ELOG_TYPE_DF(f) ELOG_TYPE_DECLARE_FORMAT_AND_FUNCTION (f, #f " %d")
#define ELOG_TYPE_XF(f) ELOG_TYPE_DECLARE_FORMAT_AND_FUNCTION (f, #f " 0x%x")
#define ELOG_TYPE_FD(f) ELOG_TYPE_DECLARE_FORMAT_AND_FUNCTION (f, #f " %d")
#define ELOG_TYPE_FX(f) ELOG_TYPE_DECLARE_FORMAT_AND_FUNCTION (f, #f " 0x%x")

#define ELOG_TRACK_DECLARE(f) static elog_track_t __ELOG_TRACK_VAR(f)
#define ELOG_TRACK(f) ELOG_TRACK_DECLARE(f) = { .name = #f, }

/* Log 32 bits of data. */
#define ELOG(em,f,data) elog (em, &__ELOG_TYPE_VAR(f), data)
#define ELOG_INLINE(em,f,data) elog_inline (em, &__ELOG_TYPE_VAR(f), data)

/* Return data pointer to fill in. */
#define ELOG_TRACK_DATA(em,f,track) \
  elog_data (em, &__ELOG_TYPE_VAR(f), &__ELOG_TRACK_VAR(track))
#define ELOG_TRACK_DATA_INLINE(em,f,track) \
  elog_data_inline (em, &__ELOG_TYPE_VAR(f), &__ELOG_TRACK_VAR(track))

/* Shorthand with default track. */
#define ELOG_DATA(em,f) elog_data (em, &__ELOG_TYPE_VAR (f), &em->default_track)
#define ELOG_DATA_INLINE(em,f) elog_data_inline (em, &__ELOG_TYPE_VAR (f), &em->default_track)

/* Convert ievents to events and return them as a vector.
   Sets em->events to resulting vector. */
elog_event_t * elog_get_events (elog_main_t * em);

/* Convert ievents to events and return them as a vector with no side effects. */
elog_event_t * elog_peek_events (elog_main_t * em);

/* Merge two logs. */
void elog_merge (elog_main_t * dst, elog_main_t * src);

/* 2 arguments elog_main_t and elog_event_t to format event or track name. */
u8 * format_elog_event (u8 * s, va_list * va);
u8 * format_elog_track (u8 * s, va_list * va);

void serialize_elog_main (serialize_main_t * m, va_list * va);
void unserialize_elog_main (serialize_main_t * m, va_list * va);

void elog_init (elog_main_t * em, u32 n_ievents);

#ifdef CLIB_UNIX
static always_inline clib_error_t *
elog_write_file (elog_main_t * em, char * unix_file)
{
  serialize_main_t m;
  clib_error_t * error;

  if ((error = serialize_open_unix_file (&m, unix_file)))
    return error;
  if ((error = serialize (&m, serialize_elog_main, em)))
    return error;
  return serialize_close (&m);
}

static always_inline clib_error_t *
elog_read_file (elog_main_t * em, char * unix_file)
{
  serialize_main_t m;
  clib_error_t * error;

  if ((error = unserialize_open_unix_file (&m, unix_file)))
    return error;
  if ((error = unserialize (&m, unserialize_elog_main, em)))
    return error;
  return unserialize_close (&m);
}

#endif /* CLIB_UNIX */

#endif /* included_clib_elog_h */
