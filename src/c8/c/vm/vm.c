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

//修正部分指令操作数
static void patchOperand(Class* class, ObjFn* fn) {
   int ip = 0; 
   OpCode opCode;
   while (true) {
      opCode = (OpCode)fn->instrStream.datas[ip++];
      switch (opCode) {

	 case OPCODE_LOAD_FIELD: 
	 case OPCODE_STORE_FIELD: 
	 case OPCODE_LOAD_THIS_FIELD: 
	 case OPCODE_STORE_THIS_FIELD: 
	   //修正子类的field数目  参数是1字节
	    fn->instrStream.datas[ip++] += class->superClass->fieldNum;
	    break;

	 case OPCODE_SUPER0:
	 case OPCODE_SUPER1:
	 case OPCODE_SUPER2:
	 case OPCODE_SUPER3:
	 case OPCODE_SUPER4:
	 case OPCODE_SUPER5:
	 case OPCODE_SUPER6:
	 case OPCODE_SUPER7:
	 case OPCODE_SUPER8:
	 case OPCODE_SUPER9:
	 case OPCODE_SUPER10:
	 case OPCODE_SUPER11:
	 case OPCODE_SUPER12:
	 case OPCODE_SUPER13:
	 case OPCODE_SUPER14:
	 case OPCODE_SUPER15:
	 case OPCODE_SUPER16: {
	 //指令流1: 2字节的method索引
	 //指令流2: 2字节的基类常量索引
	 
	    ip += 2; //跳过2字节的method索引
	    uint32_t superClassIdx = 
	       (fn->instrStream.datas[ip] << 8) | fn->instrStream.datas[ip + 1];

	    //回填在函数emitCallBySignature中的占位VT_TO_VALUE(VT_NULL)
	    fn->constants.datas[superClassIdx] = OBJ_TO_VALUE(class->superClass);

	    ip += 2; //跳过2字节的基类索引

	    break;
	 }

	 case OPCODE_CREATE_CLOSURE: {
	    //指令流: 2字节待创建闭包的函数在常量表中的索引+函数所用的upvalue数 * 2 

	    //函数是存储到常量表中,获取待创建闭包的函数在常量表中的索引
	    uint32_t fnIdx = (fn->instrStream.datas[ip] << 8) | fn->instrStream.datas[ip + 1]; 

	    //递归进入该函数的指令流,继续为其中的super和field修正操作数
	    patchOperand(class, VALUE_TO_OBJFN(fn->constants.datas[fnIdx]));	    
      
	    //ip-1是操作码OPCODE_CREATE_CLOSURE,
	    //闭包中的参数涉及到upvalue,调用getBytesOfOperands获得参数字节数
	    ip += getBytesOfOperands(fn->instrStream.datas, fn->constants.datas, ip - 1);

	    break;
	 }

	 case OPCODE_END:
	    //用于从当前及递归嵌套闭包时返回
	    return;

	 default:
	    //其它指令不需要回填因此就跳过
	    ip += getBytesOfOperands(fn->instrStream.datas, fn->constants.datas, ip - 1);
	    break;
      }
   }
}

//绑定方法和修正操作数
static void bindMethodAndPatch(VM* vm, OpCode opCode, 
      uint32_t methodIndex, Class* class, Value methodValue) {

   //如果是静态方法,就将类指向meta类(使接收者为meta类)
   if (opCode == OPCODE_STATIC_METHOD) {
      class = class->objHeader.class;
   }

   Method method;
   method.type = MT_SCRIPT;
   method.obj = VALUE_TO_OBJCLOSURE(methodValue);
   
   //修正操作数
   patchOperand(class, method.obj->fn);

   //修正过后,绑定method到class
   bindMethod(vm, class, methodIndex, method);
}

//执行指令
VMResult executeInstruction(VM* vm, register ObjThread* curThread) {
   vm->curThread = curThread;   
   register Frame* curFrame;
   register Value* stackStart;
   register uint8_t* ip;
   register ObjFn* fn;
   OpCode opCode;

   //定义操作运行时栈的宏
   //esp是栈中下一个可写入数据的slot
   #define PUSH(value)  (*curThread->esp++ = value)   //压栈
   #define POP()        (*(--curThread->esp))   //出栈
   #define DROP()       (curThread->esp--)	
   #define PEEK()       (*(curThread->esp - 1))	// 获得栈顶的数据
   #define PEEK2()      (*(curThread->esp - 2))	// 获得次栈顶的数据
   
   //下面是读取指令流:objfn.instrStream.datas
   #define READ_BYTE()  (*ip++)   //从指令流中读取一字节
   //读取指令流中的2字节
   #define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))

   //当前指令单元执行的进度就是在指令流中的指针,即ip,将其保存起来
   #define STORE_CUR_FRAME() curFrame->ip = ip   // 备份ip以能回到当前

   //加载最新的frame
   #define LOAD_CUR_FRAME()        \
      /* frames是数组,索引从0起,故usedFrameNum-1 */   \
      curFrame = &curThread->frames[curThread->usedFrameNum - 1]; \
      stackStart = curFrame->stackStart; \
      ip = curFrame->ip; \
      fn = curFrame->closure->fn;

   #define DECODE loopStart: \
      opCode = READ_BYTE();\
      switch (opCode)

   #define CASE(shortOpCode) case OPCODE_##shortOpCode
   #define LOOP() goto loopStart

   LOAD_CUR_FRAME();
   DECODE {
      //若OPCODE依赖于指令环境(栈和指令流),会在各OPCODE下说明

      CASE(LOAD_LOCAL_VAR):
	 //指令流: 1字节的局部变量索引

	 PUSH(stackStart[READ_BYTE()]);
	 LOOP();

      CASE(LOAD_THIS_FIELD): {
	 //指令流: 1字节的field索引

	 uint8_t fieldIdx = READ_BYTE();				   

	 //stackStart[0]是实例对象this
	 ASSERT(VALUE_IS_OBJINSTANCE(stackStart[0]), "method receiver should be objInstance.");
	 ObjInstance* objInstance = VALUE_TO_OBJINSTANCE(stackStart[0]);

	 ASSERT(fieldIdx < objInstance->objHeader.class->fieldNum, "out of bounds field!");
	 PUSH(objInstance->fields[fieldIdx]);
	 LOOP();
      }

      CASE(POP):
	 DROP();
	 LOOP();

      CASE(PUSH_NULL):
	 PUSH(VT_TO_VALUE(VT_NULL));
	 LOOP();

      CASE(PUSH_FALSE):
	 PUSH(VT_TO_VALUE(VT_FALSE));
	 LOOP();

      CASE(PUSH_TRUE):
	 PUSH(VT_TO_VALUE(VT_TRUE));
	 LOOP();

      CASE(STORE_LOCAL_VAR):
	 //栈顶: 局部变量值
	 //指令流: 1字节的局部变量索引

	 //将PEEK()得到的栈顶数据写入指令参数(即READ_BYTE()得到的值)为索引的栈的slot中
	 stackStart[READ_BYTE()] = PEEK();
	 LOOP();

      CASE(LOAD_CONSTANT):
	 //指令流: 2字节的常量索引

	 //加载常量就是把常量表中的数据入栈
	 PUSH(fn->constants.datas[READ_SHORT()]);
	 LOOP();

      {
	 int argNum, index;
	 Value* args;
	 Class* class;
	 Method* method; 

      CASE(CALL0):
      CASE(CALL1):
      CASE(CALL2):
      CASE(CALL3):
      CASE(CALL4):
      CASE(CALL5):
      CASE(CALL6):
      CASE(CALL7):
      CASE(CALL8):
      CASE(CALL9):
      CASE(CALL10):
      CASE(CALL11):
      CASE(CALL12):
      CASE(CALL13):
      CASE(CALL14):
      CASE(CALL15):
      CASE(CALL16):
	 //指令流1: 2字节的method索引
	 //因为还有个隐式的receiver(就是下面的args[0]), 所以参数个数+1.
	 argNum = opCode - OPCODE_CALL0 + 1;

	 //读取2字节的数据(CALL指令的操作数),index是方法名的索引
	 index = READ_SHORT();
      
	 //为参数指针数组args赋值
	 args = curThread->esp - argNum;

	 //获得方法所在的类
	 class = getClassOfObj(vm, args[0]);
	 goto invokeMethod;

      CASE(SUPER0): 
      CASE(SUPER1): 
      CASE(SUPER2): 
      CASE(SUPER3): 
      CASE(SUPER4): 
      CASE(SUPER5): 
      CASE(SUPER6): 
      CASE(SUPER7): 
      CASE(SUPER8): 
      CASE(SUPER9): 
      CASE(SUPER10): 
      CASE(SUPER11): 
      CASE(SUPER12): 
      CASE(SUPER13): 
      CASE(SUPER14): 
      CASE(SUPER15): 
      CASE(SUPER16): 
	 //指令流1: 2字节的method索引
	 //指令流2: 2字节的基类常量索引

	 //因为还有个隐式的receiver(就是下面的args[0]), 所以参数个数+1.
	 argNum = opCode - OPCODE_SUPER0 + 1;
	 index = READ_SHORT();
	 args = curThread->esp - argNum;

	 //在函数bindMethodAndPatch中实现的基类的绑定
	 class = VALUE_TO_CLASS(fn->constants.datas[READ_SHORT()]);

      invokeMethod:
	 if ((uint32_t)index > class->methods.count || 
	       (method = &class->methods.datas[index])->type == MT_NONE) {
	    RUN_ERROR("method \"%s\" not found!", vm->allMethodNames.datas[index].str);
	 }

	 switch (method->type) {
	    case MT_PRIMITIVE:

	       //如果返回值为true,则vm进行空间回收的工作
	       if (method->primFn(vm, args)) {
		  //args[0]是返回值, argNum-1是保留args[0],
		  //args[0]的空间最终由返回值的接收者即函数的主调方回收
		  curThread->esp -= argNum - 1; 
	       } else { 
		  //如果返回false则说明有两种情况:
		  //   1 出错(比如原生函数primThreadAbort使线程报错或无错退出),
		  //   2 或者切换了线程,此时vm->curThread已经被切换为新的线程
		  //保存线程的上下文环境,运行新线程之后还能回到当前老线程指令流的正确位置
		  STORE_CUR_FRAME();

		  if (!VALUE_IS_NULL(curThread->errorObj)) {
		     if (VALUE_IS_OBJSTR(curThread->errorObj)) {
			ObjString* err = VALUE_TO_OBJSTR(curThread->errorObj);
			printf("%s", err->value.start);
		     }
		     //出错后将返回值置为null,避免主调方获取到错误的结果
		     PEEK() = VT_TO_VALUE(VT_NULL);
		  }

		  //如果没有待执行的线程,说明执行完毕
		  if (vm->curThread == NULL) {
		     return VM_RESULT_SUCCESS;
		  }

		  //vm->curThread已经由返回false的函数置为下一个线程
		  //切换到下一个线程的上下文
		  curThread = vm->curThread;
		  LOAD_CUR_FRAME();
	       }
	       break;

	    case MT_SCRIPT:
	       STORE_CUR_FRAME();
	       createFrame(vm, curThread, (ObjClosure*)method->obj, argNum);
	       LOAD_CUR_FRAME();   //加载最新的frame
	       break;

	    case MT_FN_CALL:
	       ASSERT(VALUE_IS_OBJCLOSURE(args[0]), "instance must be a closure!");
	       ObjFn* objFn = VALUE_TO_OBJCLOSURE(args[0])->fn;
	       //-1是去掉实例this
	       if (argNum - 1 < objFn->argNum) {
		  RUN_ERROR("arguments less");
	       }

	       STORE_CUR_FRAME();
	       createFrame(vm, curThread, VALUE_TO_OBJCLOSURE(args[0]), argNum);
	       LOAD_CUR_FRAME();   //加载最新的frame
	       break;

	    default:
	       NOT_REACHED();
	 }

	 LOOP();
      }
      
      CASE(LOAD_UPVALUE):
	 //指令流: 1字节的upvalue索引
	 PUSH(*((curFrame->closure->upvalues[READ_BYTE()])->localVarPtr));
	 LOOP();

      CASE(STORE_UPVALUE):
	 //栈顶: upvalue值
	 //指令流: 1字节的upvalue索引

	 *((curFrame->closure->upvalues[READ_BYTE()])->localVarPtr) = PEEK();
	 LOOP();

      CASE(LOAD_MODULE_VAR):
	 //指令流: 2字节的模块变量索引

	 PUSH(fn->module->moduleVarValue.datas[READ_SHORT()]);
	 LOOP();

      CASE(STORE_MODULE_VAR):
	 //栈顶: 模块变量值

	 fn->module->moduleVarValue.datas[READ_SHORT()] = PEEK();
	 LOOP();

      CASE(STORE_THIS_FIELD): {
	 //栈顶: field值
	 //指令流: 1字节的field索引

	 uint8_t fieldIdx = READ_BYTE();
	 ASSERT(VALUE_IS_OBJINSTANCE(stackStart[0]), "receiver should be instance!");
	 ObjInstance* objInstance = VALUE_TO_OBJINSTANCE(stackStart[0]); 
	 ASSERT(fieldIdx < objInstance->objHeader.class->fieldNum, "out of bounds field!");
	 objInstance->fields[fieldIdx] = PEEK();
	 LOOP();
      }

      CASE(LOAD_FIELD): {
	 //栈顶:实例对象
	 //指令流: 1字节的field索引

	 uint8_t fieldIdx = READ_BYTE();  //获取待加载的字段索引
	 Value receiver = POP();   //获取消息接收者
	 ASSERT(VALUE_IS_OBJINSTANCE(receiver), "receiver should be instance!");
	 ObjInstance* objInstance = VALUE_TO_OBJINSTANCE(receiver);
	 ASSERT(fieldIdx < objInstance->objHeader.class->fieldNum, "out of bounds field!");
	 PUSH(objInstance->fields[fieldIdx]);
	 LOOP();
      }      

      CASE(STORE_FIELD): {
	 //栈顶:实例对象 次栈顶:filed值 
	 //指令流: 1字节的field索引
	 
	 uint8_t fieldIdx = READ_BYTE();  //获取待加载的字段索引
	 Value receiver = POP();   //获取消息接收者
	 ASSERT(VALUE_IS_OBJINSTANCE(receiver), "receiver should be instance!");
	 ObjInstance* objInstance = VALUE_TO_OBJINSTANCE(receiver);
	 ASSERT(fieldIdx < objInstance->objHeader.class->fieldNum, "out of bounds field!");
	 objInstance->fields[fieldIdx] = PEEK();
	 LOOP();
      }
	 
      CASE(JUMP): {
	 //指令流: 2字节的跳转正偏移量

	 int16_t offset = READ_SHORT();
	 ASSERT(offset > 0, "OPCODE_JUMP`s operand must be positive!");
	 ip += offset;
	 LOOP();
      }

      CASE(LOOP): {
	 //指令流: 2字节的跳转正偏移量

	 int16_t offset = READ_SHORT();
	 ASSERT(offset > 0, "OPCODE_LOOP`s operand must be positive!");
	 ip -= offset;
	 LOOP();
      }

      CASE(JUMP_IF_FALSE): {
	 //栈顶: 跳转条件bool值
	 //指令流: 2字节的跳转偏移量

	 int16_t offset = READ_SHORT();
	 ASSERT(offset > 0, "OPCODE_JUMP_IF_FALSE`s operand must be positive!");
	 Value condition = POP();
	 if (VALUE_IS_FALSE(condition) || VALUE_IS_NULL(condition)) {
	    ip += offset;
	 }
	 LOOP();
      }

      CASE(AND): {
	 //栈顶: 跳转条件bool值
	 //指令流: 2字节的跳转偏移量

	 int16_t offset = READ_SHORT();
	 ASSERT(offset > 0, "OPCODE_AND`s operand must be positive!");
	 Value condition = PEEK();

	 if (VALUE_IS_FALSE(condition) || VALUE_IS_NULL(condition)) {
	    //若条件为假则不再计算and的右操作数,跳过右操作数的计算指令
	    ip += offset;
	 } else { 
	   //若条件为真则继续执行and右边的表达式计算步骤,丢掉栈顶的条件
	    DROP(); 
	 }
	 LOOP();
      }
	 
      CASE(OR): {
	 //栈顶: 跳转条件bool值
	 //指令流: 2字节的跳转偏移量

	 int16_t offset = READ_SHORT();
	 ASSERT(offset > 0, "OPCODE_OR`s operand must be positive!");
	 Value condition = PEEK();

	 if (VALUE_IS_FALSE(condition) || VALUE_IS_NULL(condition)) {
	    //若条件为假或空则执行or右操作数的计算步骤,丢掉跳转条件
	    DROP();
	 } else {
	   //若条件为真则跳过or右边的表达式,无须计算
	    ip += offset;
	 }
	 LOOP();
      }

      CASE(CLOSE_UPVALUE):
	 //栈顶: 相当于局部变量
	 //把地址大于栈顶局部变量的upvalue关闭
	 closeUpvalue(curThread, curThread->esp - 1);
	 DROP();   //弹出栈顶局部变量
	 LOOP();

      CASE(RETURN): {
	 //栈顶: 返回值

	 //获取返回值
	 Value retVal = POP();	    

	 //return是从函数返回 故该堆栈框架使用完毕,增加可用堆栈框架数量
	 curThread->usedFrameNum--;
	 
	 //关闭堆栈框架即此作用域内所有upvalue
	 closeUpvalue(curThread, stackStart);

	 //如果一个堆栈框架都没用,
	 //说明它没有调用函数或者所有的函数调用都返回了,可以结束它
	 if (curThread->usedFrameNum == 0) {
	    //如果并不是被另一线程调用的,就直接结束
	    if (curThread->caller == NULL) {
	       curThread->stack[0] = retVal;

	       //保留stack[0]中的结果,其它都丢弃
	       curThread->esp = curThread->stack + 1;
	       return VM_RESULT_SUCCESS;
	    }

	    //恢复主调方线程的调度
	    ObjThread* callerThread = curThread->caller;
	    curThread->caller = NULL;
	    curThread = callerThread;
	    vm->curThread = callerThread;

	    //在主调线程的栈顶存储被调线程的执行结果
	    curThread->esp[-1] = retVal;
	 } else {   
	    //将返回值置于运行时栈栈顶
	    stackStart[0] = retVal;
	    //回收堆栈:保留除结果所在的slot即stackStart[0] 其它全丢弃
	    curThread->esp = stackStart + 1;
	 }

	 LOAD_CUR_FRAME();
	 LOOP();
      }

      CASE(CONSTRUCT): {
	 //栈底: startStart[0]是class

	 ASSERT(VALUE_IS_CLASS(stackStart[0]), 
	       "stackStart[0] should be a class for OPCODE_CONSTRUCT!");

	 //将创建的类实例存储到stackStart[0],即this
	 ObjInstance* objInstance = newObjInstance(vm, VALUE_TO_CLASS(stackStart[0]));
	 //此时stackStart[0]是类,其类名便是方法所定义的类
	 //把对象写入stackStart[0]
	 stackStart[0] = OBJ_TO_VALUE(objInstance);

	 LOOP();
      }
	 
      CASE(CREATE_CLOSURE): {
	 //指令流: 2字节待创建闭包的函数在常量表中的索引+函数所用的upvalue数 * 2 

	 //endCompileUnit已经将闭包函数添加进了常量表
	 ObjFn* objFn =	VALUE_TO_OBJFN(fn->constants.datas[READ_SHORT()]);
	 ObjClosure* objClosure = newObjClosure(vm, objFn);
	 //将创建好的闭包的value结构压到栈顶,
	 //后续会有函数如defineMethod从栈底取出
	 //先将其压到栈中,后面再创建upvalue,这样可避免在创建upvalue过程中被GC
	 PUSH(OBJ_TO_VALUE(objClosure));
	 
	 uint32_t idx = 0;
	 while (idx < objFn->upvalueNum) {
	    //读入endCompilerUnit函数最后为每个upvale写入的数据对儿
	    uint8_t isEnclosingLocalVar = READ_BYTE();
	    uint8_t index = READ_BYTE();

	    if (isEnclosingLocalVar) {   //是直接外层的局部变量
	       //创建upvalue
	       objClosure->upvalues[idx] = 
		  createOpenUpvalue(vm, curThread, curFrame->stackStart + index); 
	    } else {
	       //直接从父编译单元中继承
	       objClosure->upvalues[idx] = curFrame->closure->upvalues[index];
	    }
	    idx++;
	 }

	 LOOP();
      }

      CASE(CREATE_CLASS): {
	 //指令流: 1字节的field数量
	 //栈顶: 基类  次栈顶: 子类名

	 uint32_t fieldNum = READ_BYTE();
	 Value superClass = curThread->esp[-1];  //基类名
	 Value className = curThread->esp[-2];  //子类名

	 //回收基类所占的栈空间,
	 //次栈顶的空间暂时保留,创建的类会直接用该空间.
	 DROP();

	 //校验基类合法性,若不合法则停止运行
	 validateSuperClass(vm, className, fieldNum, superClass);
	 Class* class = newClass(vm, VALUE_TO_OBJSTR(className),
	       fieldNum, VALUE_TO_CLASS(superClass));

	 //类存储于栈底
	 stackStart[0] = OBJ_TO_VALUE(class);

	 LOOP();
      }

      CASE(INSTANCE_METHOD):
      CASE(STATIC_METHOD): {
	 //指令流: 待绑定的方法"名字"在vm->allMethodNames中的2字节的索引
	 //栈顶: 待绑定的类  次栈顶: 待绑定的方法

	 //获得方法名的索引
	 uint32_t methodNameIndex = READ_SHORT();

	 //从栈顶中获得待绑定的类
	 Class* class = VALUE_TO_CLASS(PEEK());

	 //从次栈顶中获得待绑定的方法,
	 //这是由OPCODE_CREATE_CLOSURE操作码生成后压到栈中的
	 Value method = PEEK2();

	 bindMethodAndPatch(vm, opCode, methodNameIndex, class, method);
	 
	 DROP();
	 DROP();
	 LOOP();
      }

      CASE(END):
	 NOT_REACHED();
   }
   NOT_REACHED();

   #undef PUSH
   #undef POP
   #undef DROP
   #undef PEEK
   #undef PEEK2
   #undef LOAD_CUR_FRAME
   #undef STORE_CUR_FRAME
   #undef READ_BYTE
   #undef READ_SHORT
}
