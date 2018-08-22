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

typedef enum {
   BP_NONE,      //无绑定能力

   //从上往下,优先级越来越高
   BP_LOWEST,    //最低绑定能力
   BP_ASSIGN,    // =
   BP_CONDITION,   // ?:
   BP_LOGIC_OR,    // ||
   BP_LOGIC_AND,   // &&
   BP_EQUAL,      // == !=
   BP_IS,        // is
   BP_CMP,       // < > <= >=
   BP_BIT_OR,    // |
   BP_BIT_AND,   // &
   BP_BIT_SHIFT, // << >>
   BP_RANGE,       // .. 
   BP_TERM,	  // + -
   BP_FACTOR,	  // * / %
   BP_UNARY,    // - ! ~
   BP_CALL,     // . () []
   BP_HIGHEST 
} BindPower;   //定义了操作符的绑定权值,即优先级

//指示符函数指针
typedef void (*DenotationFn)(CompileUnit* cu, bool canAssign);

//签名函数指针
typedef void (*methodSignatureFn)(CompileUnit* cu, Signature* signature);

typedef struct {
   const char* id;	      //符号

   //左绑定权值,不关注左边操作数的符号此值为0
   BindPower lbp;

   //字面量,变量,前缀运算符等不关注左操作数的Token调用的方法
   DenotationFn nud;

   //中缀运算符等关注左操作数的Token调用的方法
   DenotationFn led;

   //表示本符号在类中被视为一个方法,
   //为其生成一个方法签名.
   methodSignatureFn methodSign;
} SymbolBindRule;   //符号绑定规则

static uint32_t addConstant(CompileUnit* cu, Value constant);

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

//把Signature转换为字符串,返回字符串长度
static uint32_t sign2String(Signature* sign, char* buf) {
   uint32_t pos = 0;

   //复制方法名xxx
   memcpy(buf + pos, sign->name, sign->length);
   pos += sign->length;

   //下面单独处理方法名之后的部分
   switch (sign->type) {
      //SIGN_GETTER形式:xxx,无参数,上面memcpy已完成
      case SIGN_GETTER:
	 break;

      //SIGN_SETTER形式: xxx=(_),之前已完成xxx
      case SIGN_SETTER: 
	 buf[pos++] = '=';
	 //下面添加=右边的赋值,只支持一个赋值
	 buf[pos++] = '(';
	 buf[pos++] = '_';
	 buf[pos++] = ')';
	 break;

      //SIGN_METHOD和SIGN_CONSTRUCT形式:xxx(_,...)
      case SIGN_CONSTRUCT:
      case SIGN_METHOD: {
	 buf[pos++] = '(';
	 uint32_t idx = 0;
	 while (idx < sign->argNum) {
	    buf[pos++] = '_';  
	    buf[pos++] = ',';  
	    idx++;
	 }

	 if (idx == 0) { //说明没有参数
	    buf[pos++] = ')';
	 } else { //用rightBracket覆盖最后的','
	    buf[pos - 1] = ')';
	 }
	 break;
      }

      //SIGN_SUBSCRIPT形式:xxx[_,...]
      case SIGN_SUBSCRIPT: {
	 buf[pos++] = '[';
	 uint32_t idx = 0;
	 while (idx < sign->argNum) {
	    buf[pos++] = '_';  
	    buf[pos++] = ',';  
	    idx++;
	 }
	 if (idx == 0) { //说明没有参数
	    buf[pos++] = ']';
	 } else { //用rightBracket覆盖最后的','
	    buf[pos - 1] = ']';
	 }
	 break;
      }

      //SIGN_SUBSCRIPT_SETTER形式:xxx[_,...]=(_)
      case SIGN_SUBSCRIPT_SETTER: {
	 buf[pos++] = '[';
	 uint32_t idx = 0;
	 //argNum包括了等号右边的1个赋值参数,
	 //这里是在处理等号左边subscript中的参数列表,因此减1.
	 //后面专门添加该参数
	 while (idx < sign->argNum - 1) {
	    buf[pos++] = '_';  
	    buf[pos++] = ',';  
	    idx++;
	 }
	 if (idx == 0) { //说明没有参数
	    buf[pos++] = ']';
	 } else { //用rightBracket覆盖最后的','
	    buf[pos - 1] = ']';
	 }

	 //下面为等号右边的参数构造签名部分
	 buf[pos++] = '=';  
	 buf[pos++] = '(';
	 buf[pos++] = '_';
	 buf[pos++] = ')';
      }
   }

   buf[pos] = '\0';
   return pos;   //返回签名串的长度
}

//添加局部变量到cu
static uint32_t addLocalVar(CompileUnit* cu, const char* name, uint32_t length) {
   LocalVar* var = &(cu->localVars[cu->localVarNum]);
   var->name = name;
   var->length = length;
   var->scopeDepth = cu->scopeDepth;
   var->isUpvalue = false;
   return cu->localVarNum++;
}

//声明局部变量
static int declareLocalVar(CompileUnit* cu, const char* name, uint32_t length) {
   if (cu->localVarNum >= MAX_LOCAL_VAR_NUM) {
      COMPILE_ERROR(cu->curParser, "the max length of local variable of one scope is %d", MAX_LOCAL_VAR_NUM);  
   }

   //判断当前作用域中该变量是否已存在
   int idx = (int)cu->localVarNum - 1;
   while (idx >= 0) {
      LocalVar* var = &cu->localVars[idx];

      //只在当前作用域中查找同名变量,
      //如果到了父作用域就退出,减少没必要的遍历
      if (var->scopeDepth < cu->scopeDepth) {
	 break;
      }

      if (var->length == length && memcmp(var->name, name, length) == 0) {
	 char id[MAX_ID_LEN] = {'\0'};
	 memcpy(id, name, length);
	 COMPILE_ERROR(cu->curParser, "identifier \"%s\" redefinition!", id);
      }
      idx--;
   }

   //检查过后声明该局部变量
   return addLocalVar(cu, name, length);
}

//根据作用域声明变量
static int declareVariable(CompileUnit* cu, const char* name, uint32_t length) {
   //若当前是模块作用域就声明为模块变量
   if (cu->scopeDepth == -1) {
      int index = defineModuleVar(cu->curParser->vm,
	    cu->curParser->curModule, name, length, VT_TO_VALUE(VT_NULL));
      if (index == -1) {   //重复定义则报错
	 char id[MAX_ID_LEN] = {'\0'};
	 memcpy(id, name, length);
	 COMPILE_ERROR(cu->curParser, "identifier \"%s\" redefinition!", id);
      }
      return index;
   }

   //否则是局部作用域,声明局部变量
   return declareLocalVar(cu, name, length);
}

//为单运算符方法创建签名
static void unaryMethodSignature(CompileUnit* cu UNUSED, Signature* sign UNUSED) {
   //名称部分在调用前已经完成,只修改类型
   sign->type = SIGN_GETTER;
}

//为中缀运算符创建签名
static void infixMethodSignature(CompileUnit* cu, Signature* sign) {
   //在类中的运算符都是方法,类型为SIGN_METHOD
   sign->type = SIGN_METHOD;

   // 中缀运算符只有一个参数,故初始为1
   sign->argNum = 1;
   consumeCurToken(cu->curParser, TOKEN_LEFT_PAREN, "expect '(' after infix operator!");
   consumeCurToken(cu->curParser, TOKEN_ID, "expect variable name!");
   declareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
   consumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after parameter!");
}

//为既做单运算符又做中缀运算符的符号方法创建签名
static void mixMethodSignature(CompileUnit* cu, Signature* sign) {
   //假设是单运算符方法,因此默认为getter
   sign->type = SIGN_GETTER;

   //若后面有'(',说明其为中缀运算符,那就置其类型为SIGN_METHOD 
   if (matchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
      sign->type = SIGN_METHOD;   
      sign->argNum = 1;
      consumeCurToken(cu->curParser, TOKEN_ID, "expect variable name!"); 
      declareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
      consumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after parameter!");
   }
}

//添加常量并返回其索引
static uint32_t addConstant(CompileUnit* cu, Value constant) {
   ValueBufferAdd(cu->curParser->vm, &cu->fn->constants, constant);
   return cu->fn->constants.count - 1;
}

//生成加载常量的指令
static void emitLoadConstant(CompileUnit* cu, Value value) {
   int index = addConstant(cu, value);
   writeOpCodeShortOperand(cu, OPCODE_LOAD_CONSTANT, index);
}

//数字和字符串.nud() 编译字面量
static void literal(CompileUnit* cu, bool canAssign UNUSED) {
   //literal是常量(数字和字符串)的nud方法,用来返回字面值.
   emitLoadConstant(cu, cu->curParser->preToken.value);
}

//不关注左操作数的符号称为前缀符号
//用于如字面量,变量名,前缀符号等非运算符
#define PREFIX_SYMBOL(nud) {NULL, BP_NONE, nud, NULL, NULL}

//前缀运算符,如'!'
#define PREFIX_OPERATOR(id) {id, BP_NONE, unaryOperator, NULL, unaryMethodSignature}

//关注左操作数的符号称为中缀符号
//数组'[',函数'(',实例与方法之间的'.'等
#define INFIX_SYMBOL(lbp, led) {NULL, lbp, NULL, led, NULL}

//中棳运算符
#define INFIX_OPERATOR(id, lbp) {id, lbp, NULL, infixOperator, infixMethodSignature}

//既可做前缀又可做中缀的运算符,如'-'
#define MIX_OPERATOR(id) {id, BP_TERM, unaryOperator, infixOperator, mixMethodSignature}

//占位用的
#define UNUSED_RULE {NULL, BP_NONE, NULL, NULL, NULL}

SymbolBindRule Rules[] = {
   /* TOKEN_INVALID*/		    UNUSED_RULE,
   /* TOKEN_NUM	*/	   	    PREFIX_SYMBOL(literal),
   /* TOKEN_STRING */ 	   	    PREFIX_SYMBOL(literal),
};

//语法分析的核心
static void expression(CompileUnit* cu, BindPower rbp) {
   //以中缀运算符表达式"aSwTe"为例,
   //大写字符表示运算符,小写字符表示操作数

   //进入expression时,curToken是操作数w, preToken是运算符S
   DenotationFn nud = Rules[cu->curParser->curToken.type].nud;

   //表达式开头的要么是操作数要么是前缀运算符,必然有nud方法
   ASSERT(nud != NULL, "nud is NULL!");

   getNextToken(cu->curParser);  //执行后curToken为运算符T

   bool canAssign = rbp < BP_ASSIGN;
   nud(cu, canAssign);   //计算操作数w的值

   while (rbp < Rules[cu->curParser->curToken.type].lbp) {
      DenotationFn led = Rules[cu->curParser->curToken.type].led;
      getNextToken(cu->curParser);  //执行后curToken为操作数e
      led(cu, canAssign);  //计算运算符T.led方法
   }
}

//通过签名编译方法调用 包括callX和superX指令
static void emitCallBySignature(CompileUnit* cu, Signature* sign, OpCode opcode) {
   char signBuffer[MAX_SIGN_LEN];
   uint32_t length = sign2String(sign, signBuffer);

   //确保签名录入到vm->allMethodNames中
   int symbolIndex = ensureSymbolExist(cu->curParser->vm, \
	 &cu->curParser->vm->allMethodNames, signBuffer, length);
   writeOpCodeShortOperand(cu, opcode + sign->argNum, symbolIndex);
  
   //此时在常量表中预创建一个空slot占位,将来绑定方法时再装入基类
   if (opcode == OPCODE_SUPER0) {
      writeShortOperand(cu,  addConstant(cu, VT_TO_VALUE(VT_NULL)));
   }
}

//生成方法调用的指令,仅限callX指令
static void emitCall(CompileUnit* cu, int numArgs, const char* name, int length) {
   int symbolIndex = ensureSymbolExist(cu->curParser->vm,
	 &cu->curParser->vm->allMethodNames, name, length);
   writeOpCodeShortOperand(cu, OPCODE_CALL0 + numArgs, symbolIndex);
}

//中缀运算符.led方法
static void infixOperator(CompileUnit* cu, bool canAssign UNUSED) {
   SymbolBindRule* rule = &Rules[cu->curParser->preToken.type];

   //中缀运算符对左右操作数的绑定权值一样
   BindPower rbp = rule->lbp;
   expression(cu, rbp);  //解析右操作数
   
   //生成1个参数的签名
   Signature sign = {SIGN_METHOD, rule->id, strlen(rule->id), 1};
   emitCallBySignature(cu, &sign, OPCODE_CALL0);
}

//前缀运算符.nud方法, 如'-','!'等
static void unaryOperator(CompileUnit* cu, bool canAssign UNUSED) {
   SymbolBindRule* rule = &Rules[cu->curParser->preToken.type];

   //BP_UNARY做为rbp去调用expression解析右操作数
   expression(cu, BP_UNARY);
   
   //生成调用前缀运算符的指令
   //0个参数,前缀运算符都是1个字符,长度是1
   emitCall(cu, 0, rule->id, 1);
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
