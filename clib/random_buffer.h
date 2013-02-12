/*
  Copyright (c) 2005 Eliot Dresselhaus

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

#ifndef included_clib_random_buffer_h
#define included_clib_random_buffer_h

#include <clib/clib.h>
#include <clib/random_isaac.h>

typedef struct {
  /* Two parallel ISAAC contexts for speed. */
  isaac_t ctx[2];

  /* Random buffer. */
  uword * buffer;

  /* Cache up to 1 word worth of bytes for random data
     less than one word at a time. */
  uword n_cached_bytes;

  union {
    u8 cached_bytes[sizeof (uword)];
    uword cached_word;
  };
} clib_random_buffer_t;

always_inline void
clib_random_buffer_free (clib_random_buffer_t * b)
{
  vec_free (b->buffer);
}

/* Fill random buffer. */
void clib_random_buffer_fill (clib_random_buffer_t * b, uword n_words);

/* Initialize random buffer. */
void clib_random_buffer_init (clib_random_buffer_t * b, uword seed);

/* Returns word aligned random data, possibly filling buffer. */
always_inline void *
clib_random_buffer_get_data (clib_random_buffer_t * b, uword n_bytes)
{
  uword n_words, i, l;

  l = b->n_cached_bytes;
  if (n_bytes <= l)
    {
      b->n_cached_bytes = l - n_bytes;
      return &b->cached_bytes[l];
    }

  n_words = n_bytes / sizeof (uword);
  if (n_bytes % sizeof (uword))
    n_words++;

  /* Enough random words left? */
  if (PREDICT_FALSE (n_words > vec_len (b->buffer)))
    clib_random_buffer_fill (b, n_words);

  i = vec_len (b->buffer) - n_words;
  _vec_len (b->buffer) = i;

  if (n_bytes < sizeof (uword))
    {
      b->cached_word = b->buffer[i];
      b->n_cached_bytes = sizeof (uword) - n_bytes;
      return b->cached_bytes;
    }
  else
    return b->buffer + i;
}

/* Random data of type T. */
#define clib_random_buffer_get(b,t)					\
({									\
  t * __clib_random_buffer_get = clib_random_buffer_get_data ((b), sizeof (t)); \
  *__clib_random_buffer_get;						\
})

#endif /* included_clib_random_buffer_h */
