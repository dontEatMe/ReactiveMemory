#ifndef REACTIVITY_H
#define REACTIVITY_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <windows.h>

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

typedef struct variableEntry {
	struct variable* variable;
	struct variableEntry* next;
} variableEntry;

typedef struct observerEntry {
	struct observer* observer;
	struct observerEntry* next;
} observerEntry;

typedef struct variable {
	void* value;
	size_t size;
	//uint32_t oldValue;
	bool isComputed;
	void (*callback)(void* bufForReturnValue, void* imPointer); // pointer to compute callback
	struct {
		observerEntry* tail;
		observerEntry* head;
	} observers;
	struct variable* next; // TODO double-linked list
} variable;

typedef struct observer {
	variable* variable; // variable to observe
	void (*triggerCallback)(void* value, void* imPointer); // pointer to trigger callback
	struct { // variables on which this observer depends
		variableEntry* tail;
		variableEntry* head;
	} depends;
	struct observer* next; // TODO double-linked list
} observer;

typedef struct mmBlock {
	void* imPointer;
	size_t size;
} mmBlock;

typedef struct engineState {
	observer* registerObserver;
	variable* changedVariable;
	mmBlock* reactiveMem;
	void* exHandler;
	REACTIVITY_MODE mode;
	struct {
		observer* tail;
		observer* head;
	} observers;
	struct {
		variable* tail;
		variable* head;
	} variables;
} engineState;

extern void ref(void* pointer, size_t size);
extern void computed(void* pointer, size_t size, void (*callback)(void* bufForReturnValue, void* imPointer));
extern void watch(void* pointer, void (*triggerCallback)(void* value, void* imPointer));
extern void* reactiveAlloc(size_t memSize);
extern void reactiveFree(void* memPointer);
extern void initReactivity(REACTIVITY_MODE mode);
extern void freeReactivity();

#endif