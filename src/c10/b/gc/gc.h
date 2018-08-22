#ifndef _GC_GC_H
#define _GC_GC_H
#include "vm.h"
void grayObject(VM* vm, ObjHeader* obj);
void grayValue(VM* vm, Value value);
void freeObject(VM* vm, ObjHeader* obj);
void startGC(VM* vm);
#endif
