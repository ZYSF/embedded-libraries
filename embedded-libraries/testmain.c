/* This is just a test program for my embedded libraries (which, at the moment, mostly consists of a memory manager. */
#include <stdio.h>
#include <stdlib.h>
#include "elmm.h"
#include "elprintf.h"

typedef bool (*testf_t)(const char* testname);

uint8_t* bigarray = NULL;
uintptr_t bigarraytop = 0;

void* ezsbrk(intptr_t incr, void* udata) {
	printf("ezsbrk(%d,%d)\n", incr, udata);
	if (bigarray == NULL) {
		/* Use the system's malloc to allocate a few MB for our heap.
		 * (A simple static array can be used instead, but since you probably have a malloc
		 * function already there's no point not using it to allocate memory for the custom
		 * malloc.)
		 */
		bigarray = malloc(1024 * 1024 * 4);
		if (bigarray == NULL) {
			return ELMM_SBRK_ERROR;
		}
		bigarraytop = 0;
	}
	
	if (bigarraytop + incr >= (1024 * 1024 * 4)) {
		printf("OUT OF EZSBRK MEMORY\n");
		return ELMM_SBRK_ERROR;
	}

	void* result = bigarray + bigarraytop;
	bigarraytop += incr;

	printf("GIVING %d MEMORY TO MM at %d\n", incr, result);

	bigarray[0] = 'f';

	return result;
}

void printstats(elmm_t* mm) {
	uintptr_t stats[ELMM_STATTOP];
	uintptr_t nstats = elmm_stat(mm, stats, ELMM_STATTOP);
	if (nstats != ELMM_STATTOP) {
		printf("STATS FAILED!\n");
	}
	printf("TOTAL:     %d\n", stats[ELMM_STAT_TOTAL]);
	printf("ALLOCATED: %d\n", stats[ELMM_STAT_ALLOCATED]);
	printf("FREE:      %d\n", stats[ELMM_STAT_FREE]);
	printf("OVERHEADS: %d\n", stats[ELMM_STAT_OVERHEADS]);
}

bool test_basicmem(const char* testname) {
	bigarray = NULL;
	bigarraytop = 0;
	elmm_t mm;
	elmm_sbrk_data_t sbrkdata;
	sbrkdata.func = &ezsbrk;
	sbrkdata.max = 1024 * 1024 * 4;
	sbrkdata.onlyChunk = NULL;
	sbrkdata.udata = NULL;
	mm.bigchunkMinimum = 1024 * 1024;
	mm.bigchunkGranularity = 1024;
	mm.initialised = false;
	mm.lockFunction = NULL;
	mm.bigchunkData = &sbrkdata;
	mm.bigchunkFunction = &elmm_bigchunk_sbrk;
	if (!elmm_init(&mm)) {
		return false;
	}

	printstats(&mm);


	void* somemem = elmm_malloc(&mm, 123);
	if (somemem == NULL) {
		return false;
	}

	printstats(&mm);

	void* somemem2 = elmm_malloc(&mm, 123);
	if (somemem2 == NULL) {
		return false;
	}


	printstats(&mm);

	if (!elmm_free(&mm, somemem)) {
		return false;
	}


	printstats(&mm);

	if (!elmm_free(&mm, somemem2)) {
		return false;
	}

	printstats(&mm);

	if (elmm_fullcompact(&mm) < 0) {
		return false;
	}

	printstats(&mm);

	/*
	if (!elmm_cleanup(&mm)) {
		return false;
	}
	*/

	return true;
}

bool runtest(const char* name, testf_t function) {
	printf("Running test '%s'...\n", name);
	bool result = function(name);
	printf("Result: %s '%s'.\n", result ? "passed" : "FAILED", name);
	return result;
}

int main(int argc, char** argv) {
	printf("Some embedded library tests...\n");

	runtest("basicmem", &test_basicmem);

	printf("ALL DONE.\n");
	return 0;
}