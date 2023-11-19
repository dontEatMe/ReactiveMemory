#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <windows.h>
#include "reactivity.h"

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

// tests here

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