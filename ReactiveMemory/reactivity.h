#ifndef REACTIVITY_H
#define REACTIVITY_H

#define memAlloc malloc
#define memRealloc realloc
#define memFree free
#define memCopy memcpy
// #define THREADSAFE

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef THREADSAFE
	#include <threads.h>
#endif

// TODO memory manager
// TODO thread safety
// TODO RM_MODE_NONLAZY

// RM_MODE_LAZY
//  calculate computed variables only on read
// RM_MODE_NONLAZY:
//  calculate computed variables if variables on which computed variable depends changed
//  1. on register computed variable save all addresses of variables (static and/or computed) used in the calculation process by handling access to them
//  2. on every change static variable value check dependent of this variable computed variables and recalculate it
//  3. callbacks of the computed variables must not be manually changed

typedef enum RM_MODE {
	RM_MODE_LAZY = 0,
	RM_MODE_NONLAZY = 1
} RM_MODE;

typedef enum RM_EXCEPTION {
	RM_EXCEPTION_PAGEFAULT = 0,
	RM_EXCEPTION_DEBUG = 1
} RM_EXCEPTION;

typedef enum RM_STATUS {
	RM_STATUS_SUCCESS = 0,
	RM_STATUS_FAIL = 1
} RM_STATUS;

extern RM_STATUS ref(void* pointer, size_t size);
extern RM_STATUS computed(void* pointer, size_t size, void (*callback)(void* bufForReturnValue, void* imPointer));
extern void watch(void* pointer, void (*triggerCallback)(void* value, void* oldValue, void* imPointer));
extern void* reactiveAlloc(size_t memSize);
extern void reactiveFree(void* memPointer);
extern RM_STATUS initReactivity(RM_MODE mode, void* (*pagesAlloc)(size_t size), void (*pagesFree)(void* pointer), void (*pagesProtectLock)(void* pointer, size_t size), void (*pagesProtectUnlock)(void* pointer, size_t size), void (*enableTrap)(void* userData));
extern void freeReactivity();
extern void exceptionHandler(void* userData, RM_EXCEPTION exception, bool isWrite, void* pointer);

#endif