/*
 * File:
 *   atomic.h
 * Author(s):
 *   Pascal Felber <pascal.felber@unine.ch>
 *   Patrick Marlier <patrick.marlier@unine.ch>
 * Description:
 *   Atomic operations.
 *
 * Copyright (c) 2007-2014.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This program has a dual license and can also be distributed
 * under the terms of the MIT license.
 */

#ifndef _ATOMIC_H_
# define _ATOMIC_H_

# define ATOMIC_GCC                      0
# define ATOMIC_LIBATOMIC                1
# define ATOMIC_C11                      2

# ifndef ATOMIC_MECHANISM
#  define ATOMIC_MECHANISM               ATOMIC_C11
# endif /* ATOMIC_MECHANISM */

# if ATOMIC_MECHANISM == ATOMIC_GCC
typedef size_t atomic_t;
#  ifdef __INTEL_COMPILER
#   define ATOMIC_CB                     __memory_barrier()
#  else /* ! __INTEL_COMPILER, assuming __GNUC__ */
#   define ATOMIC_CB                     __asm__ __volatile__("": : :"memory")
#  endif /* ! __INTEL_COMPILER */
#  ifdef SAFE
/*
   Note: __sync_ is available for GCC 4.2+ and ICC 11.1+
   But these definitions are not 100% safe:
    * need 'a' to be volatile
    * no fence for read/store proposed (only full fence)
   C11 and C++11 also propose atomic operations.
*/
#   define ATOMIC_CAS_FULL(a, e, v)      (__sync_bool_compare_and_swap(a, e, v))
#   define ATOMIC_FETCH_INC_FULL(a)      (__sync_fetch_and_add(a, 1))
#   define ATOMIC_FETCH_DEC_FULL(a)      (__sync_fetch_and_add(a, -1))
#   define ATOMIC_FETCH_ADD_FULL(a, v)   (__sync_fetch_and_add(a, v))
#   define ATOMIC_FETCH_SUB_FULL(a, v)   (__sync_fetch_and_sub(a, v))
#   define ATOMIC_LOAD_ACQ(a)            (*(a))
#   define ATOMIC_LOAD(a)                (*(a))
#   define ATOMIC_STORE_REL(a, v)        (*(a) = (v))
#   define ATOMIC_STORE(a, v)            (*(a) = (v))
#   define ATOMIC_MB_READ                /* Nothing */
#   define ATOMIC_MB_WRITE               /* Nothing */
#   define ATOMIC_MB_FULL                __sync_synchronize()
#  else
/* Use only for testing purposes (single thread benchmarks) */
#   define ATOMIC_CAS_FULL(a, e, v)      (*(a) = (v), 1)
#   define ATOMIC_FETCH_INC_FULL(a)      ((*(a))++)
#   define ATOMIC_FETCH_DEC_FULL(a)      ((*(a))--)
#   define ATOMIC_FETCH_ADD_FULL(a, v)   ((*(a)) += (v))
#   define ATOMIC_LOAD_ACQ(a)            (*(a))
#   define ATOMIC_LOAD(a)                (*(a))
#   define ATOMIC_STORE_REL(a, v)        (*(a) = (v))
#   define ATOMIC_STORE(a, v)            (*(a) = (v))
#   define ATOMIC_MB_READ                /* Nothing */
#   define ATOMIC_MB_WRITE               /* Nothing */
#   define ATOMIC_MB_FULL                /* Nothing */
#  endif /* SAFE */
# elif ATOMIC_MECHANISM == ATOMIC_LIBATOMIC
/* NOTE: enable fence instructions for i386 and amd64 but the mfence instructions seems costly. */
/* # define AO_USE_PENTIUM4_INSTRS */
#  include <atomic_ops.h>
typedef AO_t atomic_t;
#  define ATOMIC_CB                     AO_compiler_barrier()
#  define ATOMIC_CAS_FULL(a, e, v)      (AO_compare_and_swap_full((a), (e), (v)))
#  define ATOMIC_FETCH_INC_FULL(a)      (AO_fetch_and_add1_full((a)))
#  define ATOMIC_FETCH_DEC_FULL(a)      (AO_fetch_and_sub1_full((a)))
#  define ATOMIC_FETCH_ADD_FULL(a, v)   (AO_fetch_and_add_full((a), (v)))
#  define ATOMIC_FETCH_SUB_FULL(a, v)   (AO_fetch_and_sub_full((a), (v)))
#  ifdef SAFE
#   define ATOMIC_LOAD_ACQ(a)           (AO_load_full((a)))
#   define ATOMIC_LOAD(a)               (AO_load_full((a)))
#   define ATOMIC_STORE_REL(a, v)       (AO_store_full((a), (v)))
#   define ATOMIC_STORE(a, v)           (AO_store_full(((a), (v)))
#   define ATOMIC_MB_READ               AO_nop_full()
#   define ATOMIC_MB_WRITE              AO_nop_full()
#   define ATOMIC_MB_FULL               AO_nop_full()
#  else /* ! SAFE */
#   define ATOMIC_LOAD_ACQ(a)           (AO_load_acquire_read((a)))
#   define ATOMIC_LOAD(a)               (*(a))
#   define ATOMIC_STORE_REL(a, v)       (AO_store_release((a), (v)))
#   define ATOMIC_STORE(a, v)           (*(a) = (v))
#   define ATOMIC_MB_READ               AO_nop_read()
#   define ATOMIC_MB_WRITE              AO_nop_write()
#   define ATOMIC_MB_FULL               AO_nop_full()
#  endif /* ! SAFE */
# elif ATOMIC_MECHANISM == ATOMIC_C11
#  include <stdatomic.h>
typedef size_t atomic_t;
#  define ATOMIC_CB                     __asm__ __volatile__("": : :"memory")
#  define ATOMIC_CAS_FULL(a, e, v)      (atomic_compare_exchange_strong((a), &(e), (v)))
#  define ATOMIC_FETCH_INC_FULL(a)      (atomic_fetch_add((a), 1))
#  define ATOMIC_FETCH_DEC_FULL(a)      (atomic_fetch_sub((a), 1))
#  define ATOMIC_FETCH_ADD_FULL(a, v)   (atomic_fetch_add((a), (v)))
#  define ATOMIC_FETCH_SUB_FULL(a, v)   (atomic_fetch_sub((a), (v)))
#  define ATOMIC_LOAD_ACQ(a)            (atomic_load_explicit((a), memory_order_acquire))
#  define ATOMIC_LOAD(a)                (atomic_load((a)))
#  define ATOMIC_STORE_REL(a, v)        (atomic_store_explicit((a), (v), memory_order_release))
#  define ATOMIC_STORE(a, v)            (atomic_store((a), (v)))
#  define ATOMIC_MB_READ                atomic_thread_fence(memory_order_acquire)
#  define ATOMIC_MB_WRITE               atomic_thread_fence(memory_order_release)
#  define ATOMIC_MB_FULL                atomic_thread_fence(memory_order_seq_cst)
# else
#  error "Unsupported atomic memory access mechanism!"
# endif /*  */

#endif /* _ATOMIC_H_ */
