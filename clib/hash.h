/*
  Copyright (c) 2001-2005 Eliot Dresselhaus

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

#ifndef included_hash_h
#define included_hash_h

#include <clib/error.h>
#include <clib/format.h>
#include <clib/vec.h>
#include <clib/vector.h>

struct hash_header;

typedef uword (hash_key_sum_function_t)
  (struct hash_header *, uword key);
typedef uword (hash_key_equal_function_t)
  (struct hash_header *, uword key1, uword key2);

/* Vec. header for hash tables. */
typedef struct hash_header {
  /* Number of elements in table. */
  uword elts;

  /* Flags. */
  u32 flags;

  /* Flag, set if user does not want table to auto-resize when
     sufficiently full. */
#define HASH_FLAG_NO_AUTO_GROW		(1 << 0)
#define HASH_FLAG_NO_AUTO_SHRINK	(1 << 1)

  /* Set when hash_next is in the process of iterating through this hash table. */
#define HASH_FLAG_HASH_NEXT_IN_PROGRESS (1 << 2)

  u32 log2_pair_size;

  /* Function to compute the "sum" of a hash key.
     Hash function is this sum modulo the prime size of
     the hash table (vec_len (v)). */
  hash_key_sum_function_t * key_sum;

  /* Special values for key_sum "function". */
#define KEY_FUNC_NONE		(0) /* sum = key */
#define KEY_FUNC_POINTER_UWORD	(1) /* sum = *(uword *) key */
#define KEY_FUNC_POINTER_U32	(2) /* sum = *(u32 *) key */
#define KEY_FUNC_STRING         (3) /* sum = string_key_sum, etc. */

  hash_key_equal_function_t * key_equal;

  /* Hook for user's data.  Used to parameterize sum/equal functions. */
  any user;

  format_function_t * format_pair;
  void * format_pair_arg;

  /* Bit i is set if pair i is a user object (as opposed to being
     either zero or an indirect array of pairs). */
  uword is_user[0];
} hash_t;

always_inline uword hash_header_bytes (void * v)
{
  hash_t * h;
  uword is_user_bytes = (sizeof (h->is_user[0]) * vec_len (v)) / BITS (h->is_user[0]);
  return sizeof (h[0]) + is_user_bytes;
}

always_inline hash_t * hash_header (void * v)
{ return vec_header (v, hash_header_bytes (v)); }

always_inline uword hash_elts (void * v)
{
  hash_t * h = hash_header (v);
  return v ? h->elts : 0;
}

always_inline uword hash_capacity (void * v)
{ return vec_len (v); }

always_inline uword hash_is_user (void * v, uword i)
{
  hash_t * h = hash_header (v);
  uword i0 = i / BITS(h->is_user[0]);
  uword i1 = i % BITS(h->is_user[0]);
  return (h->is_user[i0] & ((uword) 1 << i1)) != 0;
}

always_inline void
hash_set_pair_format (void * v,
		      format_function_t * format_pair,
		      void * format_pair_arg)
{
  hash_t * h = hash_header (v);
  h->format_pair = format_pair;
  h->format_pair_arg = format_pair_arg;
}

always_inline void hash_set_flags (void * v, uword flags)
{ hash_header (v)->flags |= flags; }

/* Key value pairs. */
typedef struct {
  uword key;

  /* The value length is 2^log2_pair_size - 1. */
  uword value[0];
} hash_pair_t;

/* If log2_pair_size > 0 we overload hash pairs
   with indirect pairs for buckets with more than one
   pair. */
typedef struct {
  hash_pair_t * pairs;
  u8 pad[sizeof(uword) - sizeof (hash_pair_t *)];
  uword alloc_len;
} hash_pair_indirect_t;

typedef union {
    hash_pair_t direct;
    hash_pair_indirect_t indirect;
} hash_pair_union_t;

#define LOG2_ALLOC_BITS (5)
#define PAIR_BITS	(BITS (uword) - LOG2_ALLOC_BITS)

/* Log2 number of bytes allocated in pairs array. */
always_inline uword indirect_pair_get_log2_bytes (hash_pair_indirect_t * p)
{ return p->alloc_len >> PAIR_BITS; }

always_inline uword
indirect_pair_get_len (hash_pair_indirect_t * p)
{
  if (! p->pairs)
    return 0;
  else
    return p->alloc_len & (((uword) 1 << PAIR_BITS) - 1);
}

always_inline void
indirect_pair_set (hash_pair_indirect_t * p,
			       uword log2_alloc,
			       uword len)
{
  ASSERT (len < ((uword) 1 << PAIR_BITS));
  ASSERT (log2_alloc < ((uword) 1 << LOG2_ALLOC_BITS));
  p->alloc_len = (log2_alloc << PAIR_BITS) | len;
}

uword * _hash_get (void * v, uword key);
hash_pair_t * _hash_get_pair (void * v, uword key);
void *  _hash_unset (void * v, uword key, void * old_value);
void * _hash_set3 (void * v, uword key, void * value, void * old_value);
void * hash_resize (void * old, uword new_size);
void * hash_dup (void * old);

uword hash_bytes (void * v);

#define hash_set3(h,key,value,old_value)				\
({									\
  uword _v = (uword) (value);						\
  (h) = _hash_set3 ((h), (uword) (key), (void *) &_v, (old_value));	\
})

#define hash_get(h,key)		_hash_get ((h), (uword) (key))
#define hash_get_pair(h,key)	_hash_get_pair ((h), (uword) (key))
#define hash_set(h,key,value)	hash_set3(h,key,value,0)
#define hash_set1(h,key)	(h) = _hash_set3(h,(uword) (key),0,0)
#define hash_unset(h,key)	((h) = _hash_unset ((h), (uword) (key),0))
#define hash_unset3(h,key,old_value) ((h) = _hash_unset ((h), (uword) (key), (void *) (old_value)))

/* get/set/unset for pointer keys. */
#define hash_get_mem(h,key)	_hash_get ((h), pointer_to_uword (key))
#define hash_get_pair_mem(h,key) _hash_get_pair ((h), pointer_to_uword (key))
#define hash_set_mem(h,key,value) hash_set3 (h, pointer_to_uword (key), (value), 0)
#define hash_set1_mem(h,key)     hash_set3 ((h), pointer_to_uword (key), 0, 0)
#define hash_unset_mem(h,key)    ((h) = _hash_unset ((h), pointer_to_uword (key),0))

extern void * _hash_free (void * v);
#define hash_free(h) (h) = _hash_free ((h))

clib_error_t * hash_validate (void * v);

always_inline uword hash_value_bytes (hash_t * h)
{
  hash_pair_t * p;
  return (sizeof (p->value[0]) << h->log2_pair_size) - sizeof (p->key);
}

always_inline uword hash_pair_log2_bytes (hash_t * h)
{
  uword log2_bytes = h->log2_pair_size;
  ASSERT (BITS (hash_pair_t) == 32
	  || BITS (hash_pair_t) == 64);
  if (BITS (hash_pair_t) == 32)
    log2_bytes += 2;
  else if (BITS (hash_pair_t) == 64)
    log2_bytes += 3;
  return log2_bytes;
}

always_inline uword hash_pair_bytes (hash_t * h)
{ return (uword) 1 << hash_pair_log2_bytes (h); }

always_inline void * hash_forward1 (hash_t * h, void * v)
{ return (u8 *) v + hash_pair_bytes (h); }

always_inline void * hash_forward (hash_t * h, void * v, uword n)
{ return (u8 *) v + ((n * sizeof (hash_pair_t)) << h->log2_pair_size); }

/* Iterate over hash pairs. */
#define hash_foreach_pair(p,v,body)                                         \
do {                                                                        \
 __label__ _hash_foreach_done;                                              \
  hash_t * _h = hash_header (v);                                            \
  hash_pair_union_t * _p;                                                   \
  hash_pair_t * _q, * _q_end;                                               \
  uword _i, _i1, _id, _pair_increment;                                      \
                                                                            \
  _p = (hash_pair_union_t *) (v);                                           \
  _i = 0;                                                                   \
  _pair_increment = 1;                                                      \
  if ((v))                                                                  \
      _pair_increment = 1 << _h->log2_pair_size;                            \
  while (_i < hash_capacity (v))                                            \
    {                                                                       \
      _id = _h->is_user[_i / BITS (_h->is_user[0])];                        \
      _i1 = _i + BITS (_h->is_user[0]);                                     \
                                                                            \
      do {                                                                  \
        if (_id & 1)                                                        \
          {                                                                 \
            _q = &_p->direct;                                               \
            _q_end = _q + _pair_increment;                                  \
          }                                                                 \
        else                                                                \
          {                                                                 \
            hash_pair_indirect_t * _pi = &_p->indirect;                     \
            _q = _pi->pairs;                                                \
            if (_h->log2_pair_size > 0)                                     \
              _q_end = hash_forward (_h, _q, indirect_pair_get_len (_pi));  \
            else                                                            \
              _q_end = vec_end (_q);                                        \
          }                                                                 \
                                                                            \
        /* Loop through all elements in bucket.                             \
           Bucket may have 0 1 or more (indirect case) pairs. */            \
        while (_q < _q_end)                                                 \
          {                                                                 \
            uword _break_in_body = 1;                                       \
            (p) = _q;                                                       \
            do {                                                            \
              body;                                                         \
              _break_in_body = 0;                                           \
            } while (0);                                                    \
            if (_break_in_body)                                             \
              goto _hash_foreach_done;                                      \
            _q += _pair_increment;                                          \
          }                                                                 \
                                                                            \
        _p = (hash_pair_union_t *) (&_p->direct + _pair_increment);         \
        _id = _id / 2;                                                      \
        _i++;                                                               \
      } while (_i < _i1);                                                   \
    }                                                                       \
  _hash_foreach_done:                                                       \
  /* Be silent Mr. Compiler-Warning. */                                     \
  ;                                                                         \
 } while (0)

/* Iteratate over key/value pairs. */
#define hash_foreach(key_var,value_var,h,body)			\
do {								\
  hash_pair_t * _r;						\
  hash_foreach_pair (_r, (h), {					\
    (key_var) = (__typeof__ (key_var)) _r->key;			\
    (value_var) = (__typeof__ (value_var)) _r->value[0];	\
    do { body; } while (0);					\
  });								\
} while (0)

/* As above but for pointer keys. */
#define hash_foreach_mem(key_var,value_var,h,body)			\
do {									\
  hash_pair_t * _r;							\
  hash_foreach_pair (_r, (h), {						\
    (key_var) = (__typeof__ (key_var)) uword_to_pointer (_r->key, void *); \
    (value_var) = (__typeof__ (value_var)) _r->value[0];		\
    do { body; } while (0);						\
  });									\
} while (0)

/* Support for iteration through hash table. */

/* This struct saves iteration state for hash_next.
   None of these fields are meant to be visible to the user.
   Hence, the cryptic short-hand names. */
typedef struct {
  uword i, j, f;
} hash_next_t;

hash_pair_t * hash_next (void * v, hash_next_t * hn);

void * _hash_create (uword elts, hash_t * h);

always_inline void hash_set_value_bytes (hash_t * h, uword value_bytes)
{
  hash_pair_t * p;
  h->log2_pair_size =
      max_log2 ((sizeof (p->key) + value_bytes + sizeof (p->key) - 1) / sizeof (p->key));
}

#define hash_create2(_elts,_user,_value_bytes,               \
                     _key_sum,_key_equal,                    \
                     _format_pair,_format_pair_arg)          \
({							     \
  hash_t _h;						     \
  memset (&_h, 0, sizeof (_h));				     \
  _h.user = (_user);				             \
  _h.key_sum   = (hash_key_sum_function_t *) (_key_sum);     \
  _h.key_equal = (_key_equal);				     \
  hash_set_value_bytes (&_h, (_value_bytes));		     \
  _h.format_pair = (format_function_t *) (_format_pair);     \
  _h.format_pair_arg = (_format_pair_arg);                   \
  _hash_create ((_elts), &_h);				     \
})

/* Hash function based on that of Bob Jenkins (bob_jenkins@compuserve.com).
   Thanks, Bob. */
#define hash_mix_step(a,b,c,s0,s1,s2)		\
do {						\
  (a) -= (b) + (c); (a) ^= (c) >> (s0);		\
  (b) -= (c) + (a); (b) ^= (a) << (s1);		\
  (c) -= (a) + (b); (c) ^= (b) >> (s2);		\
} while (0)

#define hash_mix32_step_1(a,b,c) hash_mix_step(a,b,c,13,8,13)
#define hash_mix32_step_2(a,b,c) hash_mix_step(a,b,c,12,16,5)
#define hash_mix32_step_3(a,b,c) hash_mix_step(a,b,c,3,10,15)

#define hash_mix64_step_1(a,b,c) hash_mix_step(a,b,c,43,9,8)
#define hash_mix64_step_2(a,b,c) hash_mix_step(a,b,c,38,23,5)
#define hash_mix64_step_3(a,b,c) hash_mix_step(a,b,c,35,49,11)
#define hash_mix64_step_4(a,b,c) hash_mix_step(a,b,c,12,18,22)

/* Hash function based on that of Bob Jenkins (bob_jenkins@compuserve.com).
   Thanks, Bob. */
#define hash_mix64(a0,b0,c0)			\
do {						\
  hash_mix64_step_1 (a0, b0, c0);		\
  hash_mix64_step_2 (a0, b0, c0);		\
  hash_mix64_step_3 (a0, b0, c0);		\
  hash_mix64_step_4 (a0, b0, c0);		\
} while (0)					\

#define hash_mix32(a0,b0,c0)			\
do {						\
  hash_mix32_step_1 (a0, b0, c0);		\
  hash_mix32_step_2 (a0, b0, c0);		\
  hash_mix32_step_3 (a0, b0, c0);		\
} while (0)					\

/* Finalize from Bob Jenkins lookup3.c */

always_inline uword
hash32_rotate_left (u32 x, u32 i)
{ return (x << i) | (x >> (BITS (i) - i)); }

#define hash_v3_mix32(a,b,c)					\
do {								\
  (a) -= (c); (a) ^= hash32_rotate_left ((c), 4); (c) += (b);	\
  (b) -= (a); (b) ^= hash32_rotate_left ((a), 6); (a) += (c);	\
  (c) -= (b); (c) ^= hash32_rotate_left ((b), 8); (b) += (a);	\
  (a) -= (c); (a) ^= hash32_rotate_left ((c),16); (c) += (b);	\
  (b) -= (a); (b) ^= hash32_rotate_left ((a),19); (a) += (c);	\
  (c) -= (b); (c) ^= hash32_rotate_left ((b), 4); (b) += (a);	\
} while (0)

#define hash_v3_finalize32(a,b,c)			\
do {							\
  (c) ^= (b); (c) -= hash32_rotate_left ((b), 14);	\
  (a) ^= (c); (a) -= hash32_rotate_left ((c), 11);	\
  (b) ^= (a); (b) -= hash32_rotate_left ((a), 25);	\
  (c) ^= (b); (c) -= hash32_rotate_left ((b), 16);	\
  (a) ^= (c); (a) -= hash32_rotate_left ((c),  4);	\
  (b) ^= (a); (b) -= hash32_rotate_left ((a), 14);	\
  (c) ^= (b); (c) -= hash32_rotate_left ((b), 24);	\
} while (0)

/* Vector v3 mixing/finalize. */
#define hash_v3_mix_step_1_u32x(a,b,c)				\
do {								\
  (a) -= (c); (a) ^= u32x_irotate_left ((c), 4); (c) += (b);	\
  (b) -= (a); (b) ^= u32x_irotate_left ((a), 6); (a) += (c);	\
  (c) -= (b); (c) ^= u32x_irotate_left ((b), 8); (b) += (a);	\
} while (0)

#define hash_v3_mix_step_2_u32x(a,b,c)				\
do {								\
  (a) -= (c); (a) ^= u32x_irotate_left ((c),16); (c) += (b);	\
  (b) -= (a); (b) ^= u32x_irotate_left ((a),19); (a) += (c);	\
  (c) -= (b); (c) ^= u32x_irotate_left ((b), 4); (b) += (a);	\
} while (0)

#define hash_v3_finalize_step_1_u32x(a,b,c)		\
do {							\
  (c) ^= (b); (c) -= u32x_irotate_left ((b), 14);	\
  (a) ^= (c); (a) -= u32x_irotate_left ((c), 11);	\
  (b) ^= (a); (b) -= u32x_irotate_left ((a), 25);	\
} while (0)

#define hash_v3_finalize_step_2_u32x(a,b,c)		\
do {							\
  (c) ^= (b); (c) -= u32x_irotate_left ((b), 16);	\
  (a) ^= (c); (a) -= u32x_irotate_left ((c),  4);	\
  (b) ^= (a); (b) -= u32x_irotate_left ((a), 14);	\
  (c) ^= (b); (c) -= u32x_irotate_left ((b), 24);	\
} while (0)

#define hash_v3_mix_u32x(a,b,c)			\
do {						\
  hash_v3_mix_step_1_u32x(a,b,c);		\
  hash_v3_mix_step_2_u32x(a,b,c);		\
} while (0)

#define hash_v3_finalize_u32x(a,b,c)		\
do {						\
  hash_v3_finalize_step_1_u32x(a,b,c);		\
  hash_v3_finalize_step_2_u32x(a,b,c);		\
} while (0)

extern u64 hash_memory64 (void * p, word n_bytes, u64 state);
extern u32 hash_memory32 (void * p, word n_bytes, u32 state);
extern uword hash_memory (void * p, word n_bytes, uword state);

extern uword mem_key_sum (hash_t * h, uword key);
extern uword mem_key_equal (hash_t * h, uword key1, uword key2);

#define hash_create_mem(elts,key_bytes,value_bytes)	\
  hash_create2((elts),(key_bytes),(value_bytes),mem_key_sum,mem_key_equal,0,0)

extern uword vec_key_sum (hash_t * h, uword key);
extern uword vec_key_equal (hash_t * h, uword key1, uword key2);
extern u8 * vec_key_format_pair (u8 * s, va_list * args);

#define hash_create_vec(elts,key_bytes,value_bytes)	\
  hash_create2((elts),(key_bytes),(value_bytes),\
               vec_key_sum,vec_key_equal,vec_key_format_pair,0)

extern uword string_key_sum (hash_t * h, uword key);
extern uword string_key_equal (hash_t * h, uword key1, uword key2);
extern u8 * string_key_format_pair (u8 * s, va_list * args);

#define hash_create_string(elts,value_bytes)                    \
  hash_create2((elts),0,(value_bytes),                          \
               (hash_key_sum_function_t *) KEY_FUNC_STRING,     \
               (hash_key_equal_function_t *)KEY_FUNC_STRING,    \
               0, 0)

#define hash_create(elts,value_bytes)				\
  hash_create2((elts),0,(value_bytes),				\
               (hash_key_sum_function_t *) KEY_FUNC_NONE,	\
               (hash_key_equal_function_t *) KEY_FUNC_NONE,	\
               0,0)

#define hash_create_uword(elts,value_bytes)				\
  hash_create2((elts),0,(value_bytes),					\
               (hash_key_sum_function_t *) KEY_FUNC_POINTER_UWORD,	\
               (hash_key_equal_function_t *) KEY_FUNC_POINTER_UWORD,	\
               0,0)

#define hash_create_u32(elts,value_bytes)				\
  hash_create2((elts),0,(value_bytes),					\
               (hash_key_sum_function_t *) KEY_FUNC_POINTER_U32,	\
               (hash_key_equal_function_t *) KEY_FUNC_POINTER_U32,	\
               0,0)

u8 * format_hash (u8 * s, va_list * va);

/* Looks up input in hash table indexed by either vec string or
   c string (null terminated). */
unformat_function_t unformat_hash_vec_string;
unformat_function_t unformat_hash_string;

/* Main test routine. */ 
int test_hash_main (unformat_input_t * input);

#endif /* included_hash_h */
