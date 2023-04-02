/* Load a shared object from memory.
   Copyright (C) 1995-2022 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#include <dlfcn.h>
#include <libintl.h>
#include <stddef.h>
#include <unistd.h>
#include <ldsodefs.h>
#include <shlib-compat.h>

struct _dlmem_args
{
  /* The arguments for dlmem_doit.  */
  const unsigned char *buffer;
  size_t size;
  int mode;
  struct dlmem_args *args;
  /* The return value of dlmem_doit.  */
  void *new;
  /* Address of the caller.  */
  const void *caller;
};

static void
dlmem_doit (void *a)
{
  struct _dlmem_args *args = (struct _dlmem_args *) a;
  const struct dlmem_args *dlm_args = args->args;

  if (args->mode & ~(RTLD_BINDING_MASK | RTLD_NOLOAD | RTLD_DEEPBIND
		     | RTLD_GLOBAL | RTLD_LOCAL | RTLD_NODELETE
		     | __RTLD_SPROF))
    _dl_signal_error (EINVAL, NULL, NULL, _("invalid mode parameter"));

  /* Unaligned buffer is only permitted when DLMEM_GENBUF_SRC flag set. */
  if (((uintptr_t) args->buffer & (GLRO(dl_pagesize) - 1))
      && (!dlm_args || !(dlm_args->flags & DLMEM_GENBUF_SRC)))
    _dl_signal_error (EINVAL, NULL, NULL, _("buffer not aligned"));

  args->new = GLRO(dl_mem) (args->buffer, args->size,
			    args->mode | __RTLD_DLOPEN,
			    args->args,
			    args->caller,
			    __libc_argc, __libc_argv, __environ);
}


static void *
dlmem_implementation (const unsigned char *buffer, size_t size, int mode,
		      struct dlmem_args *dlm_args, void *dl_caller)
{
  struct _dlmem_args args;
  args.buffer = buffer;
  args.size = size;
  args.mode = mode;
  args.args = dlm_args;
  args.caller = dl_caller;

  return _dlerror_run (dlmem_doit, &args) ? NULL : args.new;
}

void *
___dlmem (const unsigned char *buffer, size_t size, int mode,
	  struct dlmem_args *dlm_args)
{
  return dlmem_implementation (buffer, size, mode, dlm_args,
				 RETURN_ADDRESS (0));
}

#ifdef SHARED
versioned_symbol (libc, ___dlmem, dlmem, GLIBC_2_38);
#else /* !SHARED */
weak_alias (___dlmem, dlmem)
static_link_warning (dlmem)
#endif /* !SHARED */
