/* Relocate the shared object loaded by `dlopen' with `RTLD_NORELOCATE'.
   Copyright (C) 1995-2023 Free Software Foundation, Inc.
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
#include <ldsodefs.h>
#include <shlib-compat.h>
#include <stddef.h>

struct dlrelocate_args
{
  /* The arguments to dlrelocate_doit.  */
  void *handle;
};

static void
dlrelocate_doit (void *a)
{
  struct dlrelocate_args *args = (struct dlrelocate_args *) a;

  GLRO (dl_relocate) (args->handle);
}

static int
dlrelocate_implementation (void *handle)
{
  struct dlrelocate_args args;
  args.handle = handle;

  /* Protect against concurrent loads and unloads.  */
  __rtld_lock_lock_recursive (GL(dl_load_lock));

  int result = (_dlerror_run (dlrelocate_doit, &args) ? -1 : 0);

  __rtld_lock_unlock_recursive (GL(dl_load_lock));

  return result;
}

int
__dlrelocate (void *handle)
{
#ifdef SHARED
  if (GLRO (dl_dlfcn_hook) != NULL)
    return GLRO (dl_dlfcn_hook)->dlrelocate (handle);
#endif
  return dlrelocate_implementation (handle);
}
#ifdef SHARED
versioned_symbol (libc, __dlrelocate, dlrelocate, GLIBC_2_38);
#else /* !SHARED */
/* Also used with _dlfcn_hook.  */
weak_alias (__dlrelocate, dlrelocate)
#endif /* !SHARED */
