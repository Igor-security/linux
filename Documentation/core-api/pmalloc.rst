.. SPDX-License-Identifier: GPL-2.0

.. _pmalloc:

Protectable memory allocator
============================

Purpose
-------

The pmalloc library is meant to provide read-only status to data that,
for some reason, could neither be declared as constant, nor could it take
advantage of the qualifier __ro_after_init.
But it is in spirit either fully write-once/read-only or at least
write-seldom/mostly-read-only.
At some point it might get teared down, however that doesn't affect how it
is treated, while it's still relevant.
Pmalloc protects data from both accidental and malicious overwrites.

Example: A policy that is loaded from userspace.


Concept
-------

The MMU available in the system can be used to write protect memory pages.
Unfortunately this feature cannot be used as-it-is, to protect sensitive
data, because this potentially read-only data is typically interleaved
with other data, which must stay writeable.

pmalloc introduces the concept of protectable memory pools.
A pool contains a list of areas of virtually contiguous pages of
memory. An area is the minimum amount of memory that pmalloc allows to
protect, because the user might have allocated a memory range that
crosses the boundary between pages.

When an allocation is performed, if there is not enough memory already
available in the pool, a new area of suitable size is grabbed.
The size chosen is the largest between the roundup (to PAGE_SIZE) of
the request from pmalloc and friends and the refill parameter specified
when creating the pool.

When a pool is created, it is possible to specify three parameters:
- refill size: the minimum size of the memory area to allocate when needed
- rewritable: if te content can be modified
- align_order: the default alignment to use when reserving memory

To facilitate the conversion of existing code to pmalloc pools, several
helper functions are provided, mirroring their k/vmalloc counterparts.
However one is missing. There is no pfree() because the memory protected
by a pool will be released exclusively when the pool is destroyed.



Caveats
-------

- When a pool is protected, whatever memory would be still available in
  the current vmap_area (from which allocations are performed) is
  relinquished.

- As already explained, freeing of memory is not supported. Pages will be
  returned to the system upon destruction of the memory pool that they
  belong to. For this reason, no pfree() function is provided

- The address range available for vmalloc (and thus for pmalloc too) is
  limited, on 32-bit systems. However it shouldn't be an issue, since not
  much data is expected tobe dynamically allocated and turned into
  read-only.

- Regarding SMP systems, the allocations are expected to happen mostly
  during an initial transient, after which there should be no more need
  to perform cross-processor synchronizations of page tables.
  Loading of kernel modules is an exception to this, but it's not expected
  to happen with such high frequency to become a problem.


Use cases
---------

- Pmalloc memory is intended to complement __read_only_after_init.
  It can be used, for example, where there is a write-once variable, for
  which it is not possible to know the initialization value before init
  is completed (which is what __read_only_after_init requires).
 
- Pmalloc can be useful also when the amount of data to protect is not
  known at compile time and the memory can only be allocated dynamically.
 
- When it's not possible to fix a point in time after which the data
  becomes immutable, but it's still fairly unlikely that it will change,
  rare write becomes a less vulnerable alternative to leaving the data
  located in freely rewritable memory.
 
- Finally, it can be useful also when it is desirable to control
  dynamically (for example throguh the kernel command line) if some
  specific data ought to be protected or not, without having to rebuild
  the kernel, for toggling a "const" qualifier.
  This can be used, for example, by a linux distro, to create a more
  versatile binary kernel and allow its users to toggle between developer
  (unprotected) or production (protected) modes by reconfiguring the
  bootloader.
 

When *not* to use pmalloc
-------------------------

Using pmalloc is not a good idea in some cases:

- when optimizing TLB utilization is paramount:
  pmalloc relies on virtual memory areas and will therefore use more
  tlb entries. It still does a better job of it, compared to invoking
  vmalloc for each allocation, but it is undeniably less optimized wrt to
  TLB use than the physmap.

- when rare-write is not-so-rare:
  rare-write does not allow updates in-place, it rather expects to be
  provided a version of how the data is supposed to be, and then it
  performs the update accordingly, by modifying the original data.
  Such procedure takes an amount of time that is proportional to the
  number of pages affected.


Utilization
-----------

The typical sequence, when using pmalloc, is:

#. create a pool, choosing if it can be altered or not, after protection

   :c:func:`pmalloc_create_pool`

#. issue one or more allocation requests to the pool

   :c:func:`pmalloc`

   or

   :c:func:`pzalloc`

#. initialize the memory obtained, with the desired values

#. write-protect the memory so far allocated

   :c::func:`pmalloc_protect_pool`

#. [optional] modify the pool, if it was created as rewritable

   :c::func:`pmalloc_rare_write`

#. iterate over the last 4 points as needed

#. [optional] destroy the pool

   :c:func:`pmalloc_destroy_pool`

API
---

.. kernel-doc:: include/linux/pmalloc.h
.. kernel-doc:: mm/pmalloc.c
