#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <windows.h>
#include "reactivity.h"

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
} someStruct;

// user functions

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
	printf("[trigger3] watch value (count): %llu, oldValue (count): %llu\n", *val, *oldVal);
}

void triggerCallback4(void* value, void* oldValue, void* imPointer) {
	uint64_t* val = (uint64_t*)value;
	uint64_t* oldVal = (uint64_t*)oldValue;
	someStruct* _someStruct = (someStruct*)imPointer;
	printf("[trigger4] watch value (doubleField1): %llu, oldValue (doubleField1): %llu\n", *val, *oldVal);
}

// tests here

int main() {
	printf("reactive memory app\n");

	initReactivity(MODE_NONLAZY, malloc, free);
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

	watch(&someStruct->count, triggerCallback3);
	watch(&someStruct->doubleField1, triggerCallback4);
	
	printf("doubleField1: %llu, field5: %llu\n", someStruct->doubleField1, someStruct->field5);
	
	someStruct->field1 = 0;
	
	printf("field1: %u, field2.field2: %u, field3: %u\n", someStruct->field1, someStruct->field2.field2, someStruct->field3);
	printf("array elements count: %llu\n", someStruct->count);
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
	printf("array elements count: %llu\n", someStruct->count);

	reactiveFree(someStruct);
	freeReactivity();

	getchar();
}