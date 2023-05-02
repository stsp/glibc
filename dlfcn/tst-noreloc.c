/* Tests for RTLD_NORELOCATE flag.
   Copyright (C) 2000-2023 Free Software Foundation, Inc.
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
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <support/check.h>

int ctor_called;

static void move_object (void *handle)
{
  int ret;
  Dl_mapinfo mapinfo;
  void *new_addr;

  ret = dlinfo (handle, RTLD_DI_MAPINFO, &mapinfo);
  TEST_COMPARE (ret, 0);
  TEST_COMPARE (mapinfo.relocated, 0);
  if (mapinfo.map_align != getpagesize ())
    error (EXIT_FAILURE, 0, "unsupported map alignment");
#ifndef MAP_32BIT
#define MAP_32BIT 0
#endif
  new_addr = mmap (NULL, mapinfo.map_length, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
  if (new_addr == MAP_FAILED)
    error (EXIT_FAILURE, 0, "cannot mmap buffer");
  memcpy (new_addr, mapinfo.map_start, mapinfo.map_length);
  ret = dlset_object_base (handle, new_addr);
  TEST_COMPARE (ret, 0);
  munmap (mapinfo.map_start, mapinfo.map_length);
}

static int
do_test (void)
{
  void *handle;
  int (*sym) (void);
  Dl_info info;
  int ret;

  handle = dlopen ("ctorlib1.so", RTLD_NOW | RTLD_NORELOCATE);
  if (handle == NULL)
    error (EXIT_FAILURE, 0, "cannot load: ctorlib1.so");

  TEST_COMPARE (ctor_called, 0);
  sym = dlsym (handle, "ref1");
  if (sym == NULL)
    error (EXIT_FAILURE, 0, "dlsym failed");
  TEST_COMPARE (ctor_called, 1);

  dlclose (handle);
  ctor_called = 0;
  handle = dlopen ("ctorlib1.so", RTLD_NOW | RTLD_NORELOCATE);
  if (handle == NULL)
    error (EXIT_FAILURE, 0, "cannot load: ctorlib1.so");

  TEST_COMPARE (ctor_called, 0);
  ret = dlrelocate (handle);
  TEST_COMPARE (ret, 0);
  /* dlrelocate() called ctors. */
  TEST_COMPARE (ctor_called, 1);
  /* This time should fail. */
  ret = dlrelocate (handle);
  TEST_COMPARE (ret, -1);
  TEST_COMPARE (ctor_called, 1);
  sym = dlsym (handle, "ref1");
  if (sym == NULL)
    error (EXIT_FAILURE, 0, "dlsym failed");
  TEST_COMPARE (ctor_called, 1);

  dlclose (handle);
  ctor_called = 0;
  handle = dlopen ("ctorlib1.so", RTLD_NOW | RTLD_NORELOCATE);
  if (handle == NULL)
    error (EXIT_FAILURE, 0, "cannot load: ctorlib1.so");

  move_object (handle);

  TEST_COMPARE (ctor_called, 0);
  sym = dlsym (handle, "ref1");
  if (sym == NULL)
    error (EXIT_FAILURE, 0, "dlsym failed");
  TEST_COMPARE (ctor_called, 1);

  memset (&info, 0, sizeof (info));
  ret = dladdr (sym, &info);
  if (ret == 0)
    error (EXIT_FAILURE, 0, "dladdr failed");
#if MAP_32BIT != 0
  TEST_VERIFY ((uintptr_t) info.dli_fbase < 0x100000000);
#endif

  printf ("ret = %d\n", ret);
  printf ("info.dli_fname = %p (\"%s\")\n", info.dli_fname, info.dli_fname);
  printf ("info.dli_fbase = %p\n", info.dli_fbase);
  printf ("info.dli_sname = %p (\"%s\")\n", info.dli_sname, info.dli_sname);
  printf ("info.dli_saddr = %p\n", info.dli_saddr);

  if (info.dli_fname == NULL)
    error (EXIT_FAILURE, 0, "dli_fname is NULL");
  if (info.dli_fbase == NULL)
    error (EXIT_FAILURE, 0, "dli_fbase is NULL");
  if (info.dli_sname == NULL)
    error (EXIT_FAILURE, 0, "dli_sname is NULL");
  if (info.dli_saddr == NULL)
    error (EXIT_FAILURE, 0, "dli_saddr is NULL");

  dlclose (handle);

  return 0;
}


#include <support/test-driver.c>
