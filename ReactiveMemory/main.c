#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>

typedef struct field {
	uint32_t value;
	BOOL isComputed;
	void (*callback)(void* imPointer); // pointer to compute callback
} field;

typedef struct someStruct {
	field field1;
	field field2;
	field field3;
} someStruct;

// TODO memory manager

typedef struct mmBlock {
	void* imPointer;
	void* rePointer;
	size_t size;
} mmBlock;

mmBlock* reactiveMem;

LONG NTAPI imExeption(PEXCEPTION_POINTERS ExceptionInfo) {
	if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		// lazy calculation, only on read
		if (ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 0) { // read
			size_t offset = ExceptionInfo->ExceptionRecord->ExceptionInformation[1] - (size_t)reactiveMem->imPointer;
			field* realAddr = (size_t)reactiveMem->rePointer + offset;
			if (realAddr->isComputed) {
				realAddr->callback(reactiveMem->imPointer);
			}
		}
		// fix rights
		VirtualAlloc(reactiveMem->imPointer, reactiveMem->size, MEM_COMMIT, PAGE_READWRITE);
		memcpy(reactiveMem->imPointer, reactiveMem->rePointer, reactiveMem->size);
		ExceptionInfo->ContextRecord->EFlags |= 0x00000100; // trap flag for get exception after memory access instruction
		//printf("EXCEPTION_ACCESS_VIOLATION\n");
	}
	else if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
		memcpy(reactiveMem->rePointer, reactiveMem->imPointer, reactiveMem->size);
		VirtualFree(reactiveMem->imPointer, reactiveMem->size, MEM_DECOMMIT);
		//printf("EXCEPTION_SINGLE_STEP\n");
	}
	return EXCEPTION_CONTINUE_EXECUTION;
}

mmBlock* reactiveAlloc(size_t memSize) {
	mmBlock* block = malloc(sizeof(mmBlock));
	block->imPointer = VirtualAlloc(NULL, memSize, MEM_RESERVE, PAGE_READWRITE); // imaginary pages
	block->rePointer = VirtualAlloc(NULL, memSize, MEM_COMMIT, PAGE_READWRITE);// real pages
	block->size = memSize;
	return block;
}

void reactiveFree(mmBlock* memBlock) {
	VirtualFree(memBlock->imPointer, 0, MEM_RELEASE);	
	VirtualFree(memBlock->rePointer, 0, MEM_RELEASE);
	free(memBlock);
}

void computedField2(void* imPointer) {
	someStruct* someStruct = imPointer;
	someStruct->field2.value = someStruct->field1.value + 2;
}

void computedField3(void* imPointer) {
	someStruct* someStruct = imPointer;
	someStruct->field3.value = someStruct->field2.value + 2;
}

void printSomeStruct() {
	someStruct* someStruct = reactiveMem->imPointer;
	printf("field1: %d, field2: %d, field3: %d\n", someStruct->field1.value, someStruct->field2.value, someStruct->field3.value);
}

int main() {
	printf("reactive memory app\n");

	PVOID exHandler = AddVectoredExceptionHandler( 1, imExeption );

	reactiveMem = reactiveAlloc(sizeof(someStruct));
	
	someStruct* someStruct = reactiveMem->imPointer;
	someStruct->field2.isComputed = TRUE;
	someStruct->field2.callback = computedField2;
	
	someStruct->field3.isComputed = TRUE;
	someStruct->field3.callback = computedField3;
	
	printSomeStruct(); // prinf in function for drop reactive-fails compiler optimization on Release build
	
	someStruct->field1.value = 77;

	printSomeStruct(); // prinf in function for drop reactive-fails compiler optimization on Release build

	reactiveFree(reactiveMem);

	RemoveVectoredExceptionHandler(exHandler);

	getchar();
}