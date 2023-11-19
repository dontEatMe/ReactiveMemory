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
	PVOID exHandler;
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

// user structures

typedef struct someSubStruct {
	uint32_t field1;
	uint32_t field2;
	uint32_t field3;
} someSubStruct;

typedef struct someComputedSubStruct {
	uint32_t field1;
	uint32_t field2;
	uint32_t field3;
} someComputedSubStruct;

typedef struct someStruct {
	uint32_t field1;
	someComputedSubStruct field2;
	uint32_t field3;
	someSubStruct field4;
} someStruct;

// engine data

engineState state = {
	.registerObserver = NULL, // TODO mutex
	.changedVariable = NULL,
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

variable* getVariable(void* pointer) {
	variable* result = NULL;
	variable* variableToTest = state.variables.head;
	while(variableToTest!=NULL) {
		// strict inequality in second case for last byte
		if (((size_t)variableToTest->value <= (size_t)pointer) && ((size_t)pointer < (size_t)variableToTest->value+variableToTest->size)) {
			result = variableToTest;
			break;
		}
		variableToTest = variableToTest->next;
	}
	return result;
}

// if we run in kernel mode we can isolate reactive memory to kernel space to prevent write to it from user mode process

LONG NTAPI imExeption(PEXCEPTION_POINTERS ExceptionInfo) {
	DWORD oldProtect;
	if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		variable* realAddr = getVariable((void*)ExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
		if (realAddr!=NULL) {
			if (state.registerObserver != NULL) {
				if (!realAddr->isComputed) {
					variableEntry* entry = malloc(sizeof(variableEntry)); // TODO check to malloc return NULL
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
					if (realAddr->isComputed) {
						void* value = malloc(realAddr->size);
						realAddr->callback(value, state.reactiveMem->imPointer);
						VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
						memcpy(realAddr->value, value, realAddr->size);
						VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
						free(value);
					}
				} else if (ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 1) { // write
					state.changedVariable = realAddr;
				}
			}
			VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
			ExceptionInfo->ContextRecord->EFlags |= 0x00000100; // trap flag for get exception after memory access instruction
			//printf("EXCEPTION_ACCESS_VIOLATION\n");
		}
	}
	else if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
		VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
		if (state.changedVariable!=NULL) {
			observerEntry* obsEntry = state.changedVariable->observers.head;
			state.changedVariable = NULL;
			while (obsEntry!=NULL) {
				// TODO pass old value and new value
				// TODO do not call computed callback here in NONLAZY_MODE
				if (obsEntry->observer->variable->isComputed) {
					void* value = malloc(obsEntry->observer->variable->size);
					obsEntry->observer->variable->callback(value, state.reactiveMem->imPointer);
					VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
					memcpy(obsEntry->observer->variable->value, value, obsEntry->observer->variable->size);
					VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
					free(value);
				}
				obsEntry->observer->triggerCallback(obsEntry->observer->variable->value, state.reactiveMem->imPointer);
				obsEntry = obsEntry->next;
			}
		}
		//printf("EXCEPTION_SINGLE_STEP\n");
	}
	return EXCEPTION_CONTINUE_EXECUTION;
}

void ref(void* pointer, size_t size) {
	DWORD oldProtect;
	variable* var = malloc(sizeof(variable));
	var->isComputed = false;
	var->callback = NULL;
	var->observers.head = NULL;
	var->observers.tail = NULL;
	var->next = NULL;
	//VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
	//var->oldValue = *pointer;
	//VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
	var->value = pointer;
	var->size = size;
	if (state.variables.head == NULL) {
		state.variables.head = var;
		state.variables.tail = var;
	} else {
		state.variables.tail->next = var;
		state.variables.tail = var;
	}
}

void computed(void* pointer, size_t size, uint32_t (*callback)(void* imPointer)) {
	DWORD oldProtect;
	variable* var = malloc(sizeof(variable));
	var->isComputed = true;
	var->callback = callback;
	var->observers.head = NULL;
	var->observers.tail = NULL;
	var->next = NULL;
	//VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_READWRITE, &oldProtect);
	//var->oldValue = *pointer;
	//VirtualProtect(state.reactiveMem->imPointer, state.reactiveMem->size, PAGE_NOACCESS, &oldProtect);
	var->value = pointer;
	var->size = size;
	if (state.variables.head == NULL) {
		state.variables.head = var;
		state.variables.tail = var;
	} else {
		state.variables.tail->next = var;
		state.variables.tail = var;
	}
}

void watch(void* pointer, void (*triggerCallback)(void* value, void* imPointer)) {
	// for register watch() callback
	// 1. get list of static variables on which observer variable depends (by call watch callback)
	// 2. add watch callback to observers list of this variables
	variable* variable = getVariable(pointer);
	observer* obs = malloc(sizeof(observer));
	obs->depends.head = NULL;
	obs->depends.tail = NULL;
	obs->variable = variable;
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
	uint32_t (*computedCallback)(void* imPointer) = variable->callback;
	bool isComputed = variable->isComputed;
	if (isComputed) {
		void* value = malloc(variable->size);
		computedCallback(value, state.reactiveMem->imPointer); // call computed callback for #PF and enum observer depends in #PF handler routine
		free(value);
	} else {
		uint8_t buf = *(uint8_t*)variable->value; // read byte for #PF and enum observer depends in #PF handler routine
	}
	variableEntry* entry = obs->depends.head;
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
	block->imPointer = VirtualAlloc(NULL, memSize, MEM_COMMIT|MEM_RESERVE, PAGE_NOACCESS); // imaginary pages
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
		variableEntry* variableEntryToFree = NULL;
		variableEntry* nextvariableEntry = obsToFree->depends.head;
		while (nextvariableEntry!=NULL) {
			variableEntryToFree = nextvariableEntry;
			nextvariableEntry = nextvariableEntry->next;
			free(variableEntryToFree);
		}
		free(obsToFree);
	}
	state.observers.head = NULL;
	state.observers.tail = NULL;
	// free variables
	variable* variableToFree = NULL;
	variable* nextVariable = state.variables.head;
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

void computedField2(someComputedSubStruct* bufForReturnValue, someStruct* someStruct) {
	bufForReturnValue->field1 = 1;
	bufForReturnValue->field2 = someStruct->field1 + 2;
	bufForReturnValue->field3 = 3;
}

void computedField3(uint32_t* bufForReturnValue, someStruct* someStruct) {
	*bufForReturnValue = someStruct->field2.field2 + someStruct->field1;
}

void triggerCallback1(uint32_t* variable, someStruct* someStruct) {
	printf("[trigger1] watch value (field3): %u, field3 value: %u, field1 value: %u\n", *variable, someStruct->field3, someStruct->field1);
}
void triggerCallback2(someSubStruct* variable, someStruct* someStruct) {
	printf("[trigger2] watch value (field4.field3): %u, field1 value: %u\n", variable->field3, someStruct->field1);
}

int main() {
	printf("reactive memory app\n");

	initReactivity(MODE_NONLAZY);
	someStruct* someStruct = reactiveAlloc(sizeof(struct someStruct));

	ref(&someStruct->field1, sizeof(someStruct->field1));
	computed(&someStruct->field2, sizeof(someStruct->field2), computedField2);
	computed(&someStruct->field3, sizeof(someStruct->field3), computedField3);
	ref(&someStruct->field4, sizeof(someStruct->field4));

	someStruct->field1 = 0;
	
	printf("field1: %u, field2.field2: %u, field3: %u\n", someStruct->field1, someStruct->field2.field2, someStruct->field3);
	
	watch(&someStruct->field3, triggerCallback1);
	watch(&someStruct->field4, triggerCallback2);

	someStruct->field1 = 77;
	someStruct->field1 = 79;
	someStruct->field4.field3 = 5;
	
	printf("field1: %u, field2.field2: %u, field3: %u\n", someStruct->field1, someStruct->field2.field2, someStruct->field3);

	reactiveFree(someStruct);
	freeReactivity();

	getchar();
}