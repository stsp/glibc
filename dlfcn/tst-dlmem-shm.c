/* Test for dlmem into shm.
   Copyright (C) 2000-2022 Free Software Foundation, Inc.
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
#include <link.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <support/check.h>

static size_t maplen;

static void *
premap_dlmem (void *mappref, size_t maplength, size_t mapalign, void *cookie)
{
  int fd = * (int *) cookie;
  int prot = PROT_READ | PROT_WRITE;
  int err;

  /* See if we support such parameters. */
  if (mappref || mapalign > 4096)
    return MAP_FAILED;

  fprintf (stderr, "%s\n", __func__);

  err = ftruncate (fd, maplength);
  if (err)
    error (EXIT_FAILURE, 0, "ftruncate() failed");
  maplen = maplength;
  return mmap (NULL, maplength, prot, MAP_SHARED | MAP_FILE
#ifdef MAP_32BIT
                                      | MAP_32BIT
#endif
                                      , fd, 0);
}

#define TEST_FUNCTION do_test
extern int do_test (void);

int
do_test (void)
{
  void *handle;
  void *addr;
  int (*sym) (void); /* We load ref1 from glreflib1.c.  */
  int *bar, *bar2;
  unsigned char *addr2;
  Dl_info info;
  int ret;
  int fd;
  int num;
  off_t len;
  struct link_map *lm;
  const char *shm_name = "/tst-dlmem";
  int shm_fd;
  struct dlmem_args a;

  shm_fd = memfd_create (shm_name, 0);
  if (shm_fd == -1)
    error (EXIT_FAILURE, 0, "shm_open() failed");

  fd = open (BUILDDIR "glreflib1.so", O_RDONLY);
  if (fd == -1)
    error (EXIT_FAILURE, 0, "cannot open: glreflib1.so");
  len = lseek (fd, 0, SEEK_END);
  lseek (fd, 0, SEEK_SET);
  addr = mmap (NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED)
    error (EXIT_FAILURE, 0, "cannot mmap: glreflib1.so");
  a.soname = "glreflib1.so";
  a.flags = DLMEM_DONTREPLACE;
  a.nsid = LM_ID_BASE;
  a.premap = premap_dlmem;
  a.cookie = &shm_fd;
  handle = dlmem (addr, len, RTLD_NOW | RTLD_LOCAL, &a);
  if (handle == NULL)
    error (EXIT_FAILURE, 0, "cannot load: glreflib1.so");
  munmap (addr, len);
  close (fd);
  /* Check if premap was called. */
  TEST_VERIFY (maplen != 0);

  sym = dlsym (handle, "ref1");
  if (sym == NULL)
    error (EXIT_FAILURE, 0, "dlsym failed");

  memset (&info, 0, sizeof (info));
  ret = dladdr (sym, &info);
  if (ret == 0)
    error (EXIT_FAILURE, 0, "dladdr failed");
#ifdef MAP_32BIT
  /* Make sure MAP_32BIT worked. */
  if ((unsigned long) info.dli_fbase >= 0x100000000)
    error (EXIT_FAILURE, 0, "premap audit didn't work");
#endif
  ret = dlinfo (handle, RTLD_DI_LINKMAP, &lm);
  if (ret != 0)
    error (EXIT_FAILURE, 0, "dlinfo failed");

  printf ("info.dli_fname = %p (\"%s\")\n", info.dli_fname, info.dli_fname);
  printf ("info.dli_fbase = %p\n", info.dli_fbase);
  printf ("info.dli_sname = %p (\"%s\")\n", info.dli_sname, info.dli_sname);
  printf ("info.dli_saddr = %p\n", info.dli_saddr);
  printf ("lm->l_addr = %lx\n", lm->l_addr);

  if (info.dli_fname == NULL)
    error (EXIT_FAILURE, 0, "dli_fname is NULL");
  if (info.dli_fbase == NULL)
    error (EXIT_FAILURE, 0, "dli_fbase is NULL");
  if (info.dli_sname == NULL)
    error (EXIT_FAILURE, 0, "dli_sname is NULL");
  if (info.dli_saddr == NULL)
    error (EXIT_FAILURE, 0, "dli_saddr is NULL");

  num = sym ();
  if (num != 42)
    error (EXIT_FAILURE, 0, "bad return from ref1");

  /* Now try symbol duplication. */
  bar = dlsym (handle, "bar");
  if (bar == NULL)
    error (EXIT_FAILURE, 0, "dlsym failed");
  TEST_COMPARE (*bar, 35);
  /* write another value */
#define TEST_BAR_VAL 48
  *bar = TEST_BAR_VAL;

  /* Create second instance of the solib. */
  addr2 = mmap (NULL, maplen, PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_SHARED, shm_fd, 0);
  if (addr2 == MAP_FAILED)
    error (EXIT_FAILURE, 0, "cannot mmap shm\n");
  /* Find our bar symbol duplicate. */
  ret = dladdr (bar, &info);
  if (ret == 0)
    error (EXIT_FAILURE, 0, "dladdr failed");
  bar2 = (int *) (addr2 + (info.dli_saddr - info.dli_fbase));
  /* See if we found the right one. */
  TEST_COMPARE (*bar2, TEST_BAR_VAL);

  munmap (addr2, maplen);
  close (shm_fd);
  dlclose (handle);

  return 0;
}


#include <support/test-driver.c>
