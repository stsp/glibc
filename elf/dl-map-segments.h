/* Map in a shared object's segments.  Generic version.
   Copyright (C) 1995-2023 Free Software Foundation, Inc.
   Copyright The GNU Toolchain Authors.
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

#include <dl-load.h>

/* Map a segment and align it properly.  */

static __always_inline ElfW(Addr)
_dl_map_segment (ElfW(Addr) mappref, size_t maplength, size_t mapalign)
{
  int err;
  /* MAP_COPY is a special flag combination for solibs. */
  unsigned int map_flags = MAP_ANONYMOUS | MAP_COPY;
  unsigned int prot = PROT_READ | PROT_WRITE;

  if (__glibc_likely (mapalign <= GLRO(dl_pagesize)))
    return (ElfW(Addr)) __mmap ((void *) mappref, maplength, prot,
				map_flags, -1, 0);

  /* If the segment alignment > the page size, allocate enough space to
     ensure that the segment can be properly aligned.  */
  ElfW(Addr) maplen = (maplength >= mapalign
		       ? (maplength + mapalign)
		       : (2 * mapalign));
  ElfW(Addr) map_start = (ElfW(Addr)) __mmap ((void *) mappref, maplen,
					      PROT_NONE,
					      MAP_ANONYMOUS|MAP_PRIVATE,
					      -1, 0);
  if (__glibc_unlikely ((void *) map_start == MAP_FAILED))
    return map_start;

  ElfW(Addr) map_start_aligned = ALIGN_UP (map_start, mapalign);
  err = __mprotect ((void *) map_start_aligned, maplength, prot);
  if (__glibc_unlikely (err))
    {
      __munmap ((void *) map_start, maplen);
      return (ElfW(Addr)) MAP_FAILED;
    }

  /* Unmap the unused regions.  */
  ElfW(Addr) delta = map_start_aligned - map_start;
  if (delta)
    __munmap ((void *) map_start, delta);
  ElfW(Addr) map_end = map_start_aligned + maplength;
  map_end = ALIGN_UP (map_end, GLRO(dl_pagesize));
  delta = map_start + maplen - map_end;
  if (delta)
    __munmap ((void *) map_end, delta);

  return map_start_aligned;
}

/* This implementation assumes (as does the corresponding implementation
   of _dl_unmap_segments, in dl-unmap-segments.h) that shared objects
   are always laid out with all segments contiguous (or with gaps
   between them small enough that it's preferable to reserve all whole
   pages inside the gaps with PROT_NONE mappings rather than permitting
   other use of those parts of the address space).  */

static __always_inline const char *
_dl_map_segments (struct link_map *l, int fd,
                  const ElfW(Ehdr) *header, int type,
                  const struct loadcmd loadcmds[], size_t nloadcmds,
                  const size_t maplength,
                  struct link_map *loader)
{
  const struct loadcmd *c = loadcmds;

  if (__glibc_likely (type == ET_DYN))
    {
      /* This is a position-independent shared object.  We can let the
         kernel map it anywhere it likes, but we must have space for all
         the segments in their specified positions relative to the first.
         So we map the first segment without MAP_FIXED, but with its
         extent increased to cover all the segments.  Then we remove
         access from excess portion, and there is known sufficient space
         there to remap from the later segments.

         As a refinement, sometimes we have an address that we would
         prefer to map such objects at; but this is only a preference,
         the OS can do whatever it likes. */
      ElfW(Addr) mappref
        = (ELF_PREFERRED_ADDRESS (loader, maplength, c->mapstart)
           - MAP_BASE_ADDR (l));

      /* Remember which part of the address space this object uses.  */
      l->l_map_start = _dl_map_segment (mappref, maplength, c->mapalign);
      if (__glibc_unlikely ((void *) l->l_map_start == MAP_FAILED))
        return DL_MAP_SEGMENTS_ERROR_MAP_SEGMENT;

      l->l_map_end = l->l_map_start + maplength;
      l->l_addr = l->l_map_start - c->mapstart;
    }
  else
    {
      /* Remember which part of the address space this object uses.  */
      l->l_map_start = (ElfW(Addr)) __mmap ((caddr_t) l->l_addr + c->mapstart,
                                            maplength, PROT_NONE,
                                            MAP_ANON|MAP_PRIVATE|MAP_FIXED,
                                            -1, 0);
      if (__glibc_unlikely ((void *) l->l_map_start == MAP_FAILED))
        return DL_MAP_SEGMENTS_ERROR_MAP_SEGMENT;
      l->l_map_end = l->l_map_start + maplength;
    }
  /* Reset to 0 later if hole found. */
  l->l_contiguous = 1;

  while (c < &loadcmds[nloadcmds])
    {
      ElfW(Addr) hole_start, hole_size;

      if (c->mapend > c->mapstart
          /* Map the segment contents from the file.  */
          && (__mmap ((void *) (l->l_addr + c->mapstart),
                      c->mapend - c->mapstart, c->prot,
                      MAP_FIXED|MAP_COPY|MAP_FILE,
                      fd, c->mapoff)
              == MAP_FAILED))
        return DL_MAP_SEGMENTS_ERROR_MAP_SEGMENT;

      _dl_postprocess_loadcmd (l, header, c);

      if (c->allocend > c->dataend)
        {
          /* Extra zero pages should appear at the end of this segment,
             after the data mapped from the file.   */
          ElfW(Addr) zero, zeroend, zeropage;
          ElfW(Off) hole_off;

          zero = l->l_addr + c->dataend;
          zeroend = l->l_addr + c->allocend;
          zeropage = ((zero + GLRO(dl_pagesize) - 1)
                      & ~(GLRO(dl_pagesize) - 1));
          hole_start = ALIGN_UP (c->allocend, GLRO(dl_pagesize));
          hole_off = hole_start - c->mapend;
          hole_size = c->maphole - hole_off;

          if (zeroend < zeropage)
            /* All the extra data is in the last page of the segment.
               We can just zero it.  */
            zeropage = zeroend;

          if (zeropage > zero)
            {
              /* Zero the final part of the last page of the segment.  */
              if (__glibc_unlikely ((c->prot & PROT_WRITE) == 0))
                {
                  /* Dag nab it.  */
                  if (__mprotect ((caddr_t) (zero
                                             & ~(GLRO(dl_pagesize) - 1)),
                                  GLRO(dl_pagesize), c->prot|PROT_WRITE) < 0)
                    return DL_MAP_SEGMENTS_ERROR_MPROTECT;
                }
              memset ((void *) zero, '\0', zeropage - zero);
              if (__glibc_unlikely ((c->prot & PROT_WRITE) == 0))
                __mprotect ((caddr_t) (zero & ~(GLRO(dl_pagesize) - 1)),
                            GLRO(dl_pagesize), c->prot);
            }

          if (zeroend > zeropage)
            {
              /* Protect the remaining zero pages.  */
              if (__glibc_unlikely (__mprotect ((caddr_t) zeropage,
                                                zeroend - zeropage,
                                                c->prot) < 0))
                return DL_MAP_SEGMENTS_ERROR_MPROTECT;
            }
        }
      else
        {
          hole_start = c->mapend;
          hole_size = c->maphole;
        }

      if (__glibc_unlikely (c->maphole))
        {
          if (__glibc_likely (type == ET_DYN))
            {
              if (hole_size)
                {
                  if (__mprotect ((caddr_t) (l->l_addr + hole_start),
                                   hole_size, PROT_NONE) < 0)
                    return DL_MAP_SEGMENTS_ERROR_MPROTECT;
                }
            }
          else if (l->l_contiguous)
            {
              l->l_contiguous = 0;
            }
        }

      ++c;
    }

  /* Notify ELF_PREFERRED_ADDRESS that we have to load this one
     fixed.  */
  ELF_FIXED_ADDRESS (loader, c->mapstart);

  return NULL;
}
