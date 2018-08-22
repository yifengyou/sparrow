#include "core.h"
#include <string.h>
#include <sys/stat.h>
#include "utils.h"
#include "vm.h"
#include "obj_thread.h"
#include "compiler.h"
#include "core.script.inc"
#include <math.h>
#include "obj_range.h"
#include "obj_map.h"
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <time.h>

char* rootDir = NULL;   //根目录

#define CORE_MODULE VT_TO_VALUE(VT_NULL)

//返回值类型是Value类型,且是放在args[0], args是Value数组
//RET_VALUE的参数就是Value类型,无须转换直接赋值.
//它是后面"RET_其它类型"的基础
#define RET_VALUE(value)\
   do {\
      args[0] = value;\
      return true;\
   } while(0);

//将obj转换为Value后做为返回值
#define RET_OBJ(objPtr) RET_VALUE(OBJ_TO_VALUE(objPtr))

//将各种值转为Value后做为返回值
#define RET_BOOL(boolean) RET_VALUE(BOOL_TO_VALUE(boolean))
#define RET_NUM(num) RET_VALUE(NUM_TO_VALUE(num))
#define RET_NULL RET_VALUE(VT_TO_VALUE(VT_NULL))
#define RET_TRUE RET_VALUE(VT_TO_VALUE(VT_TRUE))
#define RET_FALSE RET_VALUE(VT_TO_VALUE(VT_FALSE))

//设置线程报错
#define SET_ERROR_FALSE(vmPtr, errMsg) \
   do {\
      vmPtr->curThread->errorObj = \
	 OBJ_TO_VALUE(newObjString(vmPtr, errMsg, strlen(errMsg)));\
      return false;\
   } while(0);

//绑定方法func到classPtr指向的类
#define PRIM_METHOD_BIND(classPtr, methodName, func) {\
   uint32_t length = strlen(methodName);\
   int globalIdx = getIndexFromSymbolTable(&vm->allMethodNames, methodName, length);\
   if (globalIdx == -1) {\
      globalIdx = addSymbol(vm, &vm->allMethodNames, methodName, length);\
   }\
   Method method;\
   method.type = MT_PRIMITIVE;\
   method.primFn = func;\
   bindMethod(vm, classPtr, (uint32_t)globalIdx, method);\
}

//读取源代码文件
char* readFile(const char* path) {
   FILE* file = fopen(path, "r");
   if (file == NULL) {
      IO_ERROR("Could`t open file \"%s\".\n", path);
   }

   struct stat fileStat;
   stat(path, &fileStat);
   size_t fileSize = fileStat.st_size;
   char* fileContent = (char*)malloc(fileSize + 1);
   if (fileContent == NULL) {
      MEM_ERROR("Could`t allocate memory for reading file \"%s\".\n", path);
   }

   size_t numRead = fread(fileContent, sizeof(char), fileSize, file);
   if (numRead < fileSize) {
      IO_ERROR("Could`t read file \"%s\".\n", path);
   }
   fileContent[fileSize] = '\0';
   
   fclose(file);
   return fileContent;
}

//将数字转换为字符串
static ObjString* num2str(VM* vm, double num) {
   //nan不是一个确定的值,因此nan和nan是不相等的
   if (num != num) {
      return newObjString(vm, "nan", 3);
   }

   if (num == INFINITY) {
      return newObjString(vm, "infinity", 8);
   }

   if (num == -INFINITY) {
      return newObjString(vm, "-infinity", 9);
   }

  //以下24字节的缓冲区足以容纳双精度到字符串的转换
  char buf[24] = {'\0'};
  int len = sprintf(buf, "%.14g", num);
  return newObjString(vm, buf, len);
}

//判断arg是否为数字
static bool validateNum(VM* vm, Value arg) {
   if (VALUE_IS_NUM(arg)) {
      return true;   
   }
   SET_ERROR_FALSE(vm, "argument must be number!");
}

//判断arg是否为字符串
static bool validateString(VM* vm, Value arg) {
   if (VALUE_IS_OBJSTR(arg)) {
      return true; 
   }
   SET_ERROR_FALSE(vm, "argument must be string!");
}

//校验arg是否为函数
static bool validateFn(VM* vm, Value arg) {
   if (VALUE_TO_OBJCLOSURE(arg)) {
      return true;
   }
   vm->curThread->errorObj = 
      OBJ_TO_VALUE(newObjString(vm, "argument must be a function!", 28));
   return false;
}

//返回核心类name的value结构
static Value getCoreClassValue(ObjModule* objModule, const char* name) {
   int index = getIndexFromSymbolTable(&objModule->moduleVarName, name, strlen(name));
   if (index == -1) {
      char id[MAX_ID_LEN] = {'\0'};
      memcpy(id, name, strlen(name));
      RUN_ERROR("something wrong occur: missing core class \"%s\"!", id);
   }
   return objModule->moduleVarValue.datas[index];
}

//!object: object取反,结果为false
static bool primObjectNot(VM* vm UNUSED, Value* args) {
   RET_VALUE(VT_TO_VALUE(VT_FALSE));
}

//args[0] == args[1]: 返回object是否相等
static bool primObjectEqual(VM* vm UNUSED, Value* args) {
   Value boolValue = BOOL_TO_VALUE(valueIsEqual(args[0], args[1]));
   RET_VALUE(boolValue);
}

//args[0] != args[1]: 返回object是否不等
static bool primObjectNotEqual(VM* vm UNUSED, Value* args) {
   Value boolValue = BOOL_TO_VALUE(!valueIsEqual(args[0], args[1]));
   RET_VALUE(boolValue);
}

//args[0] is args[1]:类args[0]是否为类args[1]的子类
static bool primObjectIs(VM* vm, Value* args) {
   //args[1]必须是class
   if (!VALUE_IS_CLASS(args[1])) {
      RUN_ERROR("argument must be class!");
   }
   
   Class* thisClass = getClassOfObj(vm, args[0]);
   Class* baseClass = (Class*)(args[1].objHeader);

   //有可能是多级继承,因此自下而上遍历基类链
   while (baseClass != NULL) {

      //在某一级基类找到匹配就设置返回值为VT_TRUE并返回
      if (thisClass == baseClass) {
	 RET_VALUE(VT_TO_VALUE(VT_TRUE));
      }
      baseClass = baseClass->superClass;
   }

   //若未找到基类,说明不具备is_a关系
   RET_VALUE(VT_TO_VALUE(VT_FALSE));
}

//args[0].tostring: 返回args[0]所属class的名字
static bool primObjectToString(VM* vm UNUSED, Value* args) {
   Class* class = args[0].objHeader->class;
   Value nameValue = OBJ_TO_VALUE(class->name);
   RET_VALUE(nameValue);
}

//args[0].type:返回对象args[0]的类
static bool primObjectType(VM* vm, Value* args) {
   Class* class = getClassOfObj(vm, args[0]);
   RET_OBJ(class);
}

//args[0].name: 返回类名
static bool primClassName(VM* vm UNUSED, Value* args) {
   RET_OBJ(VALUE_TO_CLASS(args[0])->name);
}

//args[0].supertype: 返回args[0]的基类
static bool primClassSupertype(VM* vm UNUSED, Value* args) {
   Class* class = VALUE_TO_CLASS(args[0]);
   if (class->superClass != NULL) {
      RET_OBJ(class->superClass);
   } 
   RET_VALUE(VT_TO_VALUE(VT_NULL));
}

//args[0].toString: 返回类名
static bool primClassToString(VM* vm UNUSED, Value* args) {
   RET_OBJ(VALUE_TO_CLASS(args[0])->name);
}

//args[0].same(args[1], args[2]): 返回args[1]和args[2]是否相等
static bool primObjectmetaSame(VM* vm UNUSED, Value* args) {
   Value boolValue = BOOL_TO_VALUE(valueIsEqual(args[1], args[2]));
   RET_VALUE(boolValue);
}

//返回bool的字符串形式:"true"或"false"
static bool primBoolToString(VM* vm, Value* args) {
   ObjString* objString;
   if (VALUE_TO_BOOL(args[0])) {  //若为VT_TRUE
      objString = newObjString(vm, "true", 4);
   } else {
      objString = newObjString(vm, "false", 5);
   }
   RET_OBJ(objString);
}

//bool值取反
static bool primBoolNot(VM* vm UNUSED, Value* args) {
   RET_BOOL(!VALUE_TO_BOOL(args[0]));
}

//以下以大写字符开头的为类名,表示类(静态)方法调用

//Thread.new(func):创建一个thread实例
static bool primThreadNew(VM* vm, Value* args) {
   //代码块为参数必为闭包
   if (!validateFn(vm, args[1])) {
      return false;
   }
   
   ObjThread* objThread = newObjThread(vm, VALUE_TO_OBJCLOSURE(args[1]));

   //使stack[0]为接收者,保持栈平衡
   objThread->stack[0] = VT_TO_VALUE(VT_NULL);
   objThread->esp++;
   RET_OBJ(objThread);
}

//Thread.abort(err):以错误信息err为参数退出线程
static bool primThreadAbort(VM* vm, Value* args) {
   //此函数后续未处理,暂时放着
   vm->curThread->errorObj = args[1]; //保存退出参数
   return VALUE_IS_NULL(args[1]);
}

//Thread.current:返回当前的线程
static bool primThreadCurrent(VM* vm, Value* args UNUSED) {
   RET_OBJ(vm->curThread);
}

//Thread.suspend():挂起线程,退出解析器
static bool primThreadSuspend(VM* vm, Value* args UNUSED) {
   //目前suspend操作只会退出虚拟机,
   //使curThread为NULL,虚拟机将退出
   vm->curThread = NULL;
   return false;
}

//Thread.yield(arg)带参数让出cpu
static bool primThreadYieldWithArg(VM* vm, Value* args) {
   ObjThread* curThread = vm->curThread;
   vm->curThread = curThread->caller;   //使cpu控制权回到主调方

   curThread->caller = NULL;  //与调用者断开联系

   if (vm->curThread != NULL) { 
      //如果当前线程有主调方,就将当前线程的返回值放在主调方的栈顶
      vm->curThread->esp[-1] = args[1];
      
      //对于"thread.yield(arg)"来说, 回收arg的空间,
      //保留thread参数所在的空间,将来唤醒时用于存储yield结果
      curThread->esp--;
   }
   return false;
}

//Thread.yield() 无参数让出cpu
static bool primThreadYieldWithoutArg(VM* vm, Value* args UNUSED) {
   ObjThread* curThread = vm->curThread;
   vm->curThread = curThread->caller;   //使cpu控制权回到主调方

   curThread->caller = NULL;  //与调用者断开联系

   if (vm->curThread != NULL) { 
      //为保持通用的栈结构,如果当前线程有主调方,
      //就将空值做为返回值放在主调方的栈顶
      vm->curThread->esp[-1] = VT_TO_VALUE(VT_NULL) ;
   }
   return false;
}

//切换到下一个线程nextThread
static bool switchThread(VM* vm, 
      ObjThread* nextThread, Value* args, bool withArg) {
   //在下一线程nextThread执行之前,其主调线程应该为空
   if (nextThread->caller != NULL) {
      RUN_ERROR("thread has been called!");
   }
   nextThread->caller = vm->curThread;

   if (nextThread->usedFrameNum == 0) {
      //只有已经运行完毕的thread的usedFrameNum才为0
      SET_ERROR_FALSE(vm, "a finished thread can`t be switched to!");
   }

   if (!VALUE_IS_NULL(nextThread->errorObj)) {
      //Thread.abort(arg)会设置errorObj, 不能切换到abort的线程
      SET_ERROR_FALSE(vm, "a aborted thread can`t be switched to!");
   }

   //如果call有参数,回收参数的空间,
   //只保留次栈顶用于存储nextThread返回后的结果
   if (withArg) {
      vm->curThread->esp--;
   }

   ASSERT(nextThread->esp > nextThread->stack, "esp should be greater than stack!"); 
   //nextThread.call(arg)中的arg做为nextThread.yield的返回值
   //存储到nextThread的栈顶,否则压入null保持栈平衡
   nextThread->esp[-1] = withArg ? args[1] : VT_TO_VALUE(VT_NULL);

   //使当前线程指向nextThread,使之成为就绪
   vm->curThread = nextThread;

   //返回false以进入vm中的切换线程流程
   return false;
}

//objThread.call()
static bool primThreadCallWithoutArg(VM* vm, Value* args) {
   return switchThread(vm, VALUE_TO_OBJTHREAD(args[0]), args, false);
}

//objThread.call(arg)
static bool primThreadCallWithArg(VM* vm, Value* args) {
   return switchThread(vm, VALUE_TO_OBJTHREAD(args[0]), args, true);
}

//objThread.isDone返回线程是否运行完成
static bool primThreadIsDone(VM* vm UNUSED, Value* args) {
   //获取.isDone的调用者
   ObjThread* objThread = VALUE_TO_OBJTHREAD(args[0]);
   RET_BOOL(objThread->usedFrameNum == 0 || !VALUE_IS_NULL(objThread->errorObj));
}

//Fn.new(_):新建一个函数对象
static bool primFnNew(VM* vm, Value* args) {
   //代码块为参数必为闭包
   if (!validateFn(vm, args[1])) return false;

   //直接返回函数闭包
   RET_VALUE(args[1]);
}

//null取非
static bool primNullNot(VM* vm UNUSED, Value* args UNUSED) {
   RET_VALUE(BOOL_TO_VALUE(true));
}

//null的字符串化
static bool primNullToString(VM* vm, Value* args UNUSED) {
   ObjString* objString = newObjString(vm, "null", 4);
   RET_OBJ(objString);
}

//将字符串转换为数字
static bool primNumFromString(VM* vm, Value* args) {
   if (!validateString(vm, args[1])) {
      return false;
   }
   
   ObjString* objString = VALUE_TO_OBJSTR(args[1]);

   //空字符串返回RETURN_NULL
   if (objString->value.length == 0) {
      RET_NULL;
   }

   ASSERT(objString->value.start[objString->value.length] == '\0', "objString don`t teminate!");

   errno = 0;
   char* endPtr;

   //将字符串转换为double型, 它会自动跳过前面的空白
   double num = strtod(objString->value.start, &endPtr);
   
   //以endPtr是否等于start+length来判断不能转换的字符之后是否全是空白
   while (*endPtr != '\0' && isspace((unsigned char)*endPtr)) {
      endPtr++;   
   }
   
   if (errno == ERANGE) {
      RUN_ERROR("string too large!"); 
   }

   //如果字符串中不能转换的字符不全是空白,字符串非法,返回NULL
   if (endPtr < objString->value.start + objString->value.length) {
      RET_NULL;   
   }

   //至此,检查通过,返回正确结果
   RET_NUM(num);
}

//返回圆周率
static bool primNumPi(VM* vm UNUSED, Value* args UNUSED) {
   RET_NUM(3.14159265358979323846);
}

#define PRIM_NUM_INFIX(name, operator, type) \
   static bool name(VM* vm, Value* args) {\
      if (!validateNum(vm, args[1])) {\
	 return false; \
      }\
      RET_##type(VALUE_TO_NUM(args[0]) operator VALUE_TO_NUM(args[1]));\
   }

PRIM_NUM_INFIX(primNumPlus,   +, NUM);
PRIM_NUM_INFIX(primNumMinus,  -, NUM);
PRIM_NUM_INFIX(primNumMul,    *, NUM);
PRIM_NUM_INFIX(primNumDiv,    /, NUM);
PRIM_NUM_INFIX(primNumGt,   >, BOOL);
PRIM_NUM_INFIX(primNumGe,  >=, BOOL);
PRIM_NUM_INFIX(primNumLt,   <, BOOL);
PRIM_NUM_INFIX(primNumLe,  <=, BOOL);
#undef PRIM_NUM_INFIX

 #define PRIM_NUM_BIT(name, operator) \
   static bool name(VM* vm UNUSED, Value* args) {\
      if (!validateNum(vm, args[1])) {\
	 return false;\
      }\
      uint32_t leftOperand = VALUE_TO_NUM(args[0]); \
      uint32_t rightOperand = VALUE_TO_NUM(args[1]); \
      RET_NUM(leftOperand operator rightOperand);\
   }

PRIM_NUM_BIT(primNumBitAnd, &);
PRIM_NUM_BIT(primNumBitOr, |);
PRIM_NUM_BIT(primNumBitShiftRight, >>);
PRIM_NUM_BIT(primNumBitShiftLeft, <<);
#undef PRIM_NUM_BIT

//使用数学库函数
#define PRIM_NUM_MATH_FN(name, mathFn) \
   static bool name(VM* vm UNUSED, Value* args) {\
      RET_NUM(mathFn(VALUE_TO_NUM(args[0]))); \
   }

PRIM_NUM_MATH_FN(primNumAbs,	 fabs);
PRIM_NUM_MATH_FN(primNumAcos,	 acos);
PRIM_NUM_MATH_FN(primNumAsin,    asin);
PRIM_NUM_MATH_FN(primNumAtan,    atan);
PRIM_NUM_MATH_FN(primNumCeil,    ceil);
PRIM_NUM_MATH_FN(primNumCos,     cos);
PRIM_NUM_MATH_FN(primNumFloor,   floor);
PRIM_NUM_MATH_FN(primNumNegate,  -);
PRIM_NUM_MATH_FN(primNumSin,     sin);
PRIM_NUM_MATH_FN(primNumSqrt,    sqrt);  //开方
PRIM_NUM_MATH_FN(primNumTan,     tan);
#undef PRIM_NUM_MATH_FN

//这里用fmod实现浮点取模
static bool primNumMod(VM* vm UNUSED, Value* args) {
   if (!validateNum(vm, args[1])) {
      return false;
   }
   RET_NUM(fmod(VALUE_TO_NUM(args[0]), VALUE_TO_NUM(args[1])));
}

//数字取反
static bool primNumBitNot(VM* vm UNUSED, Value* args) {
   RET_NUM(~(uint32_t)VALUE_TO_NUM(args[0]));
}

//[数字from..数字to]
static bool primNumRange(VM* vm UNUSED, Value* args) {
   if (!validateNum(vm, args[1])) {
      return false;
   }

   double from = VALUE_TO_NUM(args[0]); 
   double to = VALUE_TO_NUM(args[1]); 
   RET_OBJ(newObjRange(vm, from, to));
}

//atan2(args[1])
static bool primNumAtan2(VM* vm UNUSED, Value* args) {
   if (!validateNum(vm, args[1])) {
      return false;
   }
   
   RET_NUM(atan2(VALUE_TO_NUM(args[0]), VALUE_TO_NUM(args[1])));
}

//返回小数部分
static bool primNumFraction(VM* vm UNUSED, Value* args) {
   double dummyInteger;
   RET_NUM(modf(VALUE_TO_NUM(args[0]), &dummyInteger));
}

//判断数字是否无穷大,不区分正负无穷大 
static bool primNumIsInfinity(VM* vm UNUSED, Value* args) {
   RET_BOOL(isinf(VALUE_TO_NUM(args[0])));  
}

//判断是否为数字
static bool primNumIsInteger(VM* vm UNUSED, Value* args) {
   double num = VALUE_TO_NUM(args[0]);
   //如果是nan(不是一个数字)或无限大的数字就返回false
   if (isnan(num) || isinf(num)) {
      RET_FALSE;
   }
   RET_BOOL(trunc(num) == num);
}

//判断数字是否为nan
static bool primNumIsNan(VM* vm UNUSED, Value* args) {
   RET_BOOL(isnan(VALUE_TO_NUM(args[0])));
}

//数字转换为字符串
static bool primNumToString(VM* vm UNUSED, Value* args) {
   RET_OBJ(num2str(vm, VALUE_TO_NUM(args[0])));
}

//取数字的整数部分
static bool primNumTruncate(VM* vm UNUSED, Value* args) {
   double integer;
   modf(VALUE_TO_NUM(args[0]), &integer);
   RET_NUM(integer);
}

//判断两个数字是否相等
static bool primNumEqual(VM* vm UNUSED, Value* args) {
   if (!validateNum(vm, args[1])) {
      RET_FALSE;
   }

   RET_BOOL(VALUE_TO_NUM(args[0]) == VALUE_TO_NUM(args[1]));
}

//判断两个数字是否不等
static bool primNumNotEqual(VM* vm UNUSED, Value* args) {
   if (!validateNum(vm, args[1])) {
      RET_TRUE;
   }
   RET_BOOL(VALUE_TO_NUM(args[0]) != VALUE_TO_NUM(args[1]));
}

//从modules中获取名为moduleName的模块
static ObjModule* getModule(VM* vm, Value moduleName) {
   Value value = mapGet(vm->allModules, moduleName);
   if (value.type == VT_UNDEFINED) {
      return NULL;
   }
   
   return VALUE_TO_OBJMODULE(value);
}

//载入模块moduleName并编译
static ObjThread* loadModule(VM* vm, Value moduleName, const char* moduleCode) {
   //确保模块已经载入到 vm->allModules
   //先查看是否已经导入了该模块,避免重新导入
   ObjModule* module = getModule(vm, moduleName);

   //若该模块未加载先将其载入,并继承核心模块中的变量
   if (module == NULL) {
      //创建模块并添加到vm->allModules
      ObjString* modName = VALUE_TO_OBJSTR(moduleName);
      ASSERT(modName->value.start[modName->value.length] == '\0', "string.value.start is not terminated!");

      module = newObjModule(vm, modName->value.start);
      mapSet(vm, vm->allModules, moduleName, OBJ_TO_VALUE(module));
      
      //继承核心模块中的变量
      ObjModule* coreModule = getModule(vm, CORE_MODULE);
      uint32_t idx = 0;
      while (idx < coreModule->moduleVarName.count) {
	 defineModuleVar(vm, module,
	       coreModule->moduleVarName.datas[idx].str,
	       strlen(coreModule->moduleVarName.datas[idx].str),
	       coreModule->moduleVarValue.datas[idx]);
	 idx++; 
      }
   }

   ObjFn* fn = compileModule(vm, module, moduleCode);
   ObjClosure* objClosure = newObjClosure(vm, fn);
   ObjThread* moduleThread = newObjThread(vm, objClosure);

   return moduleThread;  
}

//执行模块
VMResult executeModule(VM* vm, Value moduleName, const char* moduleCode) {
   ObjThread* objThread = loadModule(vm, moduleName, moduleCode);
   return executeInstruction(vm, objThread);
}

//table中查找符号symbol 找到后返回索引,否则返回-1
int getIndexFromSymbolTable(SymbolTable* table, const char* symbol, uint32_t length) {
   ASSERT(length != 0, "length of symbol is 0!");
   uint32_t index = 0;
   while (index < table->count) {
      if (length == table->datas[index].length &&
	    memcmp(table->datas[index].str, symbol, length) == 0) {
	 return index;
      }
      index++;
   }
   return -1;
}

//往table中添加符号symbol,返回其索引
int addSymbol(VM* vm, SymbolTable* table, const char* symbol, uint32_t length) {
   ASSERT(length != 0, "length of symbol is 0!");
   String string;
   string.str = ALLOCATE_ARRAY(vm, char, length + 1);
   memcpy(string.str, symbol, length);
   string.str[length] = '\0';
   string.length = length;
   StringBufferAdd(vm, table, string);
   return table->count - 1;
}

//确保符号已添加到符号表
int ensureSymbolExist(VM* vm, SymbolTable* table, const char* symbol, uint32_t length) {
   int symbolIndex = getIndexFromSymbolTable(table, symbol, length);
   if (symbolIndex == -1) {
      return addSymbol(vm, table, symbol, length);
   }
   return symbolIndex;
}

//定义类
static Class* defineClass(VM* vm, ObjModule* objModule, const char* name) {
   //1先创建类
   Class* class = newRawClass(vm, name, 0);

   //2把类做为普通变量在模块中定义
   defineModuleVar(vm, objModule, name, strlen(name), OBJ_TO_VALUE(class));
   return class;
}

//使class->methods[index]=method
void bindMethod(VM* vm, Class* class, uint32_t index, Method method) {
   if (index >= class->methods.count) {
      Method emptyPad = {MT_NONE, {0}};
      MethodBufferFillWrite(vm, &class->methods, emptyPad, index - class->methods.count + 1); 
   }
   class->methods.datas[index] = method;
}

//绑定基类
void bindSuperClass(VM* vm, Class* subClass, Class* superClass) {
   subClass->superClass = superClass;

   //继承基类属性数
   subClass->fieldNum += superClass->fieldNum;

   //继承基类方法
   uint32_t idx = 0;
   while (idx < superClass->methods.count) {
      bindMethod(vm, subClass, idx, superClass->methods.datas[idx]); 
      idx++;
   }
}

//绑定fn.call的重载
static void bindFnOverloadCall(VM* vm, const char* sign) {
   uint32_t index = ensureSymbolExist(vm, &vm->allMethodNames, sign, strlen(sign));
   //构造method
   Method method = {MT_FN_CALL, {0}};
   bindMethod(vm, vm->fnClass, index, method);
}

//编译核心模块
void buildCore(VM* vm) {

   //核心模块不需要名字,模块也允许名字为空
   ObjModule* coreModule = newObjModule(vm, NULL);

   //创建核心模块,录入到vm->allModules
   mapSet(vm, vm->allModules, CORE_MODULE, OBJ_TO_VALUE(coreModule));

   //创建object类并绑定方法
   vm->objectClass = defineClass(vm, coreModule, "object");
   PRIM_METHOD_BIND(vm->objectClass, "!", primObjectNot);
   PRIM_METHOD_BIND(vm->objectClass, "==(_)", primObjectEqual);
   PRIM_METHOD_BIND(vm->objectClass, "!=(_)", primObjectNotEqual);
   PRIM_METHOD_BIND(vm->objectClass, "is(_)", primObjectIs);
   PRIM_METHOD_BIND(vm->objectClass, "toString", primObjectToString);
   PRIM_METHOD_BIND(vm->objectClass, "type", primObjectType);

   //定义classOfClass类,它是所有meta类的meta类和基类
   vm->classOfClass = defineClass(vm, coreModule, "class");

   //objectClass是任何类的基类 
   bindSuperClass(vm, vm->classOfClass, vm->objectClass);

   PRIM_METHOD_BIND(vm->classOfClass, "name", primClassName);
   PRIM_METHOD_BIND(vm->classOfClass, "supertype", primClassSupertype);
   PRIM_METHOD_BIND(vm->classOfClass, "toString", primClassToString);

   //定义object类的元信息类objectMetaclass,它无须挂载到vm
   Class* objectMetaclass = defineClass(vm, coreModule, "objectMeta");
   
   //classOfClass类是所有meta类的meta类和基类
   bindSuperClass(vm, objectMetaclass, vm->classOfClass);

   //类型比较
   PRIM_METHOD_BIND(objectMetaclass, "same(_,_)", primObjectmetaSame);

   //绑定各自的meta类
   vm->objectClass->objHeader.class = objectMetaclass;
   objectMetaclass->objHeader.class = vm->classOfClass;
   vm->classOfClass->objHeader.class = vm->classOfClass; //元信息类回路,meta类终点
   
   //执行核心模块
   executeModule(vm, CORE_MODULE, coreModuleCode);
   
   //Bool类定义在core.script.inc中,将其挂载Bool类到vm->boolClass
   vm->boolClass = VALUE_TO_CLASS(getCoreClassValue(coreModule, "Bool"));
   PRIM_METHOD_BIND(vm->boolClass, "toString", primBoolToString);
   PRIM_METHOD_BIND(vm->boolClass, "!", primBoolNot);

   //Thread类也是在core.script.inc中定义的,
   //将其挂载到vm->threadClass并补充原生方法
   vm->threadClass = VALUE_TO_CLASS(getCoreClassValue(coreModule, "Thread"));
   //以下是类方法
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "new(_)", primThreadNew);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "abort(_)", primThreadAbort);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "current", primThreadCurrent);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "suspend()", primThreadSuspend);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "yield(_)", primThreadYieldWithArg);
   PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "yield()", primThreadYieldWithoutArg);
   //以下是实例方法
   PRIM_METHOD_BIND(vm->threadClass, "call()", primThreadCallWithoutArg);
   PRIM_METHOD_BIND(vm->threadClass, "call(_)", primThreadCallWithArg);
   PRIM_METHOD_BIND(vm->threadClass, "isDone", primThreadIsDone);

   //绑定函数类
   vm->fnClass = VALUE_TO_CLASS(getCoreClassValue(coreModule, "Fn"));
   PRIM_METHOD_BIND(vm->fnClass->objHeader.class, "new(_)", primFnNew);

   //绑定call的重载方法
   bindFnOverloadCall(vm, "call()");
   bindFnOverloadCall(vm, "call(_)");
   bindFnOverloadCall(vm, "call(_,_)");
   bindFnOverloadCall(vm, "call(_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)");
   bindFnOverloadCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)");

   //绑定Null类的方法
   vm->nullClass = VALUE_TO_CLASS(getCoreClassValue(coreModule, "Null"));
   PRIM_METHOD_BIND(vm->nullClass, "!", primNullNot);
   PRIM_METHOD_BIND(vm->nullClass, "toString", primNullToString);

   //绑定num类方法
   vm->numClass = VALUE_TO_CLASS(getCoreClassValue(coreModule, "Num"));
   //类方法
   PRIM_METHOD_BIND(vm->numClass->objHeader.class, "fromString(_)", primNumFromString);
   PRIM_METHOD_BIND(vm->numClass->objHeader.class, "pi", primNumPi);
   //实例方法 
   PRIM_METHOD_BIND(vm->numClass, "+(_)", primNumPlus);
   PRIM_METHOD_BIND(vm->numClass, "-(_)", primNumMinus);
   PRIM_METHOD_BIND(vm->numClass, "*(_)", primNumMul);
   PRIM_METHOD_BIND(vm->numClass, "/(_)", primNumDiv);
   PRIM_METHOD_BIND(vm->numClass, ">(_)", primNumGt);
   PRIM_METHOD_BIND(vm->numClass, ">=(_)", primNumGe);
   PRIM_METHOD_BIND(vm->numClass, "<(_)", primNumLt);
   PRIM_METHOD_BIND(vm->numClass, "<=(_)", primNumLe);

   //位运算
   PRIM_METHOD_BIND(vm->numClass, "&(_)", primNumBitAnd);
   PRIM_METHOD_BIND(vm->numClass, "|(_)", primNumBitOr);
   PRIM_METHOD_BIND(vm->numClass, ">>(_)", primNumBitShiftRight);
   PRIM_METHOD_BIND(vm->numClass, "<<(_)", primNumBitShiftLeft);
   //以上都是通过rules中INFIX_OPERATOR来解析的

   //下面大多数方法是通过rules中'.'对应的led(callEntry)来解析,
   //少数符号依然是INFIX_OPERATOR解析
   PRIM_METHOD_BIND(vm->numClass, "abs", primNumAbs);
   PRIM_METHOD_BIND(vm->numClass, "acos", primNumAcos);
   PRIM_METHOD_BIND(vm->numClass, "asin", primNumAsin);
   PRIM_METHOD_BIND(vm->numClass, "atan", primNumAtan);
   PRIM_METHOD_BIND(vm->numClass, "ceil", primNumCeil);
   PRIM_METHOD_BIND(vm->numClass, "cos", primNumCos);
   PRIM_METHOD_BIND(vm->numClass, "floor", primNumFloor);
   PRIM_METHOD_BIND(vm->numClass, "-", primNumNegate);
   PRIM_METHOD_BIND(vm->numClass, "sin", primNumSin);
   PRIM_METHOD_BIND(vm->numClass, "sqrt", primNumSqrt);
   PRIM_METHOD_BIND(vm->numClass, "tan", primNumTan);
   PRIM_METHOD_BIND(vm->numClass, "%(_)", primNumMod);
   PRIM_METHOD_BIND(vm->numClass, "~", primNumBitNot);
   PRIM_METHOD_BIND(vm->numClass, "..(_)", primNumRange);
   PRIM_METHOD_BIND(vm->numClass, "atan(_)", primNumAtan2);
   PRIM_METHOD_BIND(vm->numClass, "fraction", primNumFraction);
   PRIM_METHOD_BIND(vm->numClass, "isInfinity", primNumIsInfinity);
   PRIM_METHOD_BIND(vm->numClass, "isInteger", primNumIsInteger);
   PRIM_METHOD_BIND(vm->numClass, "isNan", primNumIsNan);
   PRIM_METHOD_BIND(vm->numClass, "toString", primNumToString);
   PRIM_METHOD_BIND(vm->numClass, "truncate", primNumTruncate);
   PRIM_METHOD_BIND(vm->numClass, "==(_)", primNumEqual);
   PRIM_METHOD_BIND(vm->numClass, "!=(_)", primNumNotEqual);
}
