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

#include <clib/elog.h>
#include <clib/cache.h>
#include <clib/error.h>
#include <clib/format.h>

/* Dummy events for data to be written to when logging is disabled. */
elog_ievent_t elog_dummy_ievents[2];

/* External function to register types. */
word elog_event_type_register (elog_main_t * em, elog_event_type_t * t)
{
  word l = vec_len (em->event_types);

  t->type_index_plus_one = 1 + l;

  ASSERT (t->format);

  /* Default format is 1 32 bit number for each % is format string. */
  if (! t->format_args)
    {
      uword i, n_percent;
      uword l = strlen (t->format);

      for (i = n_percent = 0; i < l; i++)
	n_percent += (t->format[i] == '%'
		      && ! (i + 1 < l && t->format[i + 1] == '%'));
	  
      t->format_args = 0;
      for (i = 0; i < n_percent; i++)
	vec_add1 (t->format_args, '2');
      vec_add1 (t->format_args, 0);

      t->n_data_bytes = n_percent * sizeof (u32);
    }    

  vec_add1 (em->event_types, t[0]);

  return l;
}

u8 * format_elog_event (u8 * s, va_list * va)
{
  elog_main_t * em = va_arg (*va, elog_main_t *);
  elog_event_t * e = va_arg (*va, elog_event_t *);
  elog_event_type_t * t;
  char * p;
  u32 n_args, * a, args[sizeof (e->data)];
  void * d = (u8 *) e->data;

  t = vec_elt_at_index (em->event_types, e->type);

  p = t->format_args;
  a = args;
  while (*p)
    {
      u32 x = 0;
      switch (*p)
	{
	case '0': x = *( u8 *) d; d += 1; break;
	case '1': x = *(u16 *) d; d += 2; break;
	case '2': x = *(u32 *) d; d += 4; break;
	default: ASSERT (0); break;
	}

      *a++ = x;
      p++;
    }
  n_args = a - args;

  switch (n_args)
    {
    case 1:
      s = format (s, t->format, args[0]);
      break;
    case 2:
      s = format (s, t->format, args[0], args[1]);
      break;
    case 3:
      s = format (s, t->format, args[0], args[1], args[2]);
      break;
    case 4:
      s = format (s, t->format, args[0], args[1],
		  args[2], args[3]);
      break;
    case 5:
      s = format (s, t->format, args[0], args[1],
		  args[2], args[3], args[4]);
      break;
    case 6:
      s = format (s, t->format, args[0], args[1],
		  args[2], args[3], args[4], args[5]);
      break;

    default:
      ASSERT (0);
      break;
    }

  return s;
}

void elog_alloc (elog_main_t * em, u32 n_ievents)
{
  if (em->ievent_ring)
    vec_free_aligned (em->ievent_ring, CLIB_CACHE_LINE_BYTES);
  
  n_ievents = max_pow2 (n_ievents);

  /* Leave an empty ievent at end so we can always speculatively write
     and event there (possibly a long form event). */
  vec_resize_aligned (em->ievent_ring, n_ievents + 1, CLIB_CACHE_LINE_BYTES);

  /* Set ring size but don't include last element. */
  em->ievent_ring_size = n_ievents;
}

void elog_init (elog_main_t * em, u32 n_events)
{
  memset (em, 0, sizeof (em[0]));

  if (n_events > 0)
    elog_alloc (em, n_events);

  clib_time_init (&em->cpu_timer);
  em->n_total_ievents_disable_limit = ~0ULL;
}

static always_inline uword
elog_next_ievent_index (elog_main_t * em, uword i)
{
  ASSERT (i < vec_len (em->ievent_ring));
  i += 1;
  return i >= vec_len (em->ievent_ring) ? 0 : i;
}

static uword elog_ievent_range (elog_main_t * em, uword * lo)
{
  uword ring_len = em->ievent_ring_size;
  u64 i = em->n_total_ievents;

  /* Ring never wrapped? */
  if (i <= (u64) ring_len)
    {
      *lo = 0;
      return i;
    }
  else
    {
      *lo = elog_next_ievent_index (em, i);
      return ring_len;
    }
}

static always_inline uword
elog_ievent_to_event (elog_main_t * em,
		      elog_ievent_t * ie,
		      elog_event_t * e,
		      u64 * elapsed_time_return)
{
  u64 elapsed_time = *elapsed_time_return;
  uword i, is_long;

  is_long = elog_ievent_is_long_form (ie);
  e->type = elog_ievent_get_type (ie);
  e->track = elog_ievent_get_track (ie);

  elapsed_time += ie->dt_lo;

  for (i = 0; i < ARRAY_LEN (ie->data); i++)
    e->data[i] = ie->data[i];

  if (is_long)
    {
      for (i = 0; i < ARRAY_LEN (ie->data_continued); i++)
	e->data[ARRAY_LEN (ie->data) + i] = ie[1].data_continued[i];
      elapsed_time += (u64) ie[1].dt_hi << (u64) 32;
    }

  e->time = elapsed_time * em->cpu_timer.seconds_per_clock;
  *elapsed_time_return = elapsed_time;

  return is_long;
}

void elog_normalize_events (elog_main_t * em)
{
  u64 elapsed_time = 0;
  elog_event_t * e;
  elog_ievent_t * ie;
  uword i, j, n, is_long = 0;

  n = elog_ievent_range (em, &j);
  for (i = 0; i < n; i += 1 + is_long)
    {
      vec_add2 (em->events, e, 1);
      ie = vec_elt_at_index (em->ievent_ring, j);
      is_long = elog_ievent_to_event (em, ie, e, &elapsed_time);
      j += 1 + is_long;
      j = j >= vec_len (em->ievent_ring) ? 0 : j;
    }
}

static void
serialize_elog_ievent (serialize_main_t * m, va_list * va)
{
  elog_ievent_t * e = va_arg (*va, elog_ievent_t *);
  u32 i;

  serialize_integer (m, e->type_and_track, sizeof (e->type_and_track));
  serialize_integer (m, e->dt_lo, sizeof (e->dt_lo));
  for (i = 0; i < ARRAY_LEN (e->data); i++)
    serialize_integer (m, e->data[i], sizeof (e->data[i]));
}

static void
unserialize_elog_ievent (serialize_main_t * m, va_list * va)
{
  elog_ievent_t * e = va_arg (*va, elog_ievent_t *);
  u32 i;

  unserialize_integer (m, &e->type_and_track, sizeof (e->type_and_track));
  unserialize_integer (m, &e->dt_lo, sizeof (e->dt_lo));
  for (i = 0; i < ARRAY_LEN (e->data); i++)
    unserialize_integer (m, &e->data[i], sizeof (e->data[i]));
}

static void
serialize_elog_type (serialize_main_t * m, va_list * va)
{
  elog_event_type_t * t = va_arg (*va, elog_event_type_t *);
  int n = va_arg (*va, int);
  int i;
  for (i = 0; i < n; i++)
    {
      serialize_cstring (m, t[i].format);
      serialize_cstring (m, t[i].format_args);
      serialize_integer (m, t[i].type_index_plus_one, sizeof (t->type_index_plus_one));
      serialize_integer (m, t[i].n_data_bytes, sizeof (t->n_data_bytes));
    }
}

static void
unserialize_elog_type (serialize_main_t * m, va_list * va)
{
  elog_event_type_t * t = va_arg (*va, elog_event_type_t *);
  int n = va_arg (*va, int);
  int i;
  for (i = 0; i < n; i++)
    {
      unserialize_cstring (m, &t[i].format);
      unserialize_cstring (m, &t[i].format_args);
      unserialize_integer (m, &t[i].type_index_plus_one, sizeof (t->type_index_plus_one));
      unserialize_integer (m, &t[i].n_data_bytes, sizeof (t->n_data_bytes));
    }
}

static char * elog_serialize_magic = "elog v0";

void
serialize_elog_main (serialize_main_t * m, va_list * va)
{
  elog_main_t * em = va_arg (*va, elog_main_t *);
  uword i, j, n;

  serialize_cstring (m, elog_serialize_magic);

  serialize_integer (m, vec_len (em->ievent_ring), sizeof (u32));
  serialize (m, serialize_64, &em->n_total_ievents);
  serialize (m, serialize_f64, em->cpu_timer.seconds_per_clock);

  vec_serialize (m, em->event_types, serialize_elog_type);

  n = elog_ievent_range (em, &j);
  for (i = 0; i < n; i++, j = elog_next_ievent_index (em, j))
    serialize (m, serialize_elog_ievent, &em->ievent_ring[j]);
}

void
unserialize_elog_main (serialize_main_t * m, va_list * va)
{
  elog_main_t * em = va_arg (*va, elog_main_t *);
  u32 i, n_ievents;

  unserialize_check_magic (m, elog_serialize_magic,
			   strlen (elog_serialize_magic));

  unserialize_integer (m, &n_ievents, sizeof (n_ievents));
  elog_init (em, n_ievents);

  unserialize (m, unserialize_64, &em->n_total_ievents);
  unserialize (m, unserialize_f64, &em->cpu_timer.seconds_per_clock);

  vec_unserialize (m, &em->event_types, unserialize_elog_type);

  for (i = 0; i < n_ievents; i++)
    unserialize (m, unserialize_elog_ievent, &em->ievent_ring[i]);
}
