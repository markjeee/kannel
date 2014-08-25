/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * gwmem-check.c - memory management wrapper functions, check flavor
 *
 * This implementation of the gwmem.h interface checks for writes to
 * non-allocated areas, and fills freshly allocated and freshly freed
 * areas with garbage to prevent their use.  It also reports memory
 * leaks.
 *
 * Design: Memory is allocated with markers before and after the
 * area to be used.  These markers can be checked to see if anything
 * has written to them.  There is a table of all allocated areas,
 * which is used to detect memory leaks and which contains context
 * information about each area.
 *
 * The start marker contains the index into this table, so that it
 * can be looked up quickly -- but if the start marker has been damaged,
 * the index can still be found by searching the table.
 *
 * Enlarging an area with realloc is handled by allocating new area,
 * copying the old data, and freeing the old area.  This is an expensive
 * operation which is avoided by reserving extra space (up to the nearest
 * power of two), and only enlarging the area if the requested space is
 * larger than this extra space.  The markers are still placed at exactly
 * the size requested, so every realloc does mean moving the end marker.
 *
 * When data is freed, it is overwritten with 0xdeadbeef, so that code
 * that tries to use it after freeing will likely crash.  The freed area
 * is kept around for a while, to see if anything tries to write to it
 * after it's been freed.
 * 
 * Richard Braakman
 * Alexander Malysh (added backtrace support)
 */

#include "gw-config.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#if HAVE_BACKTRACE
#include <execinfo.h>
#endif

#include "gwlib.h"

/* In this module, we must use the real versions so let's undefine the
 * accident protectors. */
#undef malloc
#undef realloc
#undef calloc
#undef free

/* Freshly malloced space is filled with NEW_AREA_PATTERN, to break
 * code that assumes it is filled with zeroes. */
#define NEW_AREA_PATTERN 0xcafebabe

/* Freed space is filled with FREE_AREA_PATTERN, to break code that
 * tries to read from it after freeing. */
#define FREE_AREA_PATTERN 0xdeadbeef

/* The marker before an area is filled with START_MARK_PATTERN
 * (except for some bookkeeping bytes at the start of the marker). */
#define START_MARK_PATTERN 0xdadaface

/* The marker beyond an area is filled with END_MARK_PATTERN. */
#define END_MARK_PATTERN 0xadadafec

/* How many bytes to dump when listing unfreed areas. */
#define MAX_DUMP 16

static int initialized = 0;

/* Use slower, more reliable method of detecting memory corruption. */
static int slow = 0;

/* We have to use a static mutex here, because otherwise the mutex_create
 * call would try to allocate memory with gw_malloc before we're
 * initialized. */
static Mutex gwmem_lock;

struct location
{
    const char *filename;
    long lineno;
    const char *function;
};

/* Duplicating the often-identical location information in every table
 * entry uses a lot of memory, but saves the effort of maintaining a
 * data structure for it, and keeps access to it fast. */
struct area
{
    void *area;    /* The allocated memory area, as seen by caller */
    size_t area_size;   /* Size requested by caller */
    size_t max_size;    /* Size we can expand area to when reallocing */
    struct location allocator;     /* Caller that alloced area */
    struct location reallocator;   /* Caller that last realloced area */
    struct location claimer;       /* Owner of area, set by caller */
#if HAVE_BACKTRACE
    void *frames[10]; /* 10 callers should be sufficient */
    size_t frame_size;
#endif
};

/* Number of bytes to reserve on either side of each allocated area,
 * to detect writes just outside the area.  It must be at least large
 * enough to hold a long. */
#define MARKER_SIZE 16

/* 100 MB */
#define MAX_TAB_SIZE (100*1024*1024L)
#define MAX_ALLOCATIONS ((long) (MAX_TAB_SIZE/sizeof(struct area)))

/* Freed areas are thrown into the free ring.  They are not released
 * back to the system until FREE_RING_SIZE other allocations have been
 * made.  This is more effective at finding bugs than releasing them
 * immediately, because when we eventually release them we can check
 * that they have not been tampered with in that time. */
#define FREE_RING_SIZE 1024

static struct area allocated[MAX_ALLOCATIONS];
static struct area free_ring[FREE_RING_SIZE];

/* Current number of allocations in the "allocated" table.  They are
 * always consecutive and start at the beginning of the table. */
static long num_allocations;

/* The free ring can wrap around the edges of its array. */
static long free_ring_start;
static long free_ring_len;

/* The next three are used for informational messages at shutdown */
/* Largest number of allocations we've had at one time */
static long highest_num_allocations;
/* Largest value of the sum of allocated areas we've had at one time */
static long highest_total_size;
/* Current sum of allocated areas */
static long total_size;

/* Static functions */

static inline void lock(void)
{
    mutex_lock(&gwmem_lock);
}

static inline void unlock(void)
{
    mutex_unlock(&gwmem_lock);
}

static unsigned long round_pow2(unsigned long num)
{
    unsigned long i;

    if (num <= 16)
        return 16;

    for (i = 32; i < 0x80000000L; i <<= 1) {
        if (num <= i)
            return i;
    }

    /* We have to handle this case separately; the loop cannot go that
     * far because i would overflow. */
    if (num <= 0x80000000L)
        return 0x80000000L;

    return 0xffffffffL;
}

/* Fill a memory area with a bit pattern */
static void fill(unsigned char *p, size_t bytes, long pattern)
{
    while (bytes > sizeof(pattern)) {
        memcpy(p, &pattern, sizeof(pattern));
        p += sizeof(pattern);
        bytes -= sizeof(pattern);
    }
    if (bytes > 0)
        memcpy(p, &pattern, bytes);
}

/* Check that a filled memory area has not changed */
static int untouched(unsigned char *p, size_t bytes, long pattern)
{
    while (bytes > sizeof(pattern)) {
        if (memcmp(p, &pattern, sizeof(pattern)) != 0)
            return 0;
        p += sizeof(pattern);
        bytes -= sizeof(pattern);
    }
    if (bytes > 0 && memcmp(p, &pattern, bytes) != 0)
        return 0;
    return 1;
}

/* Fill the end marker for this area */
static inline void endmark(unsigned char *p, size_t size)
{
    fill(p + size, MARKER_SIZE, END_MARK_PATTERN);
}

/* Fill the start marker for this area, and assign an number to the
 * area which can be used for quick lookups later.  The number must
 * not be negative. */
static void startmark(unsigned char *p, long number)
{
    gw_assert(MARKER_SIZE >= sizeof(long));
    gw_assert(number >= 0);

    fill(p - MARKER_SIZE, sizeof(long), number);
    fill(p - MARKER_SIZE + sizeof(long),
         MARKER_SIZE - sizeof(long), START_MARK_PATTERN);
}

/* Check that the start marker for this area are intact, and return the
 * marker number if it seems intact.  Return a negative number if
 * it does not seem intact. */
static long check_startmark(unsigned char *p)
{
    long number;
    if (!untouched(p - MARKER_SIZE + sizeof(long),
                   MARKER_SIZE - sizeof(long), START_MARK_PATTERN))
        return -1;
    memcpy(&number, p - MARKER_SIZE, sizeof(number));
    return number;
}

static int check_endmark(unsigned char *p, size_t size)
{
    if (!untouched(p + size, MARKER_SIZE, END_MARK_PATTERN))
        return -1;
    return 0;
}

static int check_marks(struct area *area, long index)
{
    int result = 0;

    if (check_startmark(area->area) != index) {
        error(0, "Start marker was damaged for area %ld", index);
        result = -1;
    }
    if (check_endmark(area->area, area->area_size) < 0) {
        error(0, "End marker was damaged for area %ld", index);
        result = -1;
    }

    return result;
}

static void dump_area(struct area *area)
{
    debug("gwlib.gwmem", 0, "Area %p, size %ld, max_size %ld",
          area->area, (long) area->area_size, (long) area->max_size);
    debug("gwlib.gwmem", 0, "Allocated by %s() at %s:%ld",
          area->allocator.function,
          area->allocator.filename,
          area->allocator.lineno);
    if (area->reallocator.function) {
        debug("gwlib.gwmem", 0, "Re-allocated by %s() at %s:%ld",
              area->reallocator.function,
              area->reallocator.filename,
              area->reallocator.lineno);
    }
    if (area->claimer.function) {
        debug("gwlib.gwmem", 0, "Claimed by %s() at %s:%ld",
              area->claimer.function,
              area->claimer.filename,
              area->claimer.lineno);
    }
    if (area->area_size > 0) {
        size_t i;
        unsigned char *p;
        char buf[MAX_DUMP * 3 + 1];

        p = area->area;
        buf[0] = '\0';
        for (i = 0; i < area->area_size && i < MAX_DUMP; ++i)
            sprintf(strchr(buf, '\0'), "%02x ", p[i]);

        debug("gwlib.gwmem", 0, "Contents of area (first %d bytes):", MAX_DUMP);
        debug("gwlib.gwmem", 0, "  %s", buf);
    }
#if HAVE_BACKTRACE
    {
        size_t i;
        char **strings = backtrace_symbols(area->frames, area->frame_size);
        debug("gwlib.gwmem", 0, "Backtrace of last malloc/realloc:");
        for (i = 0; i < area->frame_size; i++) {
            if (strings != NULL)
                debug("gwlib.gwmem", 0, "%s", strings[i]);
            else
                debug("gwlib.gwmem", 0, "%p", area->frames[i]);
        }
        free(strings);
    }
#endif
}

static struct area *find_area(unsigned char *p)
{
    long index;
    struct area *area;
    long suspicious_pointer;
    unsigned long p_ul;

    gw_assert(p != NULL);

    p_ul = (unsigned long) p;
    suspicious_pointer =
        (sizeof(p) == sizeof(long) &&
         (p_ul == NEW_AREA_PATTERN || p_ul == FREE_AREA_PATTERN ||
	  p_ul == START_MARK_PATTERN || p_ul == END_MARK_PATTERN));

    if (slow || suspicious_pointer) {
        /* Extra check, which does not touch the (perhaps not allocated)
	 * memory area.  It's slow, but may help pinpoint problems that
	 * would otherwise cause segfaults. */
        for (index = 0; index < num_allocations; index++) {
            if (allocated[index].area == p)
                break;
        }
        if (index == num_allocations) {
            error(0, "Area %p not found in allocation table.", p);
            return NULL;
        }
    }

    index = check_startmark(p);
    if (index >= 0 && index < num_allocations &&
        allocated[index].area == p) {
        area = &allocated[index];
        if (check_endmark(p, area->area_size) < 0) {
            error(0, "End marker was damaged for area %p", p);
            dump_area(area);
        }
        return area;
    }

    error(0, "Start marker was damaged for area %p", p);
    for (index = 0; index < num_allocations; index++) {
        if (allocated[index].area == p) {
            area = &allocated[index];
            dump_area(area);
            return area;
        }
    }

    error(0, "Could not find area information.");
    return NULL;
}

static void change_total_size(long change)
{
    total_size += change;
    if (total_size > highest_total_size)
        highest_total_size = total_size;
}

static struct area *record_allocation(unsigned char *p, size_t size,
                                                  const char *filename, long lineno, const char *function)
{
    struct area *area;
    static struct area empty_area;

    if (num_allocations == MAX_ALLOCATIONS) {
        panic(0, "Too many concurrent allocations.");
    }

    area = &allocated[num_allocations];
    *area = empty_area;
    area->area = p;
    area->area_size = size;
    area->max_size = size;
    area->allocator.filename = filename;
    area->allocator.lineno = lineno;
    area->allocator.function = function;
#if HAVE_BACKTRACE
    area->frame_size = backtrace(area->frames, sizeof(area->frames) / sizeof(void*));
#endif

    startmark(area->area, num_allocations);
    endmark(area->area, area->area_size);

    num_allocations++;
    if (num_allocations > highest_num_allocations)
        highest_num_allocations = num_allocations;
    change_total_size(size);

    return area;
}

static void remove_allocation(struct area *area)
{
    change_total_size(-1*area->area_size);
    num_allocations--;
    if (area == &allocated[num_allocations])
        return;
    check_marks(&allocated[num_allocations], num_allocations);
    *area = allocated[num_allocations];
    startmark(area->area, area - allocated);
}

static void drop_from_free_ring(long index)
{
    struct area *area;

    area = &free_ring[index];
    if (check_marks(area, index) < 0 ||
        !untouched(area->area, area->area_size, FREE_AREA_PATTERN)) {
        error(0, "Freed area %p has been tampered with.", area->area);
        dump_area(area);
    }
    free((unsigned char *)area->area - MARKER_SIZE);
}

static void put_on_free_ring(struct area *area)
{
    /* Simple case: We're still filling the free ring. */
    if (free_ring_len < FREE_RING_SIZE) {
        free_ring[free_ring_len] = *area;
        startmark(area->area, free_ring_len);
        free_ring_len++;
        return;
    }

    /* Normal case: We need to check and release a free ring entry,
     * then put this one in its place. */

    drop_from_free_ring(free_ring_start);
    free_ring[free_ring_start] = *area;
    startmark(area->area, free_ring_start);
    free_ring_start = (free_ring_start + 1) % FREE_RING_SIZE;
}

static void free_area(struct area *area)
{
    fill(area->area, area->area_size, FREE_AREA_PATTERN);
    put_on_free_ring(area);
    remove_allocation(area);
}

void gw_check_init_mem(int slow_flag)
{
    mutex_init_static(&gwmem_lock);
    slow = slow_flag;
    initialized = 1;
}

void gw_check_shutdown(void)
{
    mutex_destroy(&gwmem_lock);
    initialized = 0;
}

void *gw_check_malloc(size_t size, const char *filename, long lineno,
                      const char *function)
{
    unsigned char *p;

    gw_assert(initialized);

    /* ANSI C89 says malloc(0) is implementation-defined.  Avoid it. */
    gw_assert(size > 0);

    p = malloc(size + 2 * MARKER_SIZE);
    if (p == NULL)
        panic(errno, "Memory allocation of %ld bytes failed.", (long)size);
    p += MARKER_SIZE;

    lock();
    fill(p, size, NEW_AREA_PATTERN);
    record_allocation(p, size, filename, lineno, function);
    unlock();

    return p;
}

void *gw_check_calloc(int nmemb, size_t size, const char *filename, long lineno,
                      const char *function)
{
    unsigned char *p;

    gw_assert(initialized);

    /* ANSI C89 says malloc(0) is implementation-defined.  Avoid it. */
    gw_assert(size > 0);
    gw_assert(nmemb > 0);

    p = calloc(1, (nmemb*size) + 2 * MARKER_SIZE);
    if (p == NULL)
        panic(errno, "Memory allocation of %ld bytes failed.", (long)size);

    p += MARKER_SIZE;

    lock();
    record_allocation(p, size, filename, lineno, function);
    unlock();

    return p;
}

void *gw_check_realloc(void *p, size_t size, const char *filename,
                       long lineno, const char *function)
{
    struct area *area;

    if (p == NULL)
        return gw_check_malloc(size, filename, lineno, function);

    gw_assert(initialized);
    gw_assert(size > 0);

    lock();
    area = find_area(p);
    if (!area) {
        unlock();
        panic(0, "Realloc called on non-allocated area");
    }

    if (size == area->area_size) {
        /* No changes */
    } else if (size <= area->max_size) {
        change_total_size(size - area->area_size);
        area->area_size = size;
        endmark(p, size);
    } else if (size > area->max_size) {
        /* The current block is not large enough for the reallocation.
         * We will allocate a new block, copy the data over, and free
         * the old block.  We round the size up to a power of two,
         * to prevent frequent reallocations. */
        struct area *new_area;
        size_t new_size;
        unsigned char *new_p;

        new_size = round_pow2(size + 2 * MARKER_SIZE);
        new_p = malloc(new_size);
        new_size -= 2 * MARKER_SIZE;
        new_p += MARKER_SIZE;
        memcpy(new_p, p, area->area_size);
        fill(new_p + area->area_size, size - area->area_size,
             NEW_AREA_PATTERN);
        new_area = record_allocation(new_p, size,
                                     area->allocator.filename,
                                     area->allocator.lineno,
                                     area->allocator.function);
        new_area->max_size = new_size;
        free_area(area);

        p = new_p;
        area = new_area;
    }

    area->reallocator.filename = filename;
    area->reallocator.lineno = lineno;
    area->reallocator.function = function;
    unlock();
    return p;
}

void gw_check_free(void *p, const char *filename, long lineno,
                   const char *function)
{
    struct area *area;
    gw_assert(initialized);

    if (p == NULL)
        return;

    lock();
    area = find_area(p);
    if (!area) {
        unlock();
        panic(0, "Free called on non-allocated area");
    }

    free_area(area);
    unlock();
}

char *gw_check_strdup(const char *str, const char *filename, long lineno,
                      const char *function)
{
    char *copy;
    int size;

    gw_assert(initialized);
    gw_assert(str != NULL);

    size = strlen(str) + 1;
    copy = gw_check_malloc(size, filename, lineno, function);
    memcpy(copy, str, size);
    return copy;
}

void *gw_check_claim_area(void *p, const char *filename, long lineno,
                          const char *function)
{
    struct area *area;

    /* Allow this for the convenience of wrapper macros. */
    if (p == NULL)
        return NULL;

    lock();
    area = find_area(p);
    if (!area) {
        unlock();
        panic(0, "Claim_area called on non-allocated area");
    }

    area->claimer.filename = filename;
    area->claimer.lineno = lineno;
    area->claimer.function = function;
    unlock();

    /* For convenience of calling macros */
    return p;
}

void gw_check_check_leaks(void)
{
    long calculated_size;
    long index;

    gw_assert(initialized);
    lock();

    for (index = 0; index < free_ring_len; index++) {
        drop_from_free_ring(index);
    }
    free_ring_len = 0;

    calculated_size = 0;
    for (index = 0; index < num_allocations; index++) {
        calculated_size += allocated[index].area_size;
    }
    gw_assert(calculated_size == total_size);

    debug("gwlib.gwmem", 0, "----------------------------------------");
    debug("gwlib.gwmem", 0, "Current allocations: %ld areas, %ld bytes",
          num_allocations, total_size);
    debug("gwlib.gwmem", 0, "Highest number of allocations: %ld areas",
          highest_num_allocations);
    debug("gwlib.gwmem", 0, "Highest memory usage: %ld bytes",
          highest_total_size);
    for (index = 0; index < num_allocations; index++) {
        check_marks(&allocated[index], index);
        dump_area(&allocated[index]);
    }

    unlock();
}

int gw_check_is_allocated(void *p)
{
    struct area *area;

    lock();
    area = find_area(p);
    unlock();
    return area != NULL;
}

long gw_check_area_size(void *p)
{
    struct area *area;
    size_t size;

    lock();
    area = find_area(p);
    if (!area) {
        unlock();
        warning(0, "Area_size called on non-allocated area %p", p);
        return -1;
    }
    size = area->area_size;
    unlock();
    return size;
}
