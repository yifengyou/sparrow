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

//把opcode定义到数组opCodeSlotsUsed中
#define OPCODE_SLOTS(opCode, effect) effect,  
static const int opCodeSlotsUsed[] = {
   #include "opcode.inc"
};
#undef OPCODE_SLOTS

//初始化CompileUnit
static void initCompileUnit(Parser* parser, CompileUnit* cu,
      CompileUnit* enclosingUnit, bool isMethod) {
   parser->curCompileUnit = cu;
   cu->curParser = parser;
   cu->enclosingUnit = enclosingUnit;
   cu->curLoop = NULL;
   cu->enclosingClassBK = NULL;

   //若没有外层,说明当前属于模块作用域
   if (enclosingUnit == NULL) {
      //编译代码时是从上到下从最外层的模块作用域开始,模块作用域设为-1
      cu->scopeDepth = -1;
      //模块级作用域中没有局部变量
      cu->localVarNum = 0;

   } else {   //若是内层单元,属局部作用域
      if (isMethod) {  //若是类中的方法
	 //如果是类的方法就设定隐式"this"为第0个局部变量,即实例对象,
	 //它是方法(消息)的接收者.this这种特殊对象被处理为局部变量
	 cu->localVars[0].name = "this"; 
	 cu->localVars[0].length = 4; 

      } else {	  //若为普通函数
	 //空出第0个局部变量,保持统一
	 cu->localVars[0].name = NULL; 
	 cu->localVars[0].length = 0; 
      }

      //第0个局部变量的特殊性使其作用域为模块级别
      cu->localVars[0].scopeDepth = -1; 
      cu->localVars[0].isUpvalue = false; 
      cu->localVarNum = 1;  //localVars[0]被分配
      // 对于函数和方法来说,初始作用域是局部作用域
      // 0表示局部作用域的最外层
      cu->scopeDepth = 0; 
   }

   //局部变量保存在栈中,初始时栈中已使用的slot数量等于局部变量的数量
   cu->stackSlotNum = cu->localVarNum;

   cu->fn = newObjFn(cu->curParser->vm, cu->curParser->curModule, cu->localVarNum);
}

//往函数的指令流中写入1字节,返回其索引
static int writeByte(CompileUnit* cu, int byte) {
   //若在调试状态,额外在debug->lineNo中写入当前token行号
#if DEBUG
   IntBufferAdd(cu->curParser->vm,
	 &cu->fn->debug->lineNo, cu->curParser->preToken.lineNo);
#endif
   ByteBufferAdd(cu->curParser->vm,
	&cu->fn->instrStream, (uint8_t)byte);
   return cu->fn->instrStream.count - 1;
}

//写入操作码
static void writeOpCode(CompileUnit* cu, OpCode opCode) {
   writeByte(cu, opCode);
   //累计需要的运行时空间大小
   cu->stackSlotNum += opCodeSlotsUsed[opCode];
   if (cu->stackSlotNum > cu->fn->maxStackSlotUsedNum) {
      cu->fn->maxStackSlotUsedNum = cu->stackSlotNum;
   }
}

//写入1个字节的操作数
static int writeByteOperand(CompileUnit* cu, int operand) {
   return writeByte(cu, operand);
}

//写入2个字节的操作数 按大端字节序写入参数,低地址写高位,高地址写低位
inline static void writeShortOperand(CompileUnit* cu, int operand) {
   writeByte(cu, (operand >> 8) & 0xff); //先写高8位
   writeByte(cu, operand & 0xff);        //再写低8位
}

//写入操作数为1字节大小的指令
static int writeOpCodeByteOperand(CompileUnit* cu, OpCode opCode, int operand) {
   writeOpCode(cu, opCode);
   return writeByteOperand(cu, operand);
}

//写入操作数为2字节大小的指令
static void writeOpCodeShortOperand(CompileUnit* cu, OpCode opCode, int operand) {
   writeOpCode(cu, opCode);
   writeShortOperand(cu, operand);
}

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

//编译程序
static void compileProgram(CompileUnit* cu) {
   ;
}

//编译模块
ObjFn* compileModule(VM* vm, ObjModule* objModule, const char* moduleCode) {
   //各源码模块文件需要单独的parser
   Parser parser;
   parser.parent = vm->curParser;
   vm->curParser = &parser;

   if (objModule->name == NULL) {
      // 核心模块是core.script.inc
      initParser(vm, &parser, "core.script.inc", moduleCode, objModule);
   } else {
      initParser(vm, &parser, 
	    (const char*)objModule->name->value.start, moduleCode, objModule);
   }

   CompileUnit moduleCU;
   initCompileUnit(&parser, &moduleCU, NULL, false);

   //记录现在模块变量的数量,后面检查预定义模块变量时可减少遍历
   uint32_t moduleVarNumBefor = objModule->moduleVarValue.count;

   //初始的parser->curToken.type为TOKEN_UNKNOWN,下面使其指向第一个合法的token
   getNextToken(&parser);

   //此时compileProgram为桩函数,并不会读进token,因此是死循环.
   while (!matchToken(&parser, TOKEN_EOF)) {
      compileProgram(&moduleCU);
   }

   //后面还有很多要做的,临时放一句话在这提醒.
   //不过目前上面是死循环,本句无法执行。
   printf("There is something to do...\n"); exit(0);
}
