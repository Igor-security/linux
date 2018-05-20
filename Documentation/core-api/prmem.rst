.. SPDX-License-Identifier: GPL-2.0

.. _prmem:

Memory Protection
=================

:Date: October 2018
:Author: Igor Stoppa <igor.stoppa@huawei.com>

Foreword
--------
- In a typical system using some sort of RAM as execution environment,
  **all** memory is initially writable.

- It must be initialized with the appropriate content, be it code or data.

- Said content typically undergoes modifications, i.e. relocations or
  relocation-induced changes.

- The present document doesn't address such transient.

- Kernel code is protected at system level and, unlike data, it doesn't
  require special attention.

Protection mechanism
--------------------

- When available, the MMU can write protect memory pages that would be
  otherwise writable.

- The protection has page-level granularity.

- An attempt to overwrite a protected page will trigger an exception.
- **Write protected data must go exclusively to write protected pages**
- **Writable data must go exclusively to writable pages**

Available protections for kernel data
-------------------------------------

- **constant**
   Labelled as **const**, the data is never supposed to be altered.
   It is statically allocated - if it has any memory footprint at all.
   The compiler can even optimize it away, where possible, by replacing
   references to a **const** with its actual value.

- **read only after init**
   By tagging an otherwise ordinary statically allocated variable with
   **__ro_after_init**, it is placed in a special segment that will
   become write protected, at the end of the kernel init phase.
   The compiler has no notion of this restriction and it will treat any
   write operation on such variable as legal. However, assignments that
   are attempted after the write protection is in place, will cause
   exceptions.

- **write rare after init**
   This can be seen as variant of read only after init, which uses the
   tag **__wr_after_init**. It is also limited to statically allocated
   memory. It is still possible to alter this type of variables, after
   the kernel init phase is complete, however it can be done exclusively
   with special functions, instead of the assignment operator. Using the
   assignment operator after conclusion of the init phase will still
   trigger an exception. It is not possible to transition a certain
   variable from __wr_ater_init to a permanent read-only status, at
   runtime.

- **dynamically allocated write-rare / read-only**
   After defining a pool, memory can be obtained through it, primarily
   through the **pmalloc()** allocator. The exact writability state of the
   memory obtained from **pmalloc()** and friends can be configured when
   creating the pool. At any point it is possible to transition to a less
   permissive write status the memory currently associated to the pool.
   Once memory has become read-only, it the only valid operation, beside
   reading, is to released it, by destroying the pool it belongs to.


Protecting dynamically allocated memory
---------------------------------------

When dealing with dynamically allocated memory, three options are
 available for configuring its writability state:

- **Options selected when creating a pool**
   When creating the pool, it is possible to choose one of the following:
    - **PMALLOC_MODE_RO**
       - Writability at allocation time: *WRITABLE*
       - Writability at protection time: *NONE*
    - **PMALLOC_MODE_WR**
       - Writability at allocation time: *WRITABLE*
       - Writability at protection time: *WRITE-RARE*
    - **PMALLOC_MODE_AUTO_RO**
       - Writability at allocation time:
           - the latest allocation: *WRITABLE*
           - every other allocation: *NONE*
       - Writability at protection time: *NONE*
    - **PMALLOC_MODE_AUTO_WR**
       - Writability at allocation time:
           - the latest allocation: *WRITABLE*
           - every other allocation: *WRITE-RARE*
       - Writability at protection time: *WRITE-RARE*
    - **PMALLOC_MODE_START_WR**
       - Writability at allocation time: *WRITE-RARE*
       - Writability at protection time: *WRITE-RARE*

   **Remarks:**
    - The "AUTO" modes perform automatic protection of the content, whenever
       the current vmap_area is used up and a new one is allocated.
        - At that point, the vmap_area being phased out is protected.
        - The size of the vmap_area depends on various parameters.
        - It might not be possible to know for sure *when* certain data will
          be protected.
        - The functionality is provided as tradeoff between hardening and speed.
        - Its usefulness depends on the specific use case at hand
    - The "START_WR" mode is the only one which provides immediate protection, at the cost of speed.

- **Protecting the pool**
   This is achieved with **pmalloc_protect_pool()**
    - Any vmap_area currently in the pool is write-protected according to its initial configuration.
    - Any residual space still available from the current vmap_area is lost, as the area is protected.
    - **protecting a pool after every allocation will likely be very wasteful**
    - Using PMALLOC_MODE_START_WR is likely a better choice.

- **Upgrading the protection level**
   This is achieved with **pmalloc_make_pool_ro()**
    - it turns the present content of a write-rare pool into read-only
    - can be useful when the content of the memory has settled


Caveats
-------
- Freeing of memory is not supported. Pages will be returned to the
  system upon destruction of their memory pool.

- The address range available for vmalloc (and thus for pmalloc too) is
  limited, on 32-bit systems. However it shouldn't be an issue, since not
  much data is expected to be dynamically allocated and turned into
  write-protected.

- Regarding SMP systems, changing state of pages and altering mappings
  requires performing cross-processor synchronizations of page tables.
  This is an additional reason for limiting the use of write rare.

- Not only the pmalloc memory must be protected, but also any reference to
  it that might become the target for an attack. The attack would replace
  a reference to the protected memory with a reference to some other,
  unprotected, memory.

- The users of rare write must take care of ensuring the atomicity of the
  action, respect to the way they use the data being altered; for example,
  take a lock before making a copy of the value to modify (if it's
  relevant), then alter it, issue the call to rare write and finally
  release the lock. Some special scenario might be exempt from the need
  for locking, but in general rare-write must be treated as an operation
  that can incur into races.

- pmalloc relies on virtual memory areas and will therefore use more
  tlb entries. It still does a better job of it, compared to invoking
  vmalloc for each allocation, but it is undeniably less optimized wrt to
  TLB use than using the physmap directly, through kmalloc or similar.


Utilization
-----------

**add examples here**

API
---

.. kernel-doc:: include/linux/prmem.h
.. kernel-doc:: mm/prmem.c
.. kernel-doc:: include/linux/prmemextra.h
