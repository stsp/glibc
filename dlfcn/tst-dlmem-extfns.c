/* Test for external functions (fdlopen, dlopen_with_offset4) on top of dlmem.
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
#include <sys/stat.h>
#include <support/check.h>

/* Load the shared library from an open fd. */
static void *
fdlopen (int fd, int flags)
{
  off_t len;
  void *addr;
  void *handle;

  len = lseek (fd, 0, SEEK_END);
  lseek (fd, 0, SEEK_SET);
  addr = mmap (NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED)
    return NULL;
  handle = dlmem (addr, len, flags, NULL);
  munmap (addr, len);
  return handle;
}

/* Load the shared library from a container file.
   file   - file name.
   offset - solib offset within a container file.
            Highly recommended to be page-aligned.
   length - solib file length (not a container file length).
   flags  - dlopen() flags. */
void *dlopen_with_offset4 (const char *file, off_t offset, size_t length,
                           int flags)
{
    void *addr;
    void *handle;
    int fd;
    off_t pad_size = (offset & (getpagesize () - 1));
    off_t aligned_offset = offset - pad_size;
    size_t map_length = length + pad_size;

    fd = open (file, O_RDONLY);
    if (fd == -1)
        return NULL;
    addr = mmap (NULL, map_length, PROT_READ, MAP_PRIVATE, fd, aligned_offset);
    close(fd);
    if (addr == MAP_FAILED)
        return NULL;
    if (pad_size)
      {
        /* We need to fix alignment by hands. :-(
           And for that we need a shared mapping. */
        void *addr2 = mmap (NULL, length, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (addr2 == MAP_FAILED)
          {
            munmap (addr, map_length);
            return NULL;
          }
        memcpy (addr2, addr + pad_size, length);
        munmap (addr, map_length);
        addr = addr2;
        map_length = length;
      }
    handle = dlmem (addr, length, flags, NULL);
    munmap (addr, map_length);
    return handle;
}


#define TEST_FUNCTION do_test
extern int do_test (void);

int
do_test (void)
{
  char cmd[256];
  void *handle;
  int (*sym) (void); /* We load ref1 from glreflib1.c.  */
  Dl_info info;
  int rc;
  int fd;
  struct stat sb;
  const unsigned char *unaligned_buf = (const unsigned char *) 1;

  /* First check the reaction to unaligned buf. */
  handle = dlmem (unaligned_buf, 4096, RTLD_NOW, NULL);
  TEST_VERIFY (handle == NULL);
  /* errno is set by dlerror() so needs to print something. */
  printf ("unaligned buf gives %s\n", dlerror ());
  TEST_COMPARE (errno, EINVAL);

  fd = open (BUILDDIR "glreflib1.so", O_RDONLY);
  if (fd == -1)
    error (EXIT_FAILURE, 0, "cannot open: glreflib1.so");
  fstat (fd, &sb);
  handle = fdlopen (fd, RTLD_NOW);
  close (fd);
  if (handle == NULL)
    {
      printf ("fdlopen failed, %s\n", dlerror ());
      exit (EXIT_FAILURE);
    }

  /* Check that the lib is properly mmap()ed, rather than memcpy()ed.
     This may fail on linux kernels <5.13. */
  snprintf (cmd, sizeof(cmd), "grep glreflib1.so /proc/%i/maps", getpid());
  rc = system (cmd);
  TEST_COMPARE (rc, 0);

  sym = dlsym (handle, "ref1");
  if (sym == NULL)
    error (EXIT_FAILURE, 0, "dlsym failed");

  dlclose (handle);

  /* Try to load the solib from container, with the worst, unaligned case. */
  handle = dlopen_with_offset4 (BUILDDIR "glreflib1.img", 512, sb.st_size,
                                RTLD_NOW);

  sym = dlsym (handle, "ref1");
  if (sym == NULL)
    error (EXIT_FAILURE, 0, "dlsym failed");

  memset (&info, 0, sizeof (info));
  rc = dladdr (sym, &info);
  if (rc == 0)
    error (EXIT_FAILURE, 0, "dladdr failed");

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

  /* Handle can be closed only after checking info, as some info fields
     point to memory that is freed by dlclose(). */
  dlclose (handle);
  return 0;
}


#include <support/test-driver.c>
