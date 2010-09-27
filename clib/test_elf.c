/*
  Copyright (c) 2008 Eliot Dresselhaus

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

#include <clib/elf.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef CLIB_UNIX
#error "unix only"
#endif

static clib_error_t * elf_set_interpreter (elf_main_t * em, char * interp)
{
  elf_segment_t * g;
  elf_section_t * s;
  clib_error_t * error;

  vec_foreach (g, em->segments)
    {
      if (g->header.type == ELF_SEGMENT_INTERP)
	break;
    }

  if (g >= vec_end (em->segments))
    return clib_error_return (0, "interpreter not found");

  if (g->header.memory_size < 1 + strlen (interp))
    return clib_error_return (0, "given interpreter does not fit; must be less than %d bytes (`%s' given)",
			      g->header.memory_size, interp);

  error = elf_get_section_by_start_address (em, g->header.virtual_address, &s);
  if (error)
    return error;

  /* Put in new null terminated string. */
  memset (s->contents, 0, vec_len (s->contents));
  memcpy (s->contents, interp, strlen (interp));

  return 0;
}

static void
delete_dynamic_rpath_entries_from_section (elf_main_t * em, elf_section_t * s)
{
  uword i;

  if (em->first_header.file_class == ELF_64BIT)
    {
      elf64_dynamic_entry_t * e, * old_es, * new_es;
      uword n_del = 0;

      old_es = elf_section_contents (em, s - em->sections, sizeof (e[0]));
      new_es = 0;

      for (i = 0; i < vec_len (old_es); i++)
	{
	  u64 type = elf_swap_u64 (em, old_es[i].type);
	  switch (type)
	    {
	    case ELF_DYNAMIC_ENTRY_RPATH:
	    case ELF_DYNAMIC_ENTRY_RUN_PATH:
	      n_del++;
	      break;

	    default:
	      vec_add1 (new_es, e[0]);
	      break;
	    }
	}

      if (n_del > 0)
	{
	  vec_add2 (new_es, e, n_del);
	  for (i = 0; i < n_del; i++)
	    {
	      e[i].type = elf_swap_u64 (em, ELF_DYNAMIC_ENTRY_END);
	      e[i].data = 0;
	    }

	  /* Replace dynamic section with modified one. */
	  memcpy (s->contents, new_es, vec_bytes (new_es));
	}

      vec_free (old_es);
      vec_free (new_es);
    }
  else
    {
      elf32_dynamic_entry_t * e, * old_es, * new_es;
      uword n_del = 0;

      old_es = elf_section_contents (em, s - em->sections, sizeof (e[0]));
      new_es = 0;

      for (i = 0; i < vec_len (old_es); i++)
	{
	  u32 type = elf_swap_u32 (em, old_es[i].type);
	  switch (type)
	    {
	    case ELF_DYNAMIC_ENTRY_RPATH:
	    case ELF_DYNAMIC_ENTRY_RUN_PATH:
	      n_del++;
	      break;

	    default:
	      vec_add1 (new_es, e[0]);
	      break;
	    }
	}

      if (n_del > 0)
	{
	  vec_add2 (new_es, e, n_del);
	  for (i = 0; i < n_del; i++)
	    {
	      e[i].type = elf_swap_u32 (em, ELF_DYNAMIC_ENTRY_END);
	      e[i].data = 0;
	    }

	  /* Replace dynamic section with modified one. */
	  memcpy (s->contents, new_es, vec_bytes (new_es));
	}

      vec_free (old_es);
      vec_free (new_es);
    }
}

static void elf_delete_dynamic_rpath_entries (elf_main_t * em)
{
  elf_section_t * s;
  elf64_dynamic_entry_t * e;

  vec_foreach (s, em->sections)
    {
      switch (s->header.type)
	{
	case ELF_SECTION_DYNAMIC:
	  delete_dynamic_rpath_entries_from_section (em, s);
	  break;

	default:
	  break;
	}
    }
}

typedef struct {
  elf_main_t elf_main;
  char * input_file;
  char * output_file;
  char * set_interpreter;
  int verbose;
} elf_test_main_t;

int main (int argc, char * argv[])
{
  elf_test_main_t _tm = {0}, * tm = &_tm;
  elf_main_t * em = &tm->elf_main;
  unformat_input_t i;
  clib_error_t * error = 0;

  unformat_init_command_line (&i, argv);
  while (unformat_check_input (&i) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (&i, "in %s", &tm->input_file))
	;
      else if (unformat (&i, "out %s", &tm->output_file))
	;
      else if (unformat (&i, "set-interpreter %s", &tm->set_interpreter))
	;
      else if (unformat (&i, "verbose"))
	tm->verbose = 1;
      else
	{
	  error = unformat_parse_error (&i);
	  goto done;
	}
    }

  if (! tm->input_file)
    clib_error ("no input file");

  error = elf_read_file (em, tm->input_file);
  if (error)
    goto done;

  if (tm->set_interpreter)
    {
      clib_error_t * error = elf_set_interpreter (em, tm->set_interpreter);
      if (error)
	goto done;
      elf_delete_dynamic_rpath_entries (em);
    }

  if (tm->verbose)
    fformat (stdout, "%U", format_elf_main, em);

  if (tm->output_file)
    error = elf_write_file (em, tm->output_file);

  elf_main_free (em);

 done:
  if (error)
    {
      clib_error_report (error);
      return 1;
    }
  else
    return 0;
}
