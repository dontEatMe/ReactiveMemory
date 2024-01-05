#include "reactivity.h"

typedef struct variableEntry {
	struct variable* variable;
	struct variableEntry* prev;
	struct variableEntry* next;
} variableEntry;

typedef struct variable {
	void* value;
	void* bufValue; // buffer for value from computed callback
	void* oldValue;
	size_t size;
	bool isComputed;
	void (*callback)(void* bufForReturnValue, void* imPointer); // pointer to compute callback
	void (*triggerCallback)(void* value, void* oldValue, void* imPointer); // pointer to trigger callback
	struct { // variables which depends on this variable
		variableEntry* tail;
		variableEntry* head;
	} observers;
	struct { // variables on which this variable depends, valid only for computed
		variableEntry* tail;
		variableEntry* head;
	} depends;
	struct variable* next; // TODO double-linked list
} variable;

typedef struct mmPage {
	void* pointer;
	struct mmPage* next;
} mmPage;

typedef struct mmBlock {
	void* imPointer;
	size_t size;
	struct { // variables on which this variable depends, valid only for computed
		mmPage* tail;
		mmPage* head;
	} pages;
} mmBlock;

typedef struct engineState {
	variable* registerComputed;
	variable* changedVariable;
	mmBlock* reactiveMem;
	REACTIVITY_MODE mode;
	struct {
		variable* tail;
		variable* head;
	} variables;
	void* (*memAlloc)(size_t size);
	void (*memFree)(void* pointer);
	void* (*memCopy)(void* destination, const void* source, size_t size);
	void* (*pagesAlloc)(size_t size);
	void (*pagesFree)(void* pointer);
	void (*pagesProtectLock)(void* pointer, size_t size);
	void (*pagesProtectUnlock)(void* pointer, size_t size);
	void (*enableTrap)(void* userData);
} engineState;

// engine data

engineState state = {
	.registerComputed = NULL, // TODO mutex
	.changedVariable = NULL,
	.reactiveMem = NULL,
	.mode = MODE_LAZY,
	.variables = {
		.tail = NULL,
		.head = NULL
	},
	.memAlloc = NULL,
	.memFree = NULL,
	.memCopy = NULL,
	.pagesAlloc = NULL,
	.pagesFree = NULL,
	.pagesProtectLock = NULL,
	.pagesProtectUnlock = NULL,
	.enableTrap = NULL
};

// engine functions

variable* getVariable(void* pointer) {
	variable* result = NULL;
	variable* variableToTest = state.variables.head;
	while (variableToTest!=NULL) {
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
void exceptionHandler(void* userData, REACTIVITY_EXCEPTION exception, bool isWrite, void* pointer) {
	if (exception == EXCEPTION_PAGEFAULT) {
		variable* realAddr = getVariable(pointer);
		if (realAddr!=NULL) {
			if (state.registerComputed != NULL) {
				if (!realAddr->isComputed) {
					// check for variable already added to depends list
					variableEntry* testDependsEntry = state.registerComputed->depends.head;
					bool variableFound = false;
					while (testDependsEntry != NULL) {
						if (testDependsEntry->variable == realAddr) {
							variableFound = true;
							break;
						}
						testDependsEntry = testDependsEntry->next;
					}
					if (!variableFound) {
						// for register computed variable (will be call multiple times for one computed variable)
						// 1. get list of static variables on which computed variable depends (by call computed callback)
						// 2. add computed observer to every static variable
						variableEntry* dependsEntry = state.memAlloc(sizeof(variableEntry)); // TODO check to state.memAlloc return NULL
						dependsEntry->variable = realAddr;
						dependsEntry->prev = NULL;
						dependsEntry->next = NULL;
						if (state.registerComputed->depends.head == NULL) {
							state.registerComputed->depends.head = dependsEntry;
							state.registerComputed->depends.tail = dependsEntry;
						} else {
							dependsEntry->prev = state.registerComputed->depends.tail;
							state.registerComputed->depends.tail->next = dependsEntry;
							state.registerComputed->depends.tail = dependsEntry;
						}
						variableEntry* observersEntry = state.memAlloc(sizeof(variableEntry)); // TODO check to state.memAlloc return NULL
						observersEntry->variable = state.registerComputed;
						observersEntry->prev = NULL;
						observersEntry->next = NULL;
						if (realAddr->observers.head == NULL) {
							realAddr->observers.head = observersEntry;
							realAddr->observers.tail = observersEntry;
						} else {
							observersEntry->prev = realAddr->observers.tail;
							realAddr->observers.tail->next = observersEntry;
							realAddr->observers.tail = observersEntry;
						}
					}
				}
			} else {
				// lazy calculation, only on read
				if (!isWrite) { // read
					if (realAddr->isComputed) {
						state.pagesProtectLock(state.reactiveMem->imPointer, state.reactiveMem->size);
						realAddr->callback(realAddr->bufValue, state.reactiveMem->imPointer);
						state.pagesProtectUnlock(state.reactiveMem->imPointer, state.reactiveMem->size);
						state.memCopy(realAddr->value, realAddr->bufValue, realAddr->size);
					}
				} else { // write
					state.changedVariable = realAddr;
					// save old value for ref variable
					state.memCopy(state.changedVariable->oldValue, state.changedVariable->value, state.changedVariable->size);
				}
			}
			state.enableTrap(userData); // trap flag for get exception after memory access instruction
			//printf("EXCEPTION_PAGEFAULT\n");
		}
	}
	else if (exception == EXCEPTION_DEBUG) {
		state.pagesProtectLock(state.reactiveMem->imPointer, state.reactiveMem->size);
		if (state.changedVariable!=NULL) {
			variable* refVariable = state.changedVariable;
			state.changedVariable = NULL;
			variableEntry* compEntryToFree = NULL;
			variableEntry* compEntry = refVariable->observers.head;
			refVariable->observers.head = NULL;
			refVariable->observers.tail = NULL;
			if (refVariable->triggerCallback!=NULL) {
				refVariable->triggerCallback(refVariable->value, refVariable->oldValue, state.reactiveMem->imPointer);
			}
			while (compEntry!=NULL) {
				// save old value for computed variable
				// TODO MODE_LAZY
				state.pagesProtectUnlock(state.reactiveMem->imPointer, state.reactiveMem->size);
				state.memCopy(compEntry->variable->oldValue, compEntry->variable->value, compEntry->variable->size);
				state.pagesProtectLock(state.reactiveMem->imPointer, state.reactiveMem->size);
				// update computed variable depends
				// free depends list
				variableEntry* variableEntryToFree = NULL;
				variableEntry* nextVariableEntry = compEntry->variable->depends.head;
				while (nextVariableEntry!=NULL) {
					variableEntryToFree = nextVariableEntry;
					nextVariableEntry = nextVariableEntry->next;
					// find and remove it from depend variable observers list
					variableEntry* observerVariableEntryToFree = NULL;
					variableEntry* observerNextVariableEntry = variableEntryToFree->variable->observers.head;
					while (observerNextVariableEntry!=NULL) {
						if (observerNextVariableEntry->variable == compEntry->variable) {
							if (observerNextVariableEntry->prev!=NULL) {
								observerNextVariableEntry->prev->next = observerNextVariableEntry->next;
								if (observerNextVariableEntry->next==NULL) { // this is last element
									variableEntryToFree->variable->observers.tail = observerNextVariableEntry->prev;
								}
							} else { // this is first element
								if (observerNextVariableEntry->next!=NULL) {
									variableEntryToFree->variable->observers.head = observerNextVariableEntry->next;
								} else { // only one element
									variableEntryToFree->variable->observers.head = NULL;
									variableEntryToFree->variable->observers.tail = NULL;
								}
							}
							if (observerNextVariableEntry->next!=NULL) {
								observerNextVariableEntry->next->prev = observerNextVariableEntry->prev;
							}
							state.memFree(observerNextVariableEntry);
							break;
						}
						observerNextVariableEntry = observerNextVariableEntry->next;
					}
					state.memFree(variableEntryToFree);
				}
				compEntry->variable->depends.head = NULL;
				compEntry->variable->depends.tail = NULL;
				state.registerComputed = compEntry->variable;
				compEntry->variable->callback(compEntry->variable->bufValue, state.reactiveMem->imPointer);
				state.registerComputed = NULL;
				state.pagesProtectUnlock(state.reactiveMem->imPointer, state.reactiveMem->size);
				state.memCopy(compEntry->variable->value, compEntry->variable->bufValue, compEntry->variable->size);
				state.pagesProtectLock(state.reactiveMem->imPointer, state.reactiveMem->size);
				if (compEntry->variable->triggerCallback!=NULL) {
					compEntry->variable->triggerCallback(compEntry->variable->value, compEntry->variable->oldValue, state.reactiveMem->imPointer);
				}
				compEntryToFree = compEntry;
				compEntry = compEntry->next;
				state.memFree(compEntryToFree);
			}
		}
		//printf("EXCEPTION_DEBUG\n");
	}
}

variable* createVariable(void* pointer, size_t size) {
	variable* var = state.memAlloc(sizeof(variable));
	var->isComputed = false;
	var->callback = NULL;
	var->triggerCallback = NULL;
	var->observers.head = NULL;
	var->observers.tail = NULL;
	var->depends.head = NULL;
	var->depends.tail = NULL;
	var->next = NULL;
	var->bufValue = state.memAlloc(size);
	var->oldValue = state.memAlloc(size);
	var->value = pointer;
	var->size = size;
	if (state.variables.head == NULL) {
		state.variables.head = var;
		state.variables.tail = var;
	} else {
		state.variables.tail->next = var;
		state.variables.tail = var;
	}
	return var;
}

void ref(void* pointer, size_t size) {
	variable* var = createVariable(pointer, size);
}

void computed(void* pointer, size_t size, void (*callback)(void* bufForReturnValue, void* imPointer)) {
	variable* var = createVariable(pointer, size);
	var->isComputed = true;
	var->callback = callback;
	state.registerComputed = var;
	var->callback(var->bufValue, state.reactiveMem->imPointer); // call computed callback for #PF and enum depends for computed and observers for refs in #PF handler routine
	state.registerComputed = NULL;
}

void watch(void* pointer, void (*triggerCallback)(void* value, void* oldValue, void* imPointer)) {
	variable* variable = getVariable(pointer);
	variable->triggerCallback = triggerCallback;
}

void* reactiveAlloc(size_t memSize) {
	mmBlock* block = state.memAlloc(sizeof(mmBlock));
	block->imPointer = state.pagesAlloc(memSize); // guard pages
	block->pages.head = NULL;
	block->pages.tail = NULL;
	size_t pagesCount = (memSize+(4096-1))/4096; // 4096 is default page size for x86/x64
	mmPage* page = NULL;
	for (size_t i=0; i<pagesCount; i++) {
		mmPage* prevPage = page;
		page = state.memAlloc(sizeof(mmPage));
		if (i==0) {
			block->pages.head = page;
		} else {
			prevPage->next = page;
		}
		page->pointer = (size_t)block->imPointer + i*4096;
		page->next = NULL;
	}
	block->pages.tail = page;
	block->size = memSize;
	state.reactiveMem = block;
	return block->imPointer;
}

void reactiveFree(void* memPointer) {
	state.pagesFree(memPointer);
	// free pages descriptors
	mmPage* pageDescriptorToFree = NULL;
	mmPage* nextPageDescriptor = state.reactiveMem->pages.head;
	while (nextPageDescriptor!=NULL) {
		pageDescriptorToFree = nextPageDescriptor;
		nextPageDescriptor = nextPageDescriptor->next;
		state.memFree(pageDescriptorToFree);
	}
	state.memFree(state.reactiveMem);
	state.reactiveMem = NULL;
}

void initReactivity(REACTIVITY_MODE mode, void* (*memAlloc)(size_t size), void (*memFree)(void* pointer), void* (*memCopy)(void* destination, const void* source, size_t size), void* (*pagesAlloc)(size_t size), void (*pagesFree)(void* pointer), void (*pagesProtectLock)(void* pointer, size_t size), void (*pagesProtectUnlock)(void* pointer, size_t size), void (*enableTrap)(void* userData)) {
	state.mode = mode;
	state.memAlloc = memAlloc;
	state.memFree = memFree;
	state.memCopy = memCopy;
	state.pagesAlloc = pagesAlloc;
	state.pagesFree = pagesFree;
	state.pagesProtectLock = pagesProtectLock;
	state.pagesProtectUnlock = pagesProtectUnlock;
	state.enableTrap = enableTrap;
}

void freeReactivity() {
	// free variables
	variable* variableToFree = NULL;
	variable* nextVariable = state.variables.head;
	while (nextVariable!=NULL) {
		variableToFree = nextVariable;
		nextVariable = nextVariable->next;
		variableEntry* variableEntryToFree;
		variableEntry* nextVariableEntry;
		// free observers list
		variableEntryToFree = NULL;
		nextVariableEntry = variableToFree->observers.head;
		while (nextVariableEntry!=NULL) {
			variableEntryToFree = nextVariableEntry;
			nextVariableEntry = nextVariableEntry->next;
			state.memFree(variableEntryToFree);
		}
		variableToFree->observers.head = NULL;
		variableToFree->observers.tail = NULL;
		// free depends list
		variableEntryToFree = NULL;
		nextVariableEntry = variableToFree->depends.head;
		while (nextVariableEntry!=NULL) {
			variableEntryToFree = nextVariableEntry;
			nextVariableEntry = nextVariableEntry->next;
			state.memFree(variableEntryToFree);
		}
		variableToFree->depends.head = NULL;
		variableToFree->depends.tail = NULL;
		state.memFree(variableToFree->bufValue);
		state.memFree(variableToFree->oldValue);
		state.memFree(variableToFree);
	}
	state.variables.head = NULL;
	state.variables.tail = NULL;
	state.memAlloc = NULL;
	state.memFree = NULL;
	state.memCopy = NULL;
	state.pagesAlloc = NULL;
	state.pagesFree = NULL;
	state.pagesProtectLock = NULL;
	state.pagesProtectUnlock = NULL;
	state.enableTrap = NULL;
}