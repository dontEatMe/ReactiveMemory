#ifndef REACTIVITY_H
#define REACTIVITY_H

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
// TODO MODE_NONLAZY


typedef enum REACTIVITY_MODE {
	MODE_LAZY = 0,
	MODE_NONLAZY = 1
} REACTIVITY_MODE;

// MODE_LAZY
//  calculate computed variables only on read
// MODE_NONLAZY:
//  calculate computed variables if variables on which computed variable depends changed
//  1. on register computed variable save all addresses of variables (static and/or computed) used in the calculation process by handling access to them
//  2. on every change static variable value check dependent of this variable computed variables and recalculate it
//  3. callbacks of the computed variables must not be manually changed

typedef enum REACTIVITY_EXCEPTION {
	EXCEPTION_PAGEFAULT = 0,
	EXCEPTION_DEBUG = 1
} REACTIVITY_EXCEPTION;

extern void ref(void* pointer, size_t size);
extern void computed(void* pointer, size_t size, void (*callback)(void* bufForReturnValue, void* imPointer));
extern void watch(void* pointer, void (*triggerCallback)(void* value, void* oldValue, void* imPointer));
extern void* reactiveAlloc(size_t memSize);
extern void reactiveFree(void* memPointer);
extern void initReactivity(REACTIVITY_MODE mode, void* (*pagesAlloc)(size_t size), void (*pagesFree)(void* pointer), void (*pagesProtectLock)(void* pointer, size_t size), void (*pagesProtectUnlock)(void* pointer, size_t size), void (*enableTrap)(void* userData));
extern void freeReactivity();
extern void exceptionHandler(void* userData, REACTIVITY_EXCEPTION exception, bool isWrite, void* pointer);

#endif