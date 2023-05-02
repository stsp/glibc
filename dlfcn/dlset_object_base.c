/* Set the new base address for shared object loaded by `dlopen' with
   `RTLD_NORELOCATE' and moved by the user.
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
#include <array_length.h>

static void
adjust_dyn_info (ElfW(Dyn) **info, ptrdiff_t delta)
{
# define ADJUST_DYN_INFO(tag) \
      do								      \
	{								      \
	  if (info[tag] != NULL)					      \
	  info[tag]->d_un.d_ptr += delta;				      \
	}								      \
      while (0)

      ADJUST_DYN_INFO (DT_HASH);
      ADJUST_DYN_INFO (DT_PLTGOT);
      ADJUST_DYN_INFO (DT_STRTAB);
      ADJUST_DYN_INFO (DT_SYMTAB);
      ADJUST_DYN_INFO (DT_RELR);
      ADJUST_DYN_INFO (DT_JMPREL);
      ADJUST_DYN_INFO (VERSYMIDX (DT_VERSYM));
      ADJUST_DYN_INFO (ADDRIDX (DT_GNU_HASH));
# undef ADJUST_DYN_INFO

      /* DT_RELA/DT_REL are mandatory.  But they may have zero value if
	 there is DT_RELR.  Don't relocate them if they are zero.  */
# define ADJUST_DYN_INFO(tag) \
      do								      \
	if (info[tag] != NULL && info[tag]->d_un.d_ptr != 0)		      \
         info[tag]->d_un.d_ptr += delta;				      \
      while (0)

      ADJUST_DYN_INFO (DT_RELA);
      ADJUST_DYN_INFO (DT_REL);
# undef ADJUST_DYN_INFO
}

static int
dlset_object_base_implementation (void *handle, void *base)
{
  struct link_map *l = handle;
  int result = 0;

  /* Protect against concurrent loads and unloads.  */
  __rtld_lock_lock_recursive (GL(dl_load_lock));

  if (l->l_relocated)
    result = -1;
  else
    {
      ptrdiff_t delta = (uintptr_t) base - l->l_map_start;
      if (delta)
        {
          int i;

          l->l_map_start += delta;
          l->l_map_end += delta;
          l->l_addr += delta;
          l->l_text_end += delta;
          l->l_entry += delta;
          if (!l->l_phdr_allocated)
            l->l_phdr = (__typeof (l->l_phdr)) (((uintptr_t) l->l_phdr)
                                                             + delta);
          l->l_ld = (__typeof (l->l_ld)) (((uintptr_t) l->l_ld) + delta);
          for (i = 0; i < array_length (l->l_info); i++)
            if (l->l_info[i])
              l->l_info[i] = (__typeof (l->l_info[i])) (((uintptr_t)
                                                        l->l_info[i])
                                                        + delta);
          l->l_versyms = (__typeof (l->l_versyms)) (((uintptr_t) l->l_versyms)
                                                                 + delta);
          l->l_gnu_bitmask = (__typeof (l->l_gnu_bitmask))
                                       (((uintptr_t) l->l_gnu_bitmask)
                                       + delta);
          l->l_chain = (__typeof (l->l_chain)) (((uintptr_t) l->l_chain)
                                                             + delta);
          l->l_buckets = (__typeof (l->l_buckets)) (((uintptr_t) l->l_buckets)
                                                                 + delta);
          adjust_dyn_info (l->l_info, delta);
        }
    }

  __rtld_lock_unlock_recursive (GL(dl_load_lock));

  return result;
}

int
__dlset_object_base (void *handle, void *base)
{
#ifdef SHARED
  if (GLRO (dl_dlfcn_hook) != NULL)
    return GLRO (dl_dlfcn_hook)->dlset_object_base (handle, base);
#endif
  return dlset_object_base_implementation (handle, base);
}
#ifdef SHARED
versioned_symbol (libc, __dlset_object_base, dlset_object_base, GLIBC_2_38);
#else /* !SHARED */
/* Also used with _dlfcn_hook.  */
weak_alias (__dlset_object_base, dlset_object_base)
#endif /* !SHARED */
