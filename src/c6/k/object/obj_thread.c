#include "obj_thread.h"
#include "vm.h"

//为运行函数准备桢栈
void prepareFrame(ObjThread* objThread, ObjClosure* objClosure, Value* stackStart) {
   ASSERT(objThread->frameCapacity > objThread->usedFrameNum, "frame not enough!!");   
   //objThread->usedFrameNum是最新可用的frame
   Frame* frame = &(objThread->frames[objThread->usedFrameNum++]);

   //thread中的各个frame是共享thread的stack 
   //frame用frame->stackStart指向各自frame在thread->stack中的起始地址
   frame->stackStart = stackStart;
   frame->closure = objClosure;
   frame->ip = objClosure->fn->instrStream.datas;
}

//重置thread
void resetThread(ObjThread* objThread, ObjClosure* objClosure) {
   objThread->esp = objThread->stack;  
   objThread->openUpvalues = NULL;
   objThread->caller = NULL;
   objThread->errorObj = VT_TO_VALUE(VT_NULL);
   objThread->usedFrameNum = 0;

   ASSERT(objClosure != NULL, "objClosure is NULL in function resetThread");
   prepareFrame(objThread, objClosure, objThread->stack);
}

//新建线程
ObjThread* newObjThread(VM* vm, ObjClosure* objClosure) {
   ASSERT(objClosure != NULL, "objClosure is NULL!");

   Frame* frames = ALLOCATE_ARRAY(vm, Frame, INITIAL_FRAME_NUM);

   //加1是为接收者的slot
   uint32_t stackCapacity = ceilToPowerOf2(objClosure->fn->maxStackSlotUsedNum + 1);  
   Value* newStack = ALLOCATE_ARRAY(vm, Value, stackCapacity); 

   ObjThread* objThread = ALLOCATE(vm, ObjThread);
   initObjHeader(vm, &objThread->objHeader, OT_THREAD, vm->threadClass);

   objThread->frames = frames;
   objThread->frameCapacity = INITIAL_FRAME_NUM;
   objThread->stack = newStack;
   objThread->stackCapacity = stackCapacity;

   resetThread(objThread, objClosure);
   return objThread;
}
