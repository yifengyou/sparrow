#ifndef _VM_VM_H
#define _VM_VM_H
#include "common.h"
#include "class.h"
#include "obj_map.h"
#include "obj_thread.h"
#include "parser.h"

//为定义在opcode.inc中的操作码加上前缀"OPCODE_"
#define OPCODE_SLOTS(opcode, effect) OPCODE_##opcode,

//最多临时根对象数量
#define MAX_TEMP_ROOTS_NUM 8

typedef enum {
   #include "opcode.inc"
} OpCode;
#undef OPCODE_SLOTS

typedef enum vmResult {
   VM_RESULT_SUCCESS,
   VM_RESULT_ERROR
} VMResult;   //虚拟机执行结果
//如果执行无误,可以将字符码输出到文件缓存,避免下次重新编译

//灰色对象信息结构
typedef struct {
   //gc中的灰对象(也是保留对象)指针数组
   ObjHeader** grayObjects;
   uint32_t capacity;
   uint32_t count;
} Gray;

typedef struct {
   //堆生长因子
   int heapGrowthFactor;

   //初始堆大小,默认为10MB
   uint32_t initialHeapSize;

   //最小堆大小,默认为1MB
   uint32_t minHeapSize;

   //第一次触发gc的堆大小,默认为initialHeapSize
   uint32_t nextGC; 
} Configuration;

struct vm {
   Class* classOfClass;
   Class* objectClass;
   Class* stringClass;
   Class* mapClass;
   Class* rangeClass;
   Class* listClass;
   Class* nullClass;
   Class* boolClass;
   Class* numClass;
   Class* fnClass;
   Class* threadClass;

   uint32_t allocatedBytes;  //累计已分配的内存量
   ObjHeader* allObjects;  //所有已分配对象链表
   SymbolTable allMethodNames;    //(所有)类的方法名
   ObjMap* allModules;
   ObjThread* curThread;   //当前正在执行的线程
   Parser* curParser;  //当前词法分析器

   //临时的根对象集合(数组),存储临时需要被GC保留的对象,避免回收
   ObjHeader* tmpRoots[MAX_TEMP_ROOTS_NUM];
   uint32_t tmpRootNum;

   //用于存储存活(保留)对象
   Gray grays; 
   Configuration config;
};

void initVM(VM* vm);
VM* newVM(void);
void ensureStack(VM* vm, ObjThread* objThread, uint32_t neededSlots);
VMResult executeInstruction(VM* vm, register ObjThread* curThread);
void pushTmpRoot(VM* vm, ObjHeader* obj);
void popTmpRoot(VM* vm);
void freeVM(VM* vm);
#endif
