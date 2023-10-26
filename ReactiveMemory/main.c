#include <stdio.h>
#include <stdint.h>
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

typedef struct list {
	struct varEntry* tail;
	struct varEntry* head;
} list;

typedef struct varEntry {
	struct field* variable;
	struct varEntry* next;
} varEntry;

typedef struct observerEntry {
	struct observer* observer;
	struct observerEntry* next;
} observerEntry;

typedef struct field {
	uint32_t* value;
	uint32_t oldValue;
	BOOL isComputed;
	uint32_t (*callback)(void* imPointer); // pointer to compute callback
	struct { // variables on which this observer depends
		struct observerEntry* tail;
		struct observerEntry* head;
	} observers;
	struct field* next; // TODO double-linked list
} field;

typedef struct observer {
	field* pointer; // variable to observe
	void (*triggerCallback)(uint32_t* value, void* imPointer); // pointer to trigger callback
	struct { // variables on which this observer depends
		struct varEntry* tail;
		struct varEntry* head;
	} depends;
	struct observer* next; // TODO double-linked list
} observer;

typedef struct mmBlock {
	void* imPointer;
	size_t size;
} mmBlock;

typedef struct engineState {
	observer* registerObserver;
	field* changedField;
	mmBlock* reactiveMem;
	PVOID exHandler;
	REACTIVITY_MODE mode;
	struct {
		struct observer* tail;
		struct observer* head;
	} observers;
	struct {
		struct field* tail;
		struct field* head;
	} variables;
} engineState;

// user structures

typedef struct someStruct {
	uint32_t field1;
	uint32_t field2;
	uint32_t field3;
} someStruct;

// engine data

engineState state = {
	.registerObserver = NULL, // TODO mutex
	.changedField = NULL,
	.reactiveMem = NULL,
	.exHandler = NULL,
	.mode = MODE_LAZY,
	.observers = {
		.tail = NULL,
		.head = NULL
	},
	.variables = {
		.tail = NULL,
		.head = NULL
	}
};

// engine functions

field* getVariable(void* pointer) {
	field* result = NULL;
	field* fieldToTest = state.variables.head;
	while(fieldToTest!=NULL) {
		if (fieldToTest->value == pointer) {
			result = fieldToTest;
			break;
		}
		fieldToTest = fieldToTest->next;
	}
	return result;
}

LONG NTAPI imExeption(PEXCEPTION_POINTERS ExceptionInfo) {
	DWORD oldProtect;
	if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		field* realAddr = getVariable((void*)ExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
		if (realAddr!=NULL) {
			VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
			BOOL isComputed = realAddr->isComputed;
			uint32_t (*computedCallback)(void* imPointer) = realAddr->callback;
			VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
			if (state.registerObserver != NULL) {
				if (isComputed == FALSE) {
					varEntry* entry = malloc(sizeof(varEntry)); // TODO check to malloc return NULL
					entry->variable = realAddr;
					entry->next = NULL;
					if (state.registerObserver->depends.head == NULL) {
						state.registerObserver->depends.head = entry;
						state.registerObserver->depends.tail = entry;
					} else {
						state.registerObserver->depends.tail->next = entry;
						state.registerObserver->depends.tail = entry;
					}
				}
			} else {
				// lazy calculation, only on read
				if (ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 0) { // read
					if (isComputed) {
						uint32_t value = computedCallback(state.reactiveMem->imPointer);
						VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
						*realAddr->value = value;
						VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
					}
				} else if (ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 1) { // write
					state.changedField = realAddr;
				}
			}
			VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
			ExceptionInfo->ContextRecord->EFlags |= 0x00000100; // trap flag for get exception after memory access instruction
			//printf("EXCEPTION_ACCESS_VIOLATION\n");
		}
	}
	else if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
		if (state.changedField!=NULL) {
			VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
			observerEntry* obsEntry = state.changedField->observers.head;
			VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
			state.changedField = NULL;
			while (obsEntry!=NULL) {
				// TODO pass old value and new value
				obsEntry->observer->triggerCallback(obsEntry->observer->pointer->value, state.reactiveMem->imPointer);
				obsEntry = obsEntry->next;
			}
		}
		else {
			VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
		}
		//printf("EXCEPTION_SINGLE_STEP\n");
	}
	return EXCEPTION_CONTINUE_EXECUTION;
}

void ref(uint32_t* pointer) {
	DWORD oldProtect;
	field* variable = malloc(sizeof(field));
	variable->isComputed = FALSE;
	variable->callback = NULL;
	variable->observers.head = NULL;
	variable->observers.tail = NULL;
	variable->next = NULL;
	VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
	variable->oldValue = *pointer;
	VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
	variable->value = pointer;
	if (state.variables.head == NULL) {
		state.variables.head = variable;
		state.variables.tail = variable;
	} else {
		state.variables.tail->next = variable;
		state.variables.tail = variable;
	}
}

void computed(uint32_t* pointer, uint32_t (*callback)(void* imPointer)) {
	DWORD oldProtect;
	field* variable = malloc(sizeof(field));
	variable->isComputed = TRUE;
	variable->callback = callback;
	variable->observers.head = NULL;
	variable->observers.tail = NULL;
	variable->next = NULL;
	VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
	variable->oldValue = *pointer;
	VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
	variable->value = pointer;
	if (state.variables.head == NULL) {
		state.variables.head = variable;
		state.variables.tail = variable;
	} else {
		state.variables.tail->next = variable;
		state.variables.tail = variable;
	}
}

void watch(uint32_t* pointer, void (*triggerCallback)(uint32_t* value, void* imPointer)) {
	// for register watch() callback
	// 1. get list of static variables on which observer variable depends (by call watch callback)
	// 2. add watch callback to observers list of this variables
	field* variable = getVariable(pointer);
	observer* obs = malloc(sizeof(observer));
	obs->depends.head = NULL;
	obs->depends.tail = NULL;
	obs->pointer = variable;
	obs->triggerCallback = triggerCallback;
	obs->next = NULL;
	if (state.observers.head == NULL) {
		state.observers.head = obs;
		state.observers.tail = obs;
	} else {
		state.observers.tail->next = obs;
		state.observers.tail = obs;
	}
	state.registerObserver = obs;
	DWORD oldProtect;
	uint32_t (*computedCallback)(void* imPointer) = variable->callback;
	BOOL isComputed = variable->isComputed;
	if (isComputed) {
		computedCallback(state.reactiveMem->imPointer);
	} else {
		uint32_t buf = *variable->value;
	}
	varEntry* entry = obs->depends.head;
	while (entry!=NULL) {
		observerEntry* obsEntry = malloc(sizeof(observerEntry)); // TODO free() and check to malloc return NULL
		obsEntry->observer = obs;
		obsEntry->next = NULL;
		if (entry->variable->observers.head == NULL) {
			entry->variable->observers.head = obsEntry;
			entry->variable->observers.tail = obsEntry;
		} else {
			entry->variable->observers.tail->next = obsEntry;
			entry->variable->observers.tail = obsEntry;
		}
		entry = entry->next;
	}
	state.registerObserver = NULL;
}

void* reactiveAlloc(size_t memSize) {
	mmBlock* block = malloc(sizeof(mmBlock));
	block->imPointer = VirtualAlloc(NULL, memSize, MEM_COMMIT, PAGE_NOACCESS); // imaginary pages
	block->size = memSize;
	state.reactiveMem = block;
	return block->imPointer;
}

void reactiveFree(void* memPointer) {
	VirtualFree(memPointer, 0, MEM_RELEASE);
	free(state.reactiveMem);
	state.reactiveMem = NULL;
}

void initReactivity(REACTIVITY_MODE mode) {
	state.mode = mode;
	state.exHandler = AddVectoredExceptionHandler(1, imExeption);
}

void freeReactivity() {
	RemoveVectoredExceptionHandler(state.exHandler);
	state.exHandler = NULL;
	// free observers
	observer* obsToFree = NULL;
	observer* nextObs = state.observers.head;
	while (nextObs!=NULL) {
		obsToFree = nextObs;
		nextObs = nextObs->next;
		varEntry* varEntryToFree = NULL;
		varEntry* nextVarEntry = obsToFree->depends.head;
		while (nextVarEntry!=NULL) {
			varEntryToFree = nextVarEntry;
			nextVarEntry = nextVarEntry->next;
			free(varEntryToFree);
		}
		free(obsToFree);
	}
	state.observers.head = NULL;
	state.observers.tail = NULL;
	// free variables
	field* variableToFree = NULL;
	field* nextVariable = state.variables.head;
	while (nextVariable!=NULL) {
		variableToFree = nextVariable;
		nextVariable = nextVariable->next;
		observerEntry* observerEntryToFree = NULL;
		observerEntry* nextObserverEntry = variableToFree->observers.head;
		while (nextObserverEntry!=NULL) {
			observerEntryToFree = nextObserverEntry;
			nextObserverEntry = nextObserverEntry->next;
			free(observerEntryToFree);
		}
		free(variableToFree);
	}
	state.variables.head = NULL;
	state.variables.tail = NULL;
}

// user functions

uint32_t computedField2(someStruct* someStruct) {
	return someStruct->field1 + 2;
}

uint32_t computedField3(someStruct* someStruct) {
	return someStruct->field2 + someStruct->field1;
}

void triggerCallback(uint32_t* variable, void* imPointer) {
	someStruct* someStruct = imPointer;
	printf("[trigger] watch value: %d, field1 value: %d\n", *variable, someStruct->field1);
}

int main() {
	printf("reactive memory app\n");

	initReactivity(MODE_NONLAZY);
	someStruct* someStruct = reactiveAlloc(sizeof(struct someStruct));

	ref(&someStruct->field1);
	computed(&someStruct->field2, computedField2);
	computed(&someStruct->field3, computedField3);

	someStruct->field1 = 0;
	
	printf("field1: %d, field2: %d, field3: %d\n", someStruct->field1, someStruct->field2, someStruct->field3);

	watch(&someStruct->field3, triggerCallback);

	someStruct->field1 = 77;
	someStruct->field1 = 79;
	
	printf("field1: %d, field2: %d, field3: %d\n", someStruct->field1, someStruct->field2, someStruct->field3);

	reactiveFree(someStruct);
	freeReactivity();

	getchar();
}