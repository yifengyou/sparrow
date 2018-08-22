#ifndef _VM_VM_H
#define _VM_VM_H
#include "common.h"
#include "class.h"
#include "parser.h"

struct vm {
   Class* stringClass;
   Class* fnClass;
   uint32_t allocatedBytes;  //累计已分配的内存量
   ObjHeader* allObjects;  //所有已分配对象链表
   Parser* curParser;  //当前词法分析器
};

void initVM(VM* vm);
VM* newVM(void);
#endif
