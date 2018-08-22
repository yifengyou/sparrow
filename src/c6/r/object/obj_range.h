#ifndef _OBJECT_RANGE_H
#define _OBJECT_RANGE_H
#include "class.h"

typedef struct {
   ObjHeader objHeader;
   int from;   //范围的起始
   int to;     //范围的结束
} ObjRange;    //range对象

ObjRange* newObjRange(VM* vm, int from, int to);
#endif
