# ReactiveMemory
exceptions-based reactivity engine for C language  
builds for windows x86/x64  
  
![](https://lvlb.ru/ReactiveMemory.png)  
  
also you can build .lib using LLVM:  
clang -c -m32 -O0 -nostdlib -fno-builtin -std=c2x reactivity.c -o reactivity32.obj  
llvm-lib /out:ReactiveMemory32.lib reactivity32.obj  
clang -c -m64 -O0 -nostdlib -fno-builtin -std=c2x reactivity.c -o reactivity64.obj  
llvm-lib /out:ReactiveMemory64.lib reactivity64.obj