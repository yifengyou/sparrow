#ifndef _OBJECT_THREAD_H
#define _OBJECT_THREAD_H
#include "obj_fn.h"

typedef struct objThread {
   ObjHeader objHeader;

   Value* stack;  //运行时栈的栈底
   Value* esp;    //运行时栈的栈顶
   uint32_t stackCapacity;  //栈容量

   Frame* frames;   //调用框架
   uint32_t usedFrameNum;   //已使用的frame数量
   uint32_t frameCapacity;  //frame容量

   //"打开的upvalue"的链表首结点
   ObjUpvalue* openUpvalues;

   //当前thread的调用者
   struct objThread* caller;

   //导致运行时错误的对象会放在此处,否则为空
   Value errorObj;
} ObjThread;    //线程对象

void prepareFrame(ObjThread* objThread, ObjClosure* objClosure, Value* stackStart);
ObjThread* newObjThread(VM* vm, ObjClosure* objClosure);
void resetThread(ObjThread* objThread, ObjClosure*  objClosure);
#endif
