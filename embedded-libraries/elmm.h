/* A Simple memory manager. PUBLIC DOMAIN, NO COPYRIGHT, NO WARRANTY.
 * Peace, love & Buddhism for all. -Zak Fenton, MMXX.
 * FEATURES:
 *  Minimal, portable and embeddable implementations of "malloc", "free", "calloc" and "realloc" functions.
 *  Should be threadsafe if a locking function is provided before initialisation (the init function MUST be called
 *  before attempting to allocate any memory! But only a "bigchunk" function is required for the initialisation to
 *  work.) Fine-grained locking might be added in the future after trying some multithreaded tests.
 *  Almost completely standalone, no standard library functions are required and the only headers required are "stdint" and
 *  "stdbool" (which are both very easy to implement in the rare cases where standard versions don't work).
 *  Compaction is provided and works separately to the regular malloc/free control flows. The intention is for malloc/free
 *  to work fairly quickly and not bother sorting or merging lists, then an idle process or similar can regularly scan and
 *  compact the heap. (Returning unused memory to the environment hasn't been implemented yet, but should basically consist
 *  of a quick check-and-trim process after running the compaction function.)
 *  Provides a fairly robust way of getting free/used statistics (this is designed for future extension so only minimal
 *  changes are required to support new or system-specific metrics).
 *  Works in both 32-bit and 64-bit builds (should also adapt to smaller or larger word sizes, should use appropriate types
 *  and sizeof instead of making assumptions but rounding values etc. may need to be adapted for more obscure cases.
 * NOT YET IMPLEMENTED:
 *  Only tested under very simple and fake conditions (see testmain.c), TODO: Proper stress tests (might be easy to hook up
 *  something like Lua which can take a memory manager callback and then just run some large Lua program, otherwise some
 *  kind of benchmark program might be a good basis for a test).
 *  Some debugging code has been left in for now (search "printf" to remove the debug lines, but some may be useful to
 *  indicate TODOs or errors etc. A separate "warning" callback might be added in the future for debugging memory issues.)
 *  Doesn't release memory back to the system until cleanup is called.
 *  Growing the heap after the first call to the bigchunk function may not be fully finished/tested.
 *  Not very fast and may not have enough safeguards for some use cases, but hopefully will be enough for a starting point.
 */
#ifndef ELMM_H
#define ELMM_H

#include <stdint.h>
#include <stdbool.h>

#define ELMM_VERSION 0x0100
/**/
#define ELMM_STATIC static
#define ELMM_INLINE static inline

/* Internally, a smallchunk header is used for each allocated and unallocated piece (besides the header). */
#define ELMM_SMALLCHUNK_FREE        1234
#define ELMM_SMALLCHUNK_ALLOCATED   4321
typedef struct elmm_smallchunk elmm_smallchunk_t;
struct elmm_smallchunk {
    uintptr_t check1; /* Either ELMM_SMALLCHUNK_FREE or ELMM_SMALLCHUNK_ALLOCATED.*/
    uintptr_t size;
    elmm_smallchunk_t* next;
    uintptr_t check2; /* Either ELMM_SMALLCHUNK_FREE or ELMM_SMALLCHUNK_ALLOCATED.*/
};

/* Internal elements of a bigchunk*/
typedef struct elmm_bigchunk_internals elmm_bigchunk_internals_t;
typedef struct elmm_bigchunk elmm_bigchunk_t;
struct elmm_bigchunk_internals {
    uintptr_t allocatedTop;
    elmm_bigchunk_t* nextChunk;
    elmm_bigchunk_t* prevChunk;
    elmm_smallchunk_t* firstFree;
    elmm_smallchunk_t* firstAllocated;
};

struct elmm_bigchunk {
    void* startAddress;
    uintptr_t headerSize;
    uintptr_t totalSize;
    uintptr_t maximumSize;
    const char* errorString;
    elmm_bigchunk_internals_t internals;
};

/* The bigchunk allocator takes a size (or zero) and an "old" bigchunk address (or NULL).
 * If the oldchunk is NULL and a chunk with the given size can be allocated, the function should
 * allocate a chunk (of at least that size) and fill in a elmm_bigchunk_t structure at the start
 * of the chunk (the structure's address must match the start address). In this case, a NULL
 * should be returned if the chunk can't be allocated.
 *
 * Unused fields of the bigchunk structure
 * should be cleared to zero, but the rest of the memory doesn't necessarily need to be cleared
 * (it probably should be anyway for security reasons, but the allocator will clear it itself
 * anyway). The total size of a bigchunk is expected to always include the header (the header is
 * part of the chunk, and only needs to be separated by it's own headerSize field).
 *
 * If the oldchunk is non-null and the size is zero, the function should deallocate that chunk
 * entirely (or set it to it's minimum size, depending on implementation), and return NULL (if
 * there is an error, it should return the structure and set it's error string).
 * 
 * Before returning a bigchunk, the function can set the maximumSize field to a value other
 * than zero, in which case the higher-level allocater may call the function again with
 * the same chunk and a different size value to attempt to resize the chunk. The allocator
 * will generally try to use memory from an existing bigchunk before attempting to get a new
 * one, which means an implementation of the bigchunk function can just work with a single
 * resizable bigchunk (as in the sbrk implementation).
 * 
 * Between calls to the bigchunk function, the allocator may change, resize or reconstruct the
 * bigchunk header, but subsequent calls will always use the same header address and sensible values
 * (it must always be located at the very start of the chunk and the size fields should be unchanged
 * between calls, except for the headerSize which may be increased by the caller to account for it's own
 * chunk headers).
 */
typedef elmm_bigchunk_t* (*elmm_bigchunk_function_t)(uintptr_t size, elmm_bigchunk_t* oldchunk, void* udata);


/* Internally, the memory manager works by allocating "big chunks" from somewhere else and then allocating the
 * "small chunks" requested by the user within that memory. On Unix-like systems and on embedded systems using
 * only one heap at a time, it may be more convenient to allocate from a single memory bank which is just
 * increased or decreased in size as required. The system call usually used to achieve this is called "sbrk",
 * so a wrapper around our usual "big chunk" allocation scheme which can be plugged directly into sbrk (by
 * ignoring the udata argument).
 */

/* The error code returned by sbrk should be "NULL minus one", but some casts may be required to get the value
 * right without compiler errors.
 */
#define ELMM_SBRK_ERROR  ((void*)(((char*)(((void*)NULL))) - 1))
typedef struct elmm_sbrk_data elmm_sbrk_data_t;
typedef void* (*elmm_sbrk_function_t)(intptr_t incr, void* udata);

struct elmm_sbrk_data {
    elmm_sbrk_function_t func;
    void* udata;
    /* This can either be set to the maximum heap size, or to zero (which will imply a default of 1GB).
     * This should generally be set as high as possible, but can be used as an easy way to set an effective
     * limit on memory usage.
     */
    uintptr_t max;
    /* This should initially be set to NULL, but is otherwise used to store the single resizable chunk
     * which represents the resizable heap.
     */
    elmm_bigchunk_t* onlyChunk;
};

/* This function can be used as the bigchunk function if you need it to work on top of a "sbrk"-like API.
 * A elmm_sbrk_data_t should be given as the userdata, and it holds a pointer to an sbrk-like function.
 */
ELMM_STATIC elmm_bigchunk_t* elmm_bigchunk_sbrk(uintptr_t size, elmm_bigchunk_t* oldchunk, void* udata) {
    elmm_sbrk_data_t* data = (elmm_sbrk_data_t*)udata;
    //printf("elmm_bigchunk_sbrk(%d,%d,%d)\n", size, oldchunk, udata);
    if (size > 0 && oldchunk == NULL) {
        if (data->onlyChunk != NULL) {
            return NULL;
        }
        data->onlyChunk = (elmm_bigchunk_t*)data->func(size, data->udata);
        if (data->onlyChunk == ELMM_SBRK_ERROR) {
            return NULL;
        }
        data->onlyChunk->startAddress = (void*)data->onlyChunk; // Header MUST be at the start address.
        data->onlyChunk->headerSize = sizeof(elmm_bigchunk_t);
        data->onlyChunk->totalSize = size;
        data->onlyChunk->maximumSize = (data->max == 0) ? 1024 * 1024 * 1024 : data->max;
        return data->onlyChunk;
    } else if (size > 0 && oldchunk != NULL) {
        //printf("Attempting to resize...\n");
        if (data->onlyChunk != oldchunk) {
            return NULL;
        }
        intptr_t diff = size - data->onlyChunk->totalSize;
        void* sbrkresult = data->func(diff, data->udata);
        // TODO: Should probably check that the sbrk function returned a pointer exactly where we expected
        if (sbrkresult == ELMM_SBRK_ERROR) {
            return NULL;
        }
        data->onlyChunk->totalSize = size;
        return data->onlyChunk;
    } else if (size == 0 && oldchunk != NULL) {
        //printf("Attempting to deallocate...\n");
        if (data->onlyChunk != oldchunk) {
            return NULL;
        }
        //TODO...
        return NULL;
    } else {
        //printf("TODO!!!\n");
        return NULL;
    }
}

/* A locking function can be provided to the memory manager so that it can be used in multithreaded
 * environments. If provided, the locking function should return true on success or false on error,
 * and needs to obey a few pretty universal commands that should be easy to implement for any
 * multithreaded environment. Use of a lock function ensures that calls to the bigchunk function
 * would not be made simultaneously on different threads, but other locks may be used internally
 * to maintain structures.
 *
 * The locking function is expected to either be very straightforward (using the variable address
 * to do some platform-specific atomic check-and-set operation) or to allocate/delete it's own
 * lock structures (storing the address of the corresponding structure in each lockVariable).
 */
#define ELMM_LOCK_NEW       1
#define ELMM_LOCK_DELETE    2
#define ELMM_LOCK_TRYLOCK   3
#define ELMM_LOCK_WAITLOCK  4
#define ELMM_LOCK_UNLOCK    5
#define ELMM_LOCK_GETTHREAD 6

typedef bool (*elmm_lock_function_t)(int command, void** lockVarible, void* udata);

/* This function is used internally as the lock function if no other one is provided.
 * (The current implementation doesn't perform any checks, it just sets the lock to
 * NULL and reports success. Future versions might at least attempt to make sure
 * the commands are issued in the correct order.)
 */
ELMM_STATIC bool elmm_nolock(int command, void** lockVariable, void* udata) {
    *lockVariable = NULL;
    return true;
}

typedef struct elmm elmm_t;
struct elmm {
    /* Some configuration options come first. These can be set to zero before initialisation,
     * but shouldn't be modified by the caller after initialising the memory mananger.
     */
    uintptr_t bigchunkMinimum;
    uintptr_t bigchunkGranularity;

    /* Callback functions for the heap management and locking come next. A bigchunk function
     * is mandatory, but the lock function is optional (if it's NULL at initialisation,
     * it will be set to a locking function which has no effect or only works in single-threaded
     * implementations).
     */
    elmm_bigchunk_function_t bigchunkFunction;
    void* bigchunkData;
    elmm_lock_function_t lockFunction;
    void* lockData;

    /* Internal pointers are stored last.
     */
    void* mainLock;
    elmm_bigchunk_t* firstChunk;

    bool initialised;
};

#define ELMM_STAT_VERSION       0
#define ELMM_STAT_ALLOCATED     1
#define ELMM_STAT_FREE          2
#define ELMM_STAT_TOTAL         3
#define ELMM_STAT_OVERHEADS     4
#define ELMM_STATTOP            5

/* Called internally by elmm_innerstat to add up the "size" elements of each smallchunk in the given list. */
ELMM_INLINE uintptr_t elmm_innerstatpart(elmm_t* mm, elmm_smallchunk_t* listHead) {
    uintptr_t result = 0;

    while (listHead != NULL) {
        result += listHead->size;
        listHead = listHead->next;
    }

    return result;
}

/* This function is only useful for getting statistics WHILE THE MEMORY MANAGER IS LOCKED OR NOT BEING USED! 
 * TODO: A cleaner API which locks the collector and fills an array with stats, and possibly a way to get
 * chunk stats safely (maybe even require some explicit locking for the stats API to ensure consistency with
 * minimal interruption).
 */
ELMM_STATIC uintptr_t elmm_innerstat(elmm_t* mm, uintptr_t statnum) {
    elmm_bigchunk_t* chunk = mm->firstChunk;
    uintptr_t result = 0;
    switch (statnum) {
    case ELMM_STAT_VERSION:
        return ELMM_VERSION;
    case ELMM_STAT_ALLOCATED:
        while (chunk != NULL) {
            result += elmm_innerstatpart(mm, chunk->internals.firstAllocated);
            chunk = chunk->internals.nextChunk;
        }
        break;
    case ELMM_STAT_FREE:
        while (chunk != NULL) {
            result += elmm_innerstatpart(mm, chunk->internals.firstFree);
            chunk = chunk->internals.nextChunk;
        }
        break;
    case ELMM_STAT_TOTAL:
        while (chunk != NULL) {
            result += chunk->totalSize;
            chunk = chunk->internals.nextChunk;
        }
        break;
    case ELMM_STAT_OVERHEADS:
        return elmm_innerstat(mm, ELMM_STAT_TOTAL) - (elmm_innerstat(mm, ELMM_STAT_ALLOCATED) + elmm_innerstat(mm, ELMM_STAT_FREE));
    default:
        return 0 - 1;
    }
    return result;
}

ELMM_INLINE elmm_smallchunk_t* elmm_getheader(elmm_t* mm, void* allocatedMemory) {
    if (allocatedMemory == NULL) {
        return NULL;
    } else {
        return ((elmm_smallchunk_t*)allocatedMemory) - 1;
    }
}

ELMM_INLINE uintptr_t elmm_sizeof(elmm_t* mm, void* allocatedMemory) {
    if (allocatedMemory == NULL) {
        return 0;
    } else {
        return elmm_getheader(mm, allocatedMemory)->size;
    }
}

ELMM_STATIC elmm_bigchunk_t* elmm_allocinner(elmm_t* mm, uintptr_t minsize) {
    /* Header size plus the size of some number of smallchunk structures is added to the minimum size
     * to account for any overheads which might be required to allocate (at least) a structure of
     * the given size size.
     */
    minsize += sizeof(elmm_bigchunk_t) + (sizeof(elmm_smallchunk_t) * 10); // This should more than cover the minimum overheads
    if (minsize < mm->bigchunkMinimum) {
        minsize = mm->bigchunkMinimum;
    }
    while ((minsize % mm->bigchunkGranularity) != 0) {
        minsize++;
    }
    elmm_bigchunk_t* result = mm->bigchunkFunction(minsize, NULL, mm->bigchunkData);
    if (result == NULL) {
        //printf("BIGCHUNK FUNCTION GAVE US NULL!\n");
        return NULL;
    }
    if (result->headerSize > sizeof(elmm_bigchunk_t)) {
        return NULL;
    }
    if (result->startAddress != (void*)result) {
        return NULL;
    }
    result->internals.nextChunk = NULL;
    result->internals.prevChunk = NULL;
    /* When allocated by the bigchunk function, the header size only needs to represent the size of the header
     * known to (and cleared or filled in by) the bigchunk function. It's extended here to ensure that it fits
     * all of our interna fields (which could be ignored by the bigchunk implementation) and also aligns to a
     * reasonable boundary for the purposes of allocating within it.
     */
    result->headerSize = sizeof(elmm_bigchunk_t);
    while ((result->headerSize % 16) != 0) {
        result->headerSize++;
    }
    uint8_t* bytes = (uint8_t*)result->startAddress;
    result->internals.firstAllocated = NULL;
    result->internals.firstFree = (elmm_smallchunk_t*)(bytes + result->headerSize);
    result->internals.firstFree->check1 = ELMM_SMALLCHUNK_FREE;
    result->internals.firstFree->size = result->totalSize - (result->headerSize + sizeof(elmm_smallchunk_t));
    result->internals.firstFree->next = NULL;
    result->internals.firstFree->check2 = ELMM_SMALLCHUNK_FREE;

    return result;
}

/* Must be called before any other functions to initialise a memory manager.
 * The memory manager structure itself is provided by the caller, and at a minimum it should have it's
 * bigchunkFunction set to an appropriate value (all unused fields should be cleared to zero/NULL before
 * initialisation).
 *
 * NOTE: The reason the init function needs to be called explicitly (rather than automatically e.g. in the
 * first call to "elmm_malloc") is just because of edge-cases involving multithreaded programs: If the
 * memory manager isn't used before creating a second thread, then calling elmm_malloc at the same time from any
 * two threads could lead to both threads trying to initialise the structure at the same time (which is
 * critical because the initialisation function needs to initialise any locks which would normally safely
 * synchronise multithreaded access). This shouldn't be an issue in many cases, so in a wrapper function
 * you could just check the "initialised" field and initialise whenever necessary (but there would probably
 * be a better place to put the initialisation call in most cases anyway).
 */
ELMM_STATIC bool elmm_init(elmm_t* mm) {
    if (mm == NULL) {
        return false;
    }

    if (mm->initialised) {
        return false;
    }

    if (mm->bigchunkFunction == NULL) {
        return false;
    }

    if (mm->bigchunkMinimum == 0) {
        mm->bigchunkMinimum = 1024;
    }

    if (mm->bigchunkGranularity == 0) {
        mm->bigchunkGranularity = 1024;
    }

    if (mm->lockFunction == NULL) {
        mm->lockFunction = &elmm_nolock;
        mm->lockData = NULL;
    }

    if (!mm->lockFunction(ELMM_LOCK_NEW, &mm->mainLock, mm->lockData)) {
        return false;
    }

    /* The lock is then obtained to ensure that the structure is locked until
     * AFTER it's set to initialised.
     */
    if (!mm->lockFunction(ELMM_LOCK_WAITLOCK, &mm->mainLock, mm->lockData)) {
        return false;
    }

    mm->firstChunk = elmm_allocinner(mm, mm->bigchunkMinimum);
    if (mm->firstChunk == NULL) {
        //printf("ALLOC FAILED\n");
        return false;
    }

    mm->initialised = true;

    if (!mm->lockFunction(ELMM_LOCK_UNLOCK, &mm->mainLock, mm->lockData)) {
        return false;
    }

    return true;
}

ELMM_INLINE bool elmm_checkinit(elmm_t* mm) {
    if (mm != NULL && mm->initialised) {
        return true;
    } else {
        return false;
        /* Initially the plan was to automatically call elmm_init(mm), but then I realised this wouldn't be threadsafe.
         * So now each memory manager structure needs to be initialised explicitly before using it (and functions should
         * just fail otherwise). The initialisation function (and, if called, the cleanup function) should only be invoked
         * from a single thread (but if a lock function is provided, normal memory management functions can be called from
         * any thread once it's initialised).
         */
    }
}

/* The inverse of elmm_init. Should be called when the heap is completely finished to deallocate any remaining chunks. In
 * practice, if only one heap is used for the entire duration of a program then a program doesn't really need to clean it up
 * (all of the program's memory would normally be reclaimed when the program ends anyway), but if multiple heaps are used
 * it may become necessary to deallocate some of them individually.
 */
ELMM_INLINE bool elmm_cleanup(elmm_t* mm) {
    if (mm == NULL || mm->bigchunkFunction == NULL || mm->lockFunction == NULL) {
        return false;
    }

    mm->lockFunction(ELMM_LOCK_WAITLOCK, &mm->mainLock, mm->lockData);

    elmm_bigchunk_t* chunk = mm->firstChunk;
    mm->firstChunk = NULL;
    while (chunk != NULL) {
        elmm_bigchunk_t* deadChunk = chunk;
        chunk = deadChunk->internals.nextChunk;
        mm->bigchunkFunction(0, deadChunk, mm->bigchunkData);
    }

    mm->lockFunction(ELMM_LOCK_UNLOCK, &mm->mainLock, mm->lockData);

    /* Finally, delete the lock.*/
    mm->lockFunction(ELMM_LOCK_DELETE, &mm->mainLock, mm->lockData);

    return true;
}

/* This function is called by elmm_growheap in order to resize a chunk. One important responsibility is to create a new
 * smallchunk representing the allocated space.
 */
ELMM_INLINE bool elmm_growinner(elmm_t* mm, elmm_bigchunk_t* bigchunk, uintptr_t increment) {
    increment += sizeof(elmm_bigchunk_t) + (sizeof(elmm_smallchunk_t) * 10); // This should more than cover the minimum overheads
    if (increment < mm->bigchunkMinimum) {
        increment = mm->bigchunkMinimum;
    }
    while ((increment % mm->bigchunkGranularity) != 0) {
        increment++;
    }
    uintptr_t oldSize = bigchunk->totalSize;
    uintptr_t newSize = oldSize + increment;
    if (mm->bigchunkFunction(newSize, bigchunk, mm->bigchunkData) != bigchunk) {
        //printf("The bigchunk function failed!\n");
        return false;
    }

    /* It might be higher than our requested size, but we should indicate failure if it's lower. */
    if (bigchunk->totalSize < newSize) {
        //printf("The bigchunk function reported success but didn't allocate enough space\n");
        return false;
    }

    /* Now we just need to calculate the address and size of the newChunk, initialise it's fields
     * and add it to the free list.
     */
    elmm_smallchunk_t* newChunk = (elmm_smallchunk_t*)(((uint8_t*)bigchunk->startAddress) + oldSize);
    //printf("The new chunk starts at %d\n", newChunk);
    newChunk->check1 = ELMM_SMALLCHUNK_FREE;
    newChunk->next = bigchunk->internals.firstFree;
    bigchunk->internals.firstFree = newChunk;
    newChunk->size = (bigchunk->totalSize - oldSize) - sizeof(elmm_smallchunk_t);
    newChunk->check2 = ELMM_SMALLCHUNK_FREE;

    //printf("Smallchunk size: %d\n", newChunk->size);

    return true;
}

/* If not enough FREE smallchunks exist within the heap, this function will be
 * called to attempt to allocate more. It should return true if successful and
 * false otherwise.
 */
ELMM_STATIC bool elmm_growheap(elmm_t* mm, uintptr_t increment) {
    //printf("elmm_growheap(%d, %d)\n", mm, increment);
    if (!elmm_checkinit(mm)) {
        return false;
    }
    if (increment > 1024 * 1024 * 1024) {
        return false;
        // TODO: Check against some better maximum incr?
    }
    while ((increment % mm->bigchunkGranularity) != 0) {
        increment++;
    }
    /* First we need to check existing chunks, if any can be resized to fit the
     * new data we should attempt that.
     */
    elmm_bigchunk_t* chunk = mm->firstChunk;
    elmm_bigchunk_t* lastValidChunk = chunk;
    while (chunk != NULL) {
        if (chunk->maximumSize >= (chunk->totalSize + increment)) {
            //printf("Looks like we can resize this chunk!\n");
            if (elmm_growinner(mm, chunk, increment)) { // Only if it works should we return now!
                return true;
            }
            /* Even if the maximum size indicates otherwise, there might be some reason the chunk
             * can't be resized, so if growinner failed we need to continue and try to allocate
             * somewhere else.
             */
        }
        lastValidChunk = chunk;
        chunk = chunk->internals.nextChunk;
    }
    //printf("Attempting to allocate a new chunk...\n");
    elmm_bigchunk_t* newChunk = elmm_allocinner(mm, increment);
    if (newChunk == NULL) {
        return false;
    }
    
    if (mm->firstChunk == NULL) { // Wouldn't normally happen, but maybe if initialisation changes
        mm->firstChunk = newChunk;
    } else {
        lastValidChunk->internals.nextChunk = newChunk;
        newChunk->internals.prevChunk = lastValidChunk;
    }
    return true;
}

/* Locks the memory manager while collecting statistics into the given array (up to at most arrayLength elements).
 * The array indices will match the ELMM_STAT_ values and the number of elements filled will be returned (zero upon
 * complete failure).
 */
ELMM_STATIC uintptr_t elmm_stat(elmm_t* mm, uintptr_t* statArray, uintptr_t arrayLength) {
    if (!elmm_checkinit(mm)) {
        return 0;
    }

    if (!mm->lockFunction(ELMM_LOCK_WAITLOCK, &mm->mainLock, mm->lockData)) {
        return 0;
    }
    void* result = NULL;

    uintptr_t i;
    for (i = 0; i < arrayLength && i < ELMM_STATTOP; i++) {
        statArray[i] = elmm_innerstat(mm, i);
    }

    if (!mm->lockFunction(ELMM_LOCK_UNLOCK, &mm->mainLock, mm->lockData)) {
        return 0;
    }

    return i;
}

/* The chunkymalloc function attempts to allocate a smaller chunk from the free list within a given bigchunk. */
ELMM_INLINE void* elmm_chunkymalloc(elmm_t* mm, elmm_bigchunk_t* bigchunk, uintptr_t size) {
    while ((size % sizeof(elmm_smallchunk_t)) != 0) {
        size++;
    }
    elmm_smallchunk_t** chunkptr = &bigchunk->internals.firstFree;
    elmm_smallchunk_t* chunk = bigchunk->internals.firstFree;
    while (chunk != NULL) {
        if (chunk->size >= size) {
            //printf("Can fit\n");
            if (chunk->size >= size + (2 * sizeof(elmm_smallchunk_t))) {
                //printf("Will split\n");
                uint8_t* rawbytes = (uint8_t*) chunk;
                elmm_smallchunk_t* upperchunk = (elmm_smallchunk_t*)(rawbytes + sizeof(elmm_smallchunk_t) + size);
                while ((((uintptr_t)upperchunk) % sizeof(elmm_smallchunk_t)) != 0) {
                    upperchunk = (elmm_smallchunk_t*) (((uintptr_t)upperchunk) + 1);
                }
                upperchunk->check1 = ELMM_SMALLCHUNK_FREE;
                uintptr_t oldsize = chunk->size;
                chunk->size = (((uintptr_t)upperchunk) - ((uintptr_t)chunk)) - sizeof(elmm_smallchunk_t);
                upperchunk->size = oldsize - (((uintptr_t)upperchunk) - ((uintptr_t)chunk));
                upperchunk->next = chunk->next;
                upperchunk->check2 = ELMM_SMALLCHUNK_FREE;
                chunk->next = upperchunk;
                //printf("Splitted one chunk of %d into two chunks of %d and %d\n", oldsize, chunk->size, upperchunk->size);
            }
            *chunkptr = chunk->next;
            chunk->check1 = ELMM_SMALLCHUNK_ALLOCATED;
            chunk->next = bigchunk->internals.firstAllocated;
            bigchunk->internals.firstAllocated = chunk;
            chunk->check2 = ELMM_SMALLCHUNK_ALLOCATED;
            return (void*)(chunk + 1);
        }
        chunkptr = &chunk->next;
        chunk = chunk->next;
    }

    //printf("No fit in chunk %d\n", bigchunk);

    /* If no chunk was found, just return NULL. */
    return NULL;
}

/* The inner malloc function attempts to allocate from any free chunks. This performs
 * most of the job of malloc, but leaves the edge cases (i.e. when we need to obtain more memory)
 * as well as locking for the outer elmm_malloc function (and leaves the hard bits to elmm_chunkymalloc).
 */
ELMM_INLINE void* elmm_innermalloc(elmm_t* mm, uintptr_t size) {
    elmm_bigchunk_t* chunk = mm->firstChunk;
    while (chunk != NULL) {
        void* result = elmm_chunkymalloc(mm, chunk, size);
        if (result != NULL) {
            return result; // Success!
        }
        chunk = chunk->internals.nextChunk;
    }
    /* If we got to the end without any being able to allocate, then we didn't find
     * any free memory in the heap.
     */
    return NULL;
}

/* The main allocation function, equivalent to malloc. */
ELMM_INLINE void* elmm_malloc(elmm_t* mm, uintptr_t size) {
    if (!elmm_checkinit(mm)) {
        return NULL;
    }

    if (!mm->lockFunction(ELMM_LOCK_WAITLOCK, &mm->mainLock, mm->lockData)) {
        return NULL;
    }
    void* result = NULL;

    /* First try innermalloc once, for the usual case where memory is already available. */
    result = elmm_innermalloc(mm, size);

    /* Only if the first allocation failed do we need to expand the heap and try again. */
    if (result == NULL) {
        //printf("innermalloc failed, trying growheap...\n");
        if (elmm_growheap(mm, size)) { /* And only if the heap has actually be grown should we try again. */
            //printf("growheap worked, trying malloc again...\n");
            result = elmm_innermalloc(mm, size);
            if (result == NULL) {
                //printf("That failed :(\n");
            } else {
                //printf("That worked :D\n");
            }
        } else {
            //printf("growheap failed\n");
        }
    }

    if (!mm->lockFunction(ELMM_LOCK_UNLOCK, &mm->mainLock, mm->lockData)) {
        return NULL;
    }

    return result;
}

/* Only used internally by elmm_free to find the bigchunk which the data at the given pointer would reside in. */
ELMM_INLINE elmm_bigchunk_t* elmm_innerchunk(elmm_t* mm, void* pointer) {
    elmm_bigchunk_t* chunk = mm->firstChunk;
    while (chunk != NULL) {
        uintptr_t iptr = (uintptr_t)pointer;
        uintptr_t cptr = (uintptr_t)chunk->startAddress;
        if (iptr >= cptr && iptr < cptr + chunk->totalSize) {
            return chunk;
        }
        chunk = chunk->internals.nextChunk;
    }
    return NULL;
}

/* Only used internally by elmm_free to unlink a smallchunk element from a list. */
ELMM_INLINE bool elmm_innerunlink(elmm_t* mm, elmm_smallchunk_t** listVariable, elmm_smallchunk_t* element) {
    if (element == *listVariable) {
        *listVariable = element->next;
        element->next = NULL;
        return true;
    }
    elmm_smallchunk_t* prevChunk = *listVariable;
    elmm_smallchunk_t* chunk = prevChunk->next;
    while (chunk != NULL) {
        if (chunk == element) {
            prevChunk->next = element->next;
            element->next = NULL;
            return true;
        }
        prevChunk = chunk;
        chunk = chunk->next;
    }
    return false;
}

/* The main deallocation function, equivalent to free (except it should always return true, otherwise
 * it has e.g. been fed a NULL value or worse).
 */
ELMM_INLINE bool elmm_free(elmm_t* mm, void* pointer) {
    if (!elmm_checkinit(mm)) {
        return false;
    }

    if (!mm->lockFunction(ELMM_LOCK_WAITLOCK, &mm->mainLock, mm->lockData)) {
        return false;
    }
    bool success = false;

    elmm_smallchunk_t* header = elmm_getheader(mm, pointer);

    if (header != NULL) {
        if (header->check1 == ELMM_SMALLCHUNK_ALLOCATED && header->check1 == header->check2) {
            elmm_bigchunk_t* bigchunk = elmm_innerchunk(mm, pointer);
            if (bigchunk != NULL) {
                if (elmm_innerunlink(mm, &bigchunk->internals.firstAllocated, header)) {
                    header->check1 = ELMM_SMALLCHUNK_FREE;
                    header->next = bigchunk->internals.firstFree;
                    header->check2 = ELMM_SMALLCHUNK_FREE;
                    bigchunk->internals.firstFree = header;
                    success = true;
                }
            }
        }
    }

    if (!mm->lockFunction(ELMM_LOCK_UNLOCK, &mm->mainLock, mm->lockData)) {
        return false;
    }
    return success;
}

/* The equivalent of the calloc function, which is built on top of the malloc implementation. */
ELMM_INLINE void* elmm_calloc(elmm_t* mm, uintptr_t count, uintptr_t elementSize) {
    if (elementSize == 3) {
        elementSize = 4;
    } else if (elementSize > 4 && elementSize < 8) {
        elementSize = 8;
    } else if (elementSize > 8 && elementSize < 16) {
        elementSize = 16;
    } else if (elementSize > 16) {
        while ((elementSize % 16) != 0) {
            elementSize++;
        }
    }
    uintptr_t size = count * elementSize;
    void* result = elmm_malloc(mm, size);

    if (result == NULL) {
        return NULL;
    } else {
        uint8_t* bytes = (uint8_t*)result;
        uintptr_t i;
        for (i = 0; i < size; i++) {
            bytes[i] = 0;
        }
        return result;
    }
}

/* The equivalent of the realloc function, which is built on top of the calloc and free implementations but also
 * uses an internal API to get the original pointer's size. */
ELMM_INLINE void* elmm_realloc(elmm_t* mm, void* pointer, uintptr_t newSize) {
    if (pointer == NULL) {
        if (newSize == 0) {
            return NULL;
        } else {
            return elmm_calloc(mm, newSize, 1);
        }
    } else if (newSize == 0) {
        elmm_free(mm, pointer);
        return NULL;
    } else {
        uintptr_t oldSize = elmm_sizeof(mm, pointer);
        void* newPointer = elmm_calloc(mm, newSize, 1);
        if (newPointer == NULL) {
            return NULL;
        }
        uint8_t* oldBytes = (uint8_t*)pointer;
        uint8_t* newBytes = (uint8_t*)newPointer;
        uintptr_t commonSize = (oldSize < newSize) ? oldSize : newSize;
        uintptr_t i;
        for (i = 0; i < commonSize; i++) {
            newBytes[i] = oldBytes[i];
        }
        elmm_free(mm, pointer);
        return newPointer;
    }
}

ELMM_INLINE intptr_t elmm_innercompact(elmm_t* mm, elmm_bigchunk_t* bigchunk) {
    intptr_t result = 0;

    elmm_smallchunk_t** chunkvar = &bigchunk->internals.firstFree;
    elmm_smallchunk_t* chunk = *chunkvar;
    while (chunk != NULL) {
        uintptr_t addr1 = (uintptr_t)chunk;
        uintptr_t addr2 = (uintptr_t)chunk->next;
        if (chunk->next != NULL && addr1 > addr2) { /* Sorting is required. */
            //printf("I'm going to sort chunks %d and %d into correct order...\n", addr1, addr2);
            *chunkvar = chunk->next;
            chunk->next = (*chunkvar)->next;
            (*chunkvar)->next = chunk;
            result++;
        } else if (addr2 == (addr1 + sizeof(elmm_smallchunk_t) + chunk->size)) {
            //printf("I'm going to compact chunks %d and %d into one chunk...\n", addr1, addr2);
            chunk->size += sizeof(elmm_smallchunk_t) + chunk->next->size;
            chunk->next = chunk->next->next;
            result++;
        }
        chunkvar = &chunk->next;
        chunk = chunk->next;
    }

    return result;
}

/* Performs one-or-more cycles of compaction, returning the total number of sorting or compaction operations performed, and stopping
 * early if it stops finding sortable or compactable entries. Returns -1 on error.
 */
ELMM_INLINE intptr_t elmm_compact(elmm_t* mm/*, uintptr_t maxCycles*/) {
    if (!elmm_checkinit(mm)) {
        return -1;
    }

    if (!mm->lockFunction(ELMM_LOCK_WAITLOCK, &mm->mainLock, mm->lockData)) {
        return 0 - 1;
    }
    intptr_t result = 0;

    elmm_bigchunk_t* chunk = mm->firstChunk;
    while (chunk != NULL) {
        intptr_t nchanges = elmm_innercompact(mm, chunk);
        result += nchanges;
        chunk = chunk->internals.nextChunk;
    }

    if (!mm->lockFunction(ELMM_LOCK_UNLOCK, &mm->mainLock, mm->lockData)) {
        return 0 - 1;
    }

    return result;
}

/* Just calls elmm_compact until either there's nothing more to compact or an error occurs. */
ELMM_INLINE intptr_t elmm_fullcompact(elmm_t* mm) {
    intptr_t result = 0;

    intptr_t tmpresult;
    while ((tmpresult = elmm_compact(mm)) > 0) {
        result += tmpresult;
    }

    if (tmpresult < 0) {
        return tmpresult;
    } else {
        return result;
    }
}

/* From ifndef at top of file: */
#endif