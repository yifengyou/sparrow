#include "vm.h"
#include <stdlib.h>
#include "core.h"

//初始化虚拟机
void initVM(VM* vm) {
   vm->allocatedBytes = 0;
   vm->allObjects = NULL;
   vm->curParser = NULL;
   StringBufferInit(&vm->allMethodNames);
   vm->allModules = newObjMap(vm);
   vm->curParser = NULL;
}

VM* newVM() {
   VM* vm = (VM*)malloc(sizeof(VM)); 
   if (vm == NULL) {
      MEM_ERROR("allocate VM failed!"); 
   }
   initVM(vm);
   buildCore(vm);
   return vm;
}

//确保stack有效
void ensureStack(VM* vm, ObjThread* objThread, uint32_t neededSlots) {
   if (objThread->stackCapacity >= neededSlots) {
      return;
   }

   uint32_t newStackCapacity = ceilToPowerOf2(neededSlots);
   ASSERT(newStackCapacity > objThread->stackCapacity, "newStackCapacity error!");

   //记录原栈底以用于下面判断扩容后的栈是否是原地扩容
   Value* oldStackBottom = objThread->stack;

   uint32_t slotSize = sizeof(Value);
   objThread->stack = (Value*)memManager(vm, objThread->stack,
	 objThread->stackCapacity * slotSize, newStackCapacity * slotSize);
   objThread->stackCapacity = newStackCapacity;

   //为判断是否原地扩容
   long offset = objThread->stack - oldStackBottom;

   //说明os无法在原地满足内存需求, 重新分配了起始地址,下面要调整
   if (offset != 0) {
      //调整各堆栈框架的地址  
      uint32_t idx = 0;
      while (idx < objThread->usedFrameNum) {
	 objThread->frames[idx++].stackStart += offset; 
      }
      
      //调整"open upValue"
      ObjUpvalue* upvalue = objThread->openUpvalues;
      while (upvalue != NULL) {
	 upvalue->localVarPtr += offset;
	 upvalue = upvalue->next; 
      }
   
      //更新栈顶
      objThread->esp += offset;
   }
}

//为objClosure在objThread中创建运行时栈
inline static void createFrame(VM* vm, ObjThread* objThread,
      ObjClosure* objClosure, int argNum) {

   if (objThread->usedFrameNum + 1 > objThread->frameCapacity) { //扩容
      uint32_t newCapacity = objThread->frameCapacity * 2; 
      uint32_t frameSize = sizeof(Frame);
      objThread->frames = (Frame*)memManager(vm, objThread->frames,
	    frameSize * objThread->frameCapacity, frameSize * newCapacity);
      objThread->frameCapacity = newCapacity;
   }
   
   //栈大小等于栈顶-栈底
   uint32_t stackSlots = (uint32_t)(objThread->esp - objThread->stack);
   //总共需要的栈大小
   uint32_t neededSlots = stackSlots + objClosure->fn->maxStackSlotUsedNum;

   ensureStack(vm, objThread, neededSlots);

   //准备上cpu
   prepareFrame(objThread, objClosure, objThread->esp - argNum);
}

//关闭在栈中slot为lastSlot及之上的upvalue
static void closeUpvalue(ObjThread* objThread, Value* lastSlot) {
   ObjUpvalue* upvalue = objThread->openUpvalues;
   while (upvalue != NULL && upvalue->localVarPtr >= lastSlot) {
      //localVarPtr改指向本结构中的closedUpvalue
      upvalue->closedUpvalue = *(upvalue->localVarPtr);
      upvalue->localVarPtr = &(upvalue->closedUpvalue);

      upvalue = upvalue->next;
   }
   objThread->openUpvalues = upvalue;
}

//创建线程已打开的upvalue链表，并将localVarPtr所属的upvalue以降序插入到该链表
static ObjUpvalue* createOpenUpvalue(VM* vm, ObjThread* objThread, Value* localVarPtr) {
   //如果openUpvalues链表为空就创建
   if (objThread->openUpvalues == NULL) {
      objThread->openUpvalues = newObjUpvalue(vm, localVarPtr);
      return objThread->openUpvalues;
   }

   //下面以upvalue.localVarPtr降序组织openUpvalues
   ObjUpvalue* preUpvalue = NULL;
   ObjUpvalue* upvalue = objThread->openUpvalues;

   //后面的代码保证了openUpvalues按照降顺组织,
   //下面向堆栈的底部遍历,直到找到合适的插入位置
   while (upvalue != NULL && upvalue->localVarPtr > localVarPtr) {
      preUpvalue = upvalue; 
      upvalue = upvalue->next;
   }

   //如果之前已经插入了该upvalue则返回
   if (upvalue != NULL && upvalue->localVarPtr == localVarPtr) {
      return upvalue;
   }

   //openUpvalues中未找到该upvalue,
   //现在就创建新upvalue,按照降序插入到链表
   ObjUpvalue* newUpvalue = newObjUpvalue(vm, localVarPtr);

   //保证了openUpvalues首结点upvalue->localVarPtr的值是最高的
   if (preUpvalue == NULL) {
      //说明上面while的循环体未执行,新结点（形参localVarPtr）的值大于等于链表首结点
      //因此使链表结点指向它所在的新upvalue结点
      objThread->openUpvalues = newUpvalue; 
   } else {
      //preUpvalue已处于正确的位置
      preUpvalue->next = newUpvalue;
   }
   newUpvalue->next = upvalue;

   return newUpvalue;//返回该结点
}

//校验基类合法性
static void validateSuperClass(VM* vm, Value classNameValue, 
      uint32_t fieldNum, Value superClassValue) {

   //首先确保superClass的类型得是class
   if (!VALUE_IS_CLASS(superClassValue)) {
      ObjString* classNameString = VALUE_TO_OBJSTR(classNameValue);
      RUN_ERROR("class \"%s\" `s superClass is not a valid class!",
	    classNameString->value.start);
   }

   Class* superClass = VALUE_TO_CLASS(superClassValue);

  //基类不允许为内建类
   if (superClass == vm->stringClass ||
	 superClass == vm->mapClass ||
	 superClass == vm->rangeClass ||
	 superClass == vm->listClass ||
	 superClass == vm->nullClass ||
	 superClass == vm->boolClass ||
	 superClass == vm->numClass ||
	 superClass == vm->fnClass ||
	 superClass == vm->threadClass
	 ) {
      RUN_ERROR("superClass mustn`t be a buildin class!"); 
   }

   //子类也要继承基类的域,
   //故子类自己的域+基类域的数量不可超过MAX_FIELD_NUM
   if (superClass->fieldNum + fieldNum > MAX_FIELD_NUM) {
      RUN_ERROR("number of field including super exceed %d!", MAX_FIELD_NUM);
   }
}
