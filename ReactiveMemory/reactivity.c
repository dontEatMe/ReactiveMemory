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
	struct { // variables which whole or part located on this page
		variableEntry* tail;
		variableEntry* head;
	} dependents;
} mmPage;

typedef struct mmBlock {
	mtx_t mutex;
	void* imPointer;
	size_t size;
	mmPage* pages; // array of mmPage
	size_t pagesCount;
} mmBlock;

typedef struct engineState {
	variable* registerComputed;
	variable** changedVariables; // array of variable*
	size_t changedVariablesCount;
	mmBlock* reactiveMem;
	REACTIVITY_MODE mode;
	struct {
		variable* tail;
		variable* head;
	} variables;
	void* (*pagesAlloc)(size_t size);
	void (*pagesFree)(void* pointer);
	void (*pagesProtectLock)(void* pointer, size_t size);
	void (*pagesProtectUnlock)(void* pointer, size_t size);
	void (*enableTrap)(void* userData);
} engineState;

// engine data

engineState state = {
	.registerComputed = NULL,
	.changedVariables = NULL,
	.changedVariablesCount = 0,
	.reactiveMem = NULL,
	.mode = MODE_LAZY,
	.variables = {
		.tail = NULL,
		.head = NULL
	},
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

inline variable* getVariableFromPage(void* pointer) {
	// get page address
	size_t pageAddress = ((size_t)pointer&(~0xfff));
	size_t pageIndex = (pageAddress - (size_t)state.reactiveMem->imPointer)/4096;
	mmPage* page = &state.reactiveMem->pages[pageIndex];
	variable* result = NULL;
	variableEntry* variableEntryToTest = page->dependents.head;
	while (variableEntryToTest!=NULL) {
		variable* variableToTest = variableEntryToTest->variable;
		// strict inequality in second case for last byte
		if (((size_t)variableToTest->value <= (size_t)pointer) && ((size_t)pointer < (size_t)variableToTest->value+variableToTest->size)) {
			result = variableToTest;
			break;
		}
		variableEntryToTest = variableEntryToTest->next;
	}
	return result;
}

// if we run in kernel mode we can isolate reactive memory to kernel space to prevent write to it from user mode process
void exceptionHandler(void* userData, REACTIVITY_EXCEPTION exception, bool isWrite, void* pointer) {
	if (exception == EXCEPTION_PAGEFAULT) {
		variable* realAddr = getVariableFromPage(pointer);
		if (realAddr!=NULL) {
			mtx_lock(&state.reactiveMem->mutex);
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
						variableEntry* dependsEntry = malloc(sizeof(variableEntry)); // TODO check to malloc return NULL
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
						variableEntry* observersEntry = malloc(sizeof(variableEntry)); // TODO check to malloc return NULL
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
				if (isWrite) {
					state.changedVariables = realloc(state.changedVariables, (state.changedVariablesCount+1+1)*sizeof(variable*));
					state.changedVariables[state.changedVariablesCount] = realAddr;
					state.changedVariablesCount++;
					state.changedVariables[state.changedVariablesCount] = NULL;
					// kernel unlock only accessed page, unlock all of them (for instructions which access to data on pages boundary (on two pages))
					state.pagesProtectUnlock(realAddr->value, realAddr->size);
					// save old value for ref variable
					memcpy(state.changedVariables[state.changedVariablesCount-1]->oldValue, state.changedVariables[state.changedVariablesCount-1]->value, state.changedVariables[state.changedVariablesCount-1]->size);
				} else {
					// lazy calculation, only on read
					if (realAddr->isComputed) {
						// lock all pages (not only pages for accessed varible) for prevent bug:
						// variable1 placed on page1, variable2 placed on page2
						// 1. execute instruction which access to data on pages boundary (on two pages)
						// 2. #PF -> unlock page 1
						// 3. #PF -> unlock page 2
						// 4. calc value of computed variable2 with access to variable from page 1
						// 5. !missing #PF (page 1 unlocked)!
						state.pagesProtectLock(state.reactiveMem->imPointer, state.reactiveMem->size);
						realAddr->callback(realAddr->bufValue, state.reactiveMem->imPointer);
						// unlock all pages (not only pages for accessed varible) for prevent bug:
						// variable1 placed on page1, variable2 placed on page2
						// 1. execute instruction which access to data on pages boundary (on two pages)
						// 2. #PF -> unlock page 1
						// 3. #PF -> unlock page 2
						// 4. calc value of computed variable2 (all pages will be locked)
						// 5. unlock pages of variable2 (page2)
						// 6. !execute instruction again -> #PF (page1 locked)!
						state.pagesProtectUnlock(state.reactiveMem->imPointer, state.reactiveMem->size);
						memcpy(realAddr->value, realAddr->bufValue, realAddr->size);
					}
				}
			}
			state.enableTrap(userData); // trap flag for get exception after memory access instruction
		}
	}
	else if (exception == EXCEPTION_DEBUG) {
		state.pagesProtectLock(state.reactiveMem->imPointer, state.reactiveMem->size);
		if (state.changedVariablesCount>0) {
			size_t changedVariablesCount = state.changedVariablesCount;
			state.changedVariablesCount = 0;
			for (size_t i=0; i<changedVariablesCount; i++) {
				variable* changedVariable = state.changedVariables[i];
				if (!changedVariable->isComputed) {
					variableEntry* compEntryToFree = NULL;
					variableEntry* compEntry = changedVariable->observers.head;
					changedVariable->observers.head = NULL;
					changedVariable->observers.tail = NULL;
					if (changedVariable->triggerCallback!=NULL) {
						changedVariable->triggerCallback(changedVariable->value, changedVariable->oldValue, state.reactiveMem->imPointer);
					}
					while (compEntry!=NULL) {
						// save old value for computed variable
						// TODO MODE_LAZY
						state.pagesProtectUnlock(compEntry->variable->value, compEntry->variable->size);
						memcpy(compEntry->variable->oldValue, compEntry->variable->value, compEntry->variable->size);
						state.pagesProtectLock(compEntry->variable->value, compEntry->variable->size);
						// update computed variable depends
						// free depends list
						variableEntry* variableEntryToFree = NULL;
						variableEntry* nextVariableEntry = compEntry->variable->depends.head;
						while (nextVariableEntry!=NULL) {
							variableEntryToFree = nextVariableEntry;
							nextVariableEntry = nextVariableEntry->next;
							// find and remove it from depend variable observers list
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
									free(observerNextVariableEntry);
									break;
								}
								observerNextVariableEntry = observerNextVariableEntry->next;
							}
							free(variableEntryToFree);
						}
						compEntry->variable->depends.head = NULL;
						compEntry->variable->depends.tail = NULL;
						state.registerComputed = compEntry->variable;
						compEntry->variable->callback(compEntry->variable->bufValue, state.reactiveMem->imPointer);
						state.registerComputed = NULL;
						state.pagesProtectUnlock(compEntry->variable->value, compEntry->variable->size);
						memcpy(compEntry->variable->value, compEntry->variable->bufValue, compEntry->variable->size);
						state.pagesProtectLock(compEntry->variable->value, compEntry->variable->size);
						if (compEntry->variable->triggerCallback!=NULL) {
							compEntry->variable->triggerCallback(compEntry->variable->value, compEntry->variable->oldValue, state.reactiveMem->imPointer);
						}
						compEntryToFree = compEntry;
						compEntry = compEntry->next;
						free(compEntryToFree);
					}
				}
			}
		}
		mtx_unlock(&state.reactiveMem->mutex);
	}
}

variable* createVariable(void* pointer, size_t size) {
	variable* var = malloc(sizeof(variable));
	var->isComputed = false;
	var->callback = NULL;
	var->triggerCallback = NULL;
	var->observers.head = NULL;
	var->observers.tail = NULL;
	var->depends.head = NULL;
	var->depends.tail = NULL;
	var->next = NULL;
	var->bufValue = malloc(size);
	var->oldValue = malloc(size);
	var->value = pointer;
	var->size = size;
	if (state.variables.head == NULL) {
		state.variables.head = var;
		state.variables.tail = var;
	} else {
		state.variables.tail->next = var;
		state.variables.tail = var;
	}
	// get mmPage's corresponding to variable
	// get start page address
	size_t pageAddress = ((size_t)pointer&(~0xfff));
	size_t pageIndex = (pageAddress - (size_t)state.reactiveMem->imPointer)/4096;
	// get last page address
	size_t variableLastPageAddress = ((size_t)pointer + size)&(~0xfff);
	size_t variablePagesCount = 1 + (variableLastPageAddress - pageAddress)/4096;
	for (size_t i=0; i<variablePagesCount; i++) {
		// one variable can be linked with multiple pages
		variableEntry* dependentEntry = malloc(sizeof(variableEntry)); // TODO check to malloc return NULL
		dependentEntry->variable = var;
		dependentEntry->prev = NULL;
		dependentEntry->next = NULL;
		if (state.reactiveMem->pages[pageIndex+i].dependents.head == NULL) {
			state.reactiveMem->pages[pageIndex+i].dependents.head = dependentEntry;
			state.reactiveMem->pages[pageIndex+i].dependents.tail = dependentEntry;
		} else {
			dependentEntry->prev = state.reactiveMem->pages[pageIndex].dependents.tail;
			state.reactiveMem->pages[pageIndex+i].dependents.tail->next = dependentEntry;
			state.reactiveMem->pages[pageIndex+i].dependents.tail = dependentEntry;
		}
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
	mmBlock* block = malloc(sizeof(mmBlock));
	block->imPointer = state.pagesAlloc(memSize); // guard pages
	block->pagesCount = (memSize+(4096-1))/4096; // 4096 is default page size for x86/x64
	block->pages = malloc(sizeof(mmPage)*block->pagesCount);
	for (size_t i=0; i<block->pagesCount; i++) {
		block->pages[i].dependents.head = NULL;
		block->pages[i].dependents.tail = NULL;
	}
	block->size = memSize;
	mtx_init(&block->mutex, mtx_plain|mtx_recursive); // TODO handle errors
	state.reactiveMem = block;
	return block->imPointer;
}

void reactiveFree(void* memPointer) {
	mtx_destroy(&state.reactiveMem->mutex);
	state.pagesFree(memPointer);
	for (size_t i=0; i<state.reactiveMem->pagesCount; i++) {
		// free dependents list
		variableEntry* variableEntryToFree;
		variableEntry* nextVariableEntry;
		variableEntryToFree = NULL;
		nextVariableEntry = state.reactiveMem->pages[i].dependents.head;
		while (nextVariableEntry!=NULL) {
			variableEntryToFree = nextVariableEntry;
			nextVariableEntry = nextVariableEntry->next;
			free(variableEntryToFree);
		}
		state.reactiveMem->pages[i].dependents.head = NULL;
		state.reactiveMem->pages[i].dependents.tail = NULL;
	}
	free(state.reactiveMem->pages); // free pages descriptors
	free(state.reactiveMem);
	state.reactiveMem = NULL;
}

void initReactivity(REACTIVITY_MODE mode, void* (*pagesAlloc)(size_t size), void (*pagesFree)(void* pointer), void (*pagesProtectLock)(void* pointer, size_t size), void (*pagesProtectUnlock)(void* pointer, size_t size), void (*enableTrap)(void* userData)) {
	state.mode = mode;
	state.pagesAlloc = pagesAlloc;
	state.pagesFree = pagesFree;
	state.pagesProtectLock = pagesProtectLock;
	state.pagesProtectUnlock = pagesProtectUnlock;
	state.enableTrap = enableTrap;
	state.changedVariables = malloc(sizeof(variable*));
	state.changedVariables[0] = NULL; // last element must be nullptr (free memory prevention)
	state.changedVariablesCount = 0;
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
			free(variableEntryToFree);
		}
		variableToFree->observers.head = NULL;
		variableToFree->observers.tail = NULL;
		// free depends list
		variableEntryToFree = NULL;
		nextVariableEntry = variableToFree->depends.head;
		while (nextVariableEntry!=NULL) {
			variableEntryToFree = nextVariableEntry;
			nextVariableEntry = nextVariableEntry->next;
			free(variableEntryToFree);
		}
		variableToFree->depends.head = NULL;
		variableToFree->depends.tail = NULL;
		free(variableToFree->bufValue);
		free(variableToFree->oldValue);
		free(variableToFree);
	}
	state.variables.head = NULL;
	state.variables.tail = NULL;
	free(state.changedVariables);
	state.pagesAlloc = NULL;
	state.pagesFree = NULL;
	state.pagesProtectLock = NULL;
	state.pagesProtectUnlock = NULL;
	state.enableTrap = NULL;
}