/*
 * File:
 *   stm_wbctl.h
 * Author(s):
 *   Pascal Felber <pascal.felber@unine.ch>
 *   Patrick Marlier <patrick.marlier@unine.ch>
 * Description:
 *   STM internal functions for write-back CTL.
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

#ifndef _STM_WBCTL_H_
#define _STM_WBCTL_H_

static INLINE int
stm_wbctl_validate(stm_tx_t *tx)
{
  r_entry_t *r;
  int i;
  stm_version_t l;

  PRINT_DEBUG("==> stm_wbctl_validate(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

  /* Validate reads */
  r = tx->r_set.entries;
  for (i = tx->r_set.nb_entries; i > 0; i--, r++) {
    /* Read lock */
    l = LOCK_READ(r->lock);
restart_no_load:
    /* Unlocked and still the same version? */
    if (LOCK_GET_OWNED(l)) {
      /* Do we own the lock? */
      w_entry_t *w = (w_entry_t *)LOCK_GET_ADDR(l);
      /* Simply check if address falls inside our write set (avoids non-faulting load) */
      if (!(tx->w_set.entries <= w && w < tx->w_set.entries + tx->w_set.nb_entries))
      {
        /* Locked by another transaction: cannot validate */
#ifdef CONFLICT_TRACKING
        if (_tinystm.conflict_cb != NULL) {
# ifdef UNIT_TX
          if (l != LOCK_UNIT) {
# endif /* UNIT_TX */
            /* Call conflict callback */
            _tinystm.conflict_cb(tx, w->tx, STM_RD_VALIDATE, ENTRY_FROM_READ(r), ENTRY_FROM_WRITE(w));
# ifdef UNIT_TX
          }
# endif /* UNIT_TX */
        }
#endif /* CONFLICT_TRACKING */
        return 0;
      }
      /* We own the lock: OK */
      if (w->version != r->version) {
#ifdef CONFLICT_TRACKING
        if (_tinystm.conflict_cb != NULL) {
          /* Call conflict callback */
          _tinystm.conflict_cb(tx, w->tx, STM_RD_VALIDATE, ENTRY_FROM_READ(r), ENTRY_FROM_WRITE(w));
        }
#endif /* CONFLICT_TRACKING */
        /* Other version: cannot validate */
        return 0;
      }
    } else {
      if (LOCK_GET_TIMESTAMP(l) != r->version) {
#ifdef CONFLICT_TRACKING
        if (_tinystm.conflict_cb != NULL) {
          /* Call conflict callback */
          _tinystm.conflict_cb(tx, NULL, STM_RD_VALIDATE, ENTRY_FROM_READ(r), 0);
        }
#endif /* CONFLICT_TRACKING */
        /* Other version: cannot validate */
        return 0;
      }
      /* Same version: OK */
    }
  }
  return 1;
}

/*
 * Extend snapshot range.
 */
static INLINE int
stm_wbctl_extend(stm_tx_t *tx)
{
  stm_version_t now;

  PRINT_DEBUG("==> stm_wbctl_extend(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

#ifdef UNIT_TX
  /* Extension is disabled */
  if (tx->attr.no_extend)
    return 0;
#endif /* UNIT_TX */

  /* Get current time */
  now = GET_CLOCK;
  /* No need to check clock overflow here. The clock can exceed up to MAX_THREADS and it will be reset when the quiescence is reached. */

  /* Try to validate read set */
  if (stm_wbctl_validate(tx)) {
    /* It works: we can extend until now */
    tx->end = now;
    return 1;
  }
  return 0;
}

static INLINE void
stm_wbctl_rollback(stm_tx_t *tx)
{
  w_entry_t *w;

  PRINT_DEBUG("==> stm_wbctl_rollback(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

  assert(IS_ACTIVE(tx->status));
  assert(tx->w_set.nb_entries >= tx->w_set.nb_acquired);

  for (w = tx->w_set.entries + tx->w_set.nb_entries - 1; tx->w_set.nb_acquired > 0; --w) {
    assert(w >= tx->w_set.entries && w < tx->w_set.entries + tx->w_set.nb_entries);
    if (!w->no_drop) {
      assert(tx->w_set.nb_acquired > 0 && tx->w_set.nb_entries >= tx->w_set.nb_acquired);
      if (--tx->w_set.nb_acquired == 0) {
        /* Make sure that all lock releases become visible to other threads */
        LOCK_WRITE_REL(w->lock, LOCK_SET_TIMESTAMP(w->version));
      } else {
        LOCK_WRITE(w->lock, LOCK_SET_TIMESTAMP(w->version));
      }
    }
  }
}

static INLINE stm_word_t
stm_wbctl_read(stm_tx_t *tx, volatile stm_word_t *addr)
{
  const stm_lock_t *lock;
  stm_version_t l, l2;
  stm_word_t value;
  stm_version_t version;
  r_entry_t *r;
  w_entry_t *w;

  PRINT_DEBUG2("==> stm_wbctl_read(t=%p[%lu-%lu],a=%p)\n", tx, (unsigned long)tx->start, (unsigned long)tx->end, addr);

  assert(IS_ACTIVE(tx->status));

  /* Did we previously write the same address? */
  w = stm_has_written(tx, addr);
  if (w != NULL) {
    /* Yes: get value from write set if possible */
    if (w->mask == ~(stm_word_t)0) {
      value = w->value;
      /* No need to add to read set */
      return value;
    }
  }

  /* Get reference to lock */
  lock = GET_LOCK(addr);

  /* Note: we could check for duplicate reads and get value from read set */

  /* Read lock, value, lock */
 restart:
  l = LOCK_READ_ACQ(lock);
 restart_no_load:
  if (LOCK_GET_WRITE(l)) {
    /* Locked */
    /* Do we own the lock? */
    /* Spin while locked (should not last long) */
    goto restart;
  } else {
    /* Not locked */
#ifdef IRREVOCABLE_ENABLED
    /* In irrevocable mode, no need check timestamp nor add entry to read set */
    if (tx->irrevocable) {
      value = ATOMIC_LOAD_ACQ(addr);
      goto return_value;
    }
#endif /* IRREVOCABLE_ENABLED */
    /* Check timestamp */
    version = LOCK_GET_TIMESTAMP(l);
    l2 = LOCK_READ_ACQ(lock);
    if (l != l2) {
      l = l2;
      goto restart_no_load;
    }
    /* Valid version? */
    if (version > tx->end) {
      /* No: try to extend first (except for read-only transactions: no read set) */
      if (tx->attr.read_only || !stm_wbctl_extend(tx)) {
        /* Not much we can do: abort */
        stm_rollback(tx, STM_ABORT_VAL_READ);
        return 0;
      }
    }
    value = ATOMIC_LOAD_ACQ(addr);
    /* Verify that version has not been overwritten (read value has not
     * yet been added to read set and may have not been checked during
     * extend) */
    l2 = LOCK_READ_ACQ(lock);
    if (l != l2) {
      l = l2;
      goto restart_no_load;
    }
    /* Worked: we now have a good version (version <= tx->end) */
  }
  /* We have a good version: add to read set (update transactions) and return value */

  /* Did we previously write the same address? */
  if (w != NULL) {
    value = (value & ~w->mask) | (w->value & w->mask);
    /* Must still add to read set */
  }
#ifdef READ_LOCKED_DATA
 add_to_read_set:
#endif /* READ_LOCKED_DATA */
  stm_add_to_rs(tx, lock, version);
 return_value:
  return value;
}

static INLINE w_entry_t *
stm_wbctl_write(stm_tx_t *tx, volatile stm_word_t *addr, stm_word_t value, stm_word_t mask)
{
  const stm_lock_t *lock;
  stm_version_t l;
  stm_version_t version;
  r_entry_t *r;
  w_entry_t *w;

  PRINT_DEBUG2("==> stm_wbctl_write(t=%p[%lu-%lu],a=%p,d=%p-%lu,m=0x%lx)\n",
               tx, (unsigned long)tx->start, (unsigned long)tx->end, addr, (void *)value, (unsigned long)value, (unsigned long)mask);

  /* Get reference to lock */
  lock = GET_LOCK(addr);

  /* Try to acquire lock */
 restart:
  l = LOCK_READ_ACQ(lock);
 restart_no_load:
  if (LOCK_GET_OWNED(l)) {
    /* Locked */
    /* Spin while locked (should not last long) */
    goto restart;
  }
  /* Not locked */
  w = stm_has_written(tx, addr);
  if (w != NULL) {
    w->value = (w->value & ~mask) | (value & mask);
    w->mask |= mask;
    return w;
  }
  /* Handle write after reads (before CAS) */
  version = LOCK_GET_TIMESTAMP(l);
#ifdef IRREVOCABLE_ENABLED
  /* In irrevocable mode, no need to revalidate */
  if (tx->irrevocable)
    goto acquire_no_check;
#endif /* IRREVOCABLE_ENABLED */
 acquire:
  if (version > tx->end) {
    /* We might have read an older version previously */
#ifdef UNIT_TX
    if (tx->attr.no_extend) {
      stm_rollback(tx, STM_ABORT_VAL_WRITE);
      return NULL;
    }
#endif /* UNIT_TX */
    if ((r = stm_has_read(tx, lock)) != NULL) {
      /* Read version must be older (otherwise, tx->end >= version) */
      /* Not much we can do: abort */
#ifdef CONFLICT_TRACKING
      if (_tinystm.conflict_cb != NULL) {
        /* Call conflict callback */
        _tinystm.conflict_cb(tx, NULL, STM_WR_VALIDATE, ENTRY_FROM_READ(r), 0);
      }
#endif /* CONFLICT_TRACKING */
      stm_rollback(tx, STM_ABORT_VAL_WRITE);
      return NULL;
    }
  }
  /* Acquire lock (ETL) */
#ifdef IRREVOCABLE_ENABLED
 acquire_no_check:
#endif /* IRREVOCABLE_ENABLED */
  /* We own the lock here (ETL) */
do_write:
  /* Add address to write set */
  if (tx->w_set.nb_entries == tx->w_set.size)
    stm_allocate_ws_entries(tx, 1);
  w = &tx->w_set.entries[tx->w_set.nb_entries++];
  w->addr = addr;
  w->mask = mask;
  w->lock = lock;
  if (mask == 0) {
    /* Do not write anything */
#ifndef NDEBUG
    w->value = 0;
#endif /* ! NDEBUG */
  } else {
    /* Remember new value */
    w->value = value;
  }
# ifndef NDEBUG
  w->version = version;
# endif /* !NDEBUG */
  w->no_drop = 1;
# ifdef USE_BLOOM_FILTER
  tx->w_set.bloom |= FILTER_BITS(addr) ;
# endif /* USE_BLOOM_FILTER */

  return w;
}

static INLINE stm_word_t
stm_wbctl_RaR(stm_tx_t *tx, volatile stm_word_t *addr)
{
  /* Possible optimization: avoid adding to read set. */
  return stm_wbctl_read(tx, addr);
}

static INLINE stm_word_t
stm_wbctl_RaW(stm_tx_t *tx, volatile stm_word_t *addr)
{
  /* Cannot be much better than regular due to mask == 0 case. */
  return stm_wbctl_read(tx, addr);
}

static INLINE stm_word_t
stm_wbctl_RfW(stm_tx_t *tx, volatile stm_word_t *addr)
{
  /* We need to return the value here, so write with mask=0 is not enough. */
  return stm_wbctl_read(tx, addr);
}

static INLINE void
stm_wbctl_WaR(stm_tx_t *tx, volatile stm_word_t *addr, stm_word_t value, stm_word_t mask)
{
  /* Probably no optimization can be done here. */
  stm_wbctl_write(tx, addr, value, mask);
}

static INLINE void
stm_wbctl_WaW(stm_tx_t *tx, volatile stm_word_t *addr, stm_word_t value, stm_word_t mask)
{
  w_entry_t *w;
  /* Get the write set entry. */
  w = stm_has_written(tx, addr);
  assert(w != NULL);
  /* Update directly into the write set. */
  w->value = (w->value & ~mask) | (value & mask);
  w->mask |= mask;
}

static INLINE int
stm_wbctl_commit(stm_tx_t *tx)
{
  w_entry_t *w;
  stm_version_t l;
  stm_word_t value;

  PRINT_DEBUG("==> stm_wbctl_commit(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

  /* Acquire locks (in reverse order) */
  for (w = tx->w_set.entries + tx->w_set.nb_entries - 1; w >= tx->w_set.entries; --w) {
    /* Try to acquire lock */
restart:
    l = LOCK_READ(w->lock);
    if (LOCK_GET_OWNED(l)) {
      /* Do we already own the lock? */
      if (tx->w_set.entries <= (w_entry_t *)LOCK_GET_ADDR(l) && (w_entry_t *)LOCK_GET_ADDR(l) < tx->w_set.entries + tx->w_set.nb_entries) {
        /* Yes: ignore */
        continue;
      }
      /* Conflict: CM kicks in */
# if CM == CM_DELAY
      tx->c_lock = w->lock;
# endif /* CM == CM_DELAY */

#ifdef IRREVOCABLE_ENABLED
      if (tx->irrevocable) {
# ifdef IRREVOCABLE_IMPROVED
        if (ATOMIC_LOAD(&_tinystm.irrevocable) == 1)
          ATOMIC_STORE(&_tinystm.irrevocable, 2);
# endif /* IRREVOCABLE_IMPROVED */
        /* Spin while locked */
        goto restart;
      }
#endif /* IRREVOCABLE_ENABLED */

#ifdef CONFLICT_TRACKING
      if (_tinystm.conflict_cb != NULL) {
        /* Call conflict callback */
        _tinystm.conflict_cb(tx, ((w_entry_t*)LOCK_GET_ADDR(l))->tx, STM_WW_CONFLICT, ENTRY_FROM_WRITE(w), ENTRY_FROM_WRITE(LOCK_GET_ADDR(l)));
      }
#endif /* CONFLICT_TRACKING */
      /* Abort self */
      stm_rollback(tx, STM_ABORT_WW_CONFLICT);
      return 0;
    }
    if (LOCK_WRITE_CAS(w->lock, l, LOCK_SET_ADDR_WRITE((stm_word_t)w)) == 0)
      goto restart;
    /* We own the lock here */
    w->no_drop = 0;
    /* Store version for validation of read set */
    w->version = LOCK_GET_TIMESTAMP(l);
    tx->w_set.nb_acquired++;
  }

#ifdef IRREVOCABLE_ENABLED
  /* Verify if there is an irrevocable transaction once all locks have been acquired */
# ifdef IRREVOCABLE_IMPROVED
  /* FIXME: it is bogus. the status should be changed to idle otherwise stm_quiesce will not progress */
  if (unlikely(!tx->irrevocable)) {
    do {
      l = ATOMIC_LOAD(&_tinystm.irrevocable);
      /* If the irrevocable transaction have encountered an acquired lock, abort */
      if (l == 2) {
        stm_rollback(tx, STM_ABORT_IRREVOCABLE);
        return 0;
      }
    } while (l);
  }
# else /* ! IRREVOCABLE_IMPROVED */
  if (!tx->irrevocable && ATOMIC_LOAD(&_tinystm.irrevocable)) {
    stm_rollback(tx, STM_ABORT_IRREVOCABLE);
    return 0;
  }
# endif /* ! IRREVOCABLE_IMPROVED */
#endif /* IRREVOCABLE_ENABLED */

  /* Get commit timestamp (may exceed VERSION_MAX by up to MAX_THREADS) */
  l = FETCH_INC_CLOCK + 1;

#ifdef IRREVOCABLE_ENABLED
  if (unlikely(tx->irrevocable))
    goto release_locks;
#endif /* IRREVOCABLE_ENABLED */

  /* Try to validate (only if a concurrent transaction has committed since tx->end) */
  if (unlikely(tx->end != l - 1 && !stm_wbctl_validate(tx))) {
    /* Cannot commit */
    stm_rollback(tx, STM_ABORT_VAL_COMMIT);
    return 0;
  }

#ifdef IRREVOCABLE_ENABLED
  release_locks:
#endif /* IRREVOCABLE_ENABLED */

  /* Install new versions, drop locks and set new timestamp */
  for (w = tx->w_set.entries; w < tx->w_set.entries + tx->w_set.nb_entries; ++w) {
    if (w->mask == ~(stm_word_t)0) {
      ATOMIC_STORE(w->addr, w->value);
    } else if (w->mask != 0) {
      value = (ATOMIC_LOAD(w->addr) & ~w->mask) | (w->value & w->mask);
      ATOMIC_STORE(w->addr, value);
    }
    /* Only drop lock for last covered address in write set (cannot be "no drop") */
    if (!w->no_drop)
      LOCK_WRITE_REL(w->lock, LOCK_SET_TIMESTAMP(l));
  }

 end:
  return 1;
}

#endif /* _STM_WBCTL_H_ */
