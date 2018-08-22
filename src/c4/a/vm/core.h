#ifndef _VM_CORE_H
#define _VM_CORE_H
#include "vm.h"
extern char* rootDir;
char* readFile(const char* sourceFile);
VMResult executeModule(VM* vm, Value moduleName, const char* moduleCode);
void buildCore(VM* vm);
#endif
