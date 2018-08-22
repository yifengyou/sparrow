#include "compiler.h"
#include "parser.h"
#include "core.h"
#include <string.h>
#if DEBUG
   #include "debug.h"
#endif

struct compileUnit {
   // 所编译的函数
   ObjFn* fn;

   //作用域中允许的局部变量的个量上限
   LocalVar localVars[MAX_LOCAL_VAR_NUM];

   //已分配的局部变量个数
   uint32_t localVarNum;

   //记录本层函数所引用的upvalue
   Upvalue upvalues[MAX_UPVALUE_NUM];

  //此项表示当前正在编译的代码所处的作用域,
   int scopeDepth;
 
   //当前使用的slot个数
   uint32_t stackSlotNum;

  //当前正在编译的循环层
   Loop* curLoop;

   //当前正编译的类的编译信息
   ClassBookKeep* enclosingClassBK;

   //包含此编译单元的编译单元,即直接外层
   struct compileUnit* enclosingUnit;

   //当前parser
   Parser* curParser;
};  //编译单元

//在模块objModule中定义名为name,值为value的模块变量
int defineModuleVar(VM* vm, ObjModule* objModule,
      const char* name, uint32_t length, Value value) {
   if (length > MAX_ID_LEN) {
      //也许name指向的变量名并不以'\0'结束,将其从源码串中拷贝出来
      char id[MAX_ID_LEN] = {'\0'};
      memcpy(id, name, length);

      //本函数可能是在编译源码文件之前调用的,
      //那时还没有创建parser, 因此报错要分情况:
      if (vm->curParser != NULL) {   //编译源码文件
	 COMPILE_ERROR(vm->curParser, 
	       "length of identifier \"%s\" should be no more than %d", id, MAX_ID_LEN);
      } else {   // 编译源码前调用,比如加载核心模块时会调用本函数
	 MEM_ERROR("length of identifier \"%s\" should be no more than %d", id, MAX_ID_LEN);
      }
   }

   //从模块变量名中查找变量,若不存在就添加
   int symbolIndex = getIndexFromSymbolTable(&objModule->moduleVarName, name, length);
   if (symbolIndex == -1) {  
      //添加变量名
      symbolIndex = addSymbol(vm, &objModule->moduleVarName, name, length);
      //添加变量值
      ValueBufferAdd(vm, &objModule->moduleVarValue, value);

   } else if (VALUE_IS_NUM(objModule->moduleVarValue.datas[symbolIndex])) {
      //若遇到之前预先声明的模块变量的定义,在此为其赋予正确的值
      objModule->moduleVarValue.datas[symbolIndex] = value; 

   } else {
      symbolIndex = -1;  //已定义则返回-1,用于判断重定义
   }

   return symbolIndex;
}
