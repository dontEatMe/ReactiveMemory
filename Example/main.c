#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../ReactiveMemory/reactivity.h"

// user structures

typedef struct arrayElementStruct {
	size_t field1;
	struct {
		struct arrayElementStruct* prev;
		struct arrayElementStruct* next;
	} listEntry;
} arrayElementStruct;

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
	uint64_t doubleField1;
	someComputedSubStruct field2;
	uint32_t field3;
	someSubStruct field4;
	uint64_t field5;
	arrayElementStruct elem1;
	arrayElementStruct elem2;
	arrayElementStruct elem3;
	size_t count;
	uint8_t pages[4097];
	uint8_t field6;
} someStruct;

// user functions

void* pagesAlloc(size_t size) {
	return VirtualAlloc(NULL, size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE|PAGE_GUARD); // imaginary pages
}

void pagesFree(void* pointer) {
	VirtualFree(pointer, 0, MEM_RELEASE);
}

void pagesProtectLock(void* pointer, size_t size) {
	DWORD oldProtect;
	VirtualProtect(pointer, size, PAGE_READWRITE|PAGE_GUARD, &oldProtect);
}

void pagesProtectUnlock(void* pointer, size_t size) {
	DWORD oldProtect;
	VirtualProtect(pointer, size, PAGE_READWRITE, &oldProtect);
}

void enableTrap(void* userData) {
	PEXCEPTION_POINTERS ExceptionInfo = (PEXCEPTION_POINTERS)userData;
	ExceptionInfo->ContextRecord->EFlags |= 0x00000100;
}

void computedField2(void* bufForReturnValue, void* imPointer) {
	someComputedSubStruct* field2 = (someComputedSubStruct*)bufForReturnValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	field2->field1 = 1;
	field2->field2 = _someStruct->field1 + 2;
	field2->field3 = 3;
}

void computedField3(void* bufForReturnValue, void* imPointer) {
	uint32_t* field3 = (uint32_t*)bufForReturnValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	*field3 = _someStruct->field2.field2 + _someStruct->field1;
}

void computedDoubleField1(void* bufForReturnValue, void* imPointer) {
	uint64_t* doubleField1 = (uint64_t*)bufForReturnValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	*doubleField1 = _someStruct->field1 + _someStruct->field1;
}

void computedField5(void* bufForReturnValue, void* imPointer) {
	uint64_t* field5 = (uint64_t*)bufForReturnValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	*field5 = _someStruct->field3 + _someStruct->field2.field2;
}

void computedCount(void* bufForReturnValue, void* imPointer) {
	size_t* count = (size_t*)bufForReturnValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	size_t counter = 0;
	counter++;
	arrayElementStruct* currentElement = &_someStruct->elem1;
	while (currentElement->listEntry.next!=NULL) {
		counter++;
		currentElement = currentElement->listEntry.next;
	}
	*count = counter;
}

void computedField6(void* bufForReturnValue, void* imPointer) {
	uint8_t* field6 = (uint8_t*)bufForReturnValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	*field6 = _someStruct->pages[0];
}

void triggerCallback1(void* value, void* oldValue, void* imPointer) {
	uint32_t* val = (uint32_t*)value;
	uint32_t* oldVal = (uint32_t*)oldValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	printf("[trigger1] watch value (field3): %u, field3 value: %u, field1 value: %u, oldValue (field3): %u\n", *val, _someStruct->field3, _someStruct->field1, *oldVal);
}

void triggerCallback2(void* value, void* oldValue, void* imPointer) {
	someSubStruct* val = (someSubStruct*)value;
	someSubStruct* oldVal = (someSubStruct*)oldValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	printf("[trigger2] watch value (field4.field3): %u, field4.field3 value: %u, field1 value: %u, oldValue (field4.field3): %u\n", val->field3, _someStruct->field4.field3, _someStruct->field1, oldVal->field3);
}

void triggerCallback3(void* value, void* oldValue, void* imPointer) {
	size_t* val = (size_t*)value;
	size_t* oldVal = (size_t*)oldValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	printf("[trigger3] watch value (count): %zu, oldValue (count): %zu\n", *val, *oldVal);
}

void triggerCallback4(void* value, void* oldValue, void* imPointer) {
	uint64_t* val = (uint64_t*)value;
	uint64_t* oldVal = (uint64_t*)oldValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	printf("[trigger4] watch value (doubleField1): %llu, oldValue (doubleField1): %llu\n", *val, *oldVal);
}

void triggerCallback5(void* value, void* oldValue, void* imPointer) {
	uint8_t* val = (uint8_t*)value;
	uint8_t* oldVal = (uint8_t*)oldValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	printf("[trigger5] watch value (field6): %hhu, oldValue (field6): %hhu\n", *val, *oldVal);
}

LONG NTAPI imExeption(PEXCEPTION_POINTERS ExceptionInfo) {
	if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_GUARD_PAGE) {
		if (ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 0 || ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 1) { // 0 = read, 1 = write, 8 = DEP
			bool isWrite = false;
			if (ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 1) {
				isWrite = true;
			}
			exceptionHandler((void*)ExceptionInfo, EXCEPTION_PAGEFAULT, isWrite, (void*)ExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
		}
	} else if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
		exceptionHandler((void*)ExceptionInfo, EXCEPTION_DEBUG, false, NULL);	
	}
	return EXCEPTION_CONTINUE_EXECUTION;
}

// tests here

int main() {
	printf("reactive memory app\n");

	initReactivity(MODE_NONLAZY, malloc, free, memcpy, pagesAlloc, pagesFree, pagesProtectLock, pagesProtectUnlock, enableTrap);
	void* exHandler = AddVectoredExceptionHandler(1, imExeption);
	someStruct* someStruct = reactiveAlloc(sizeof(struct someStruct));

	ref(&someStruct->field1, sizeof(someStruct->field1));
	computed(&someStruct->doubleField1, sizeof(someStruct->doubleField1), computedDoubleField1);
	computed(&someStruct->field2, sizeof(someStruct->field2), computedField2);
	computed(&someStruct->field3, sizeof(someStruct->field3), computedField3);
	computed(&someStruct->field5, sizeof(someStruct->field5), computedField5);
	ref(&someStruct->field4, sizeof(someStruct->field4));
	ref(&someStruct->elem1, sizeof(someStruct->elem1));
	ref(&someStruct->elem2, sizeof(someStruct->elem2));
	ref(&someStruct->elem3, sizeof(someStruct->elem3));
	someStruct->elem1.listEntry.prev = NULL;
	someStruct->elem1.listEntry.next = NULL; // will be changed
	someStruct->elem2.listEntry.prev = &someStruct->elem1;
	someStruct->elem2.listEntry.next = NULL; // will be changed
	someStruct->elem3.listEntry.prev = &someStruct->elem2;
	someStruct->elem3.listEntry.next = NULL;
	computed(&someStruct->count, sizeof(someStruct->count), computedCount);
	ref(&someStruct->pages, sizeof(someStruct->pages));
	computed(&someStruct->field6, sizeof(someStruct->field6), computedField6);

	watch(&someStruct->count, triggerCallback3);
	watch(&someStruct->doubleField1, triggerCallback4);

	printf("doubleField1: %llu, field5: %llu\n", someStruct->doubleField1, someStruct->field5);
	someStruct->field1 = 0;
	printf("field1: %u, field2.field2: %u, field3: %u\n", someStruct->field1, someStruct->field2.field2, someStruct->field3);

	someStruct->field3 = 200;
	printf("field3: %u\n", someStruct->field3);

	printf("field6: %hhu\n", someStruct->field6);
	memset(&someStruct->pages, 0x01, sizeof(someStruct->pages));
	printf("field6: %hhu\n", someStruct->field6);

	watch(&someStruct->field6, triggerCallback5);
	// write 2 bytes on page boundary by one instruction
	*(uint16_t*)(void*)((size_t)&someStruct->pages+(4096-((size_t)&someStruct->pages-((size_t)(&someStruct->pages)&(~4095)))-1)) = 0x0202;

	printf("array elements count: %zu\n", someStruct->count);
	printf("add second element to array\n");
	someStruct->elem1.listEntry.next =  &someStruct->elem2;
	printf("add third element to array\n");
	someStruct->elem2.listEntry.next =  &someStruct->elem3;
	
	watch(&someStruct->field3, triggerCallback1);
	watch(&someStruct->field4, triggerCallback2);

	someStruct->field1 = 77;
	someStruct->field1 = 79;
	someStruct->field4.field3 = 5;
	
	printf("field1: %u, field2.field2: %u, field3: %u\n", someStruct->field1, someStruct->field2.field2, someStruct->field3);
	printf("doubleField1: %llu, field5: %llu\n", someStruct->doubleField1, someStruct->field5);
	printf("array elements count: %zu\n", someStruct->count);

	reactiveFree(someStruct);

	RemoveVectoredExceptionHandler(exHandler);
	freeReactivity();

	getchar();
}