#ifndef _OBJECT_LIST_H
#define _OBJECT_LIST_H
#include "class.h"
#include "vm.h"

typedef struct {
   ObjHeader objHeader;
   ValueBuffer elements;  //list中的元素
} ObjList;  //list对象

ObjList* newObjList(VM* vm, uint32_t elementNum);
Value removeElement(VM* vm, ObjList* objList, uint32_t index);
void insertElement(VM* vm, ObjList* objList, uint32_t index, Value value);
#endif
