#ifndef _VM_CORE_H
#define _VM_CORE_H
#include "vm.h"
extern char* rootDir;
char* readFile(const char* sourceFile);
VMResult executeModule(VM* vm, Value moduleName, const char* moduleCode);
int getIndexFromSymbolTable(SymbolTable* table, const char* symbol, uint32_t length);
int addSymbol(VM* vm, SymbolTable* table, const char* symbol, uint32_t length);
void buildCore(VM* vm);
void bindMethod(VM* vm, Class* class, uint32_t index, Method method);
void bindSuperClass(VM* vm, Class* subClass, Class* superClass);
#endif
