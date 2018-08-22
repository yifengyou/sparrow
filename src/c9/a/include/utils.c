#include "utils.h"
#include "vm.h"
#include "parser.h"
#include <stdlib.h>
#include <stdarg.h>
#include "gc.h"

//内存管理三种功能:
//   1 申请内存
//   2 修改空间大小
//   3 释放内存
void* memManager(VM* vm, void* ptr, uint32_t oldSize, uint32_t newSize) {
   //累计系统分配的总内存
   vm->allocatedBytes += newSize - oldSize;

    //避免realloc(NULL, 0)定义的新地址,此地址不能被释放
   if (newSize == 0) {
      free(ptr);
      return NULL;
   }

   //在分配内存时若达到了GC触发的阀值则启动垃圾回收
   if (newSize > 0 && vm->allocatedBytes > vm->config.nextGC) {
      startGC(vm);
   }

   return realloc(ptr, newSize); 
}

// 找出大于等于v最近的2次幂
uint32_t ceilToPowerOf2(uint32_t v) {
   v += (v == 0);  //修复当v等于0时结果为0的边界情况
   v--;
   v |= v >> 1;
   v |= v >> 2;
   v |= v >> 4;
   v |= v >> 8;
   v |= v >> 16;
   v++;
   return v;
}

DEFINE_BUFFER_METHOD(String)
DEFINE_BUFFER_METHOD(Int)
DEFINE_BUFFER_METHOD(Char)
DEFINE_BUFFER_METHOD(Byte)

void symbolTableClear(VM* vm, SymbolTable* buffer) {
   uint32_t idx = 0;
   while (idx < buffer->count) {
      memManager(vm, buffer->datas[idx++].str, 0, 0); 
   }
   StringBufferClear(vm, buffer);
}

//通用报错函数
void errorReport(void* parser, 
      ErrorType errorType, const char* fmt, ...) {
   char buffer[DEFAULT_BUfFER_SIZE] = {'\0'};
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(buffer, DEFAULT_BUfFER_SIZE, fmt, ap);
   va_end(ap);

   switch (errorType) {
      case ERROR_IO:
      case ERROR_MEM:
	 fprintf(stderr, "%s:%d In function %s():%s\n",
	       __FILE__, __LINE__, __func__, buffer);
	 break;
      case ERROR_LEX:
      case ERROR_COMPILE:
	 ASSERT(parser != NULL, "parser is null!");
	 fprintf(stderr, "%s:%d \"%s\"\n", ((Parser*)parser)->file,
	       ((Parser*)parser)->preToken.lineNo, buffer);
	 break;
      case ERROR_RUNTIME:
	    fprintf(stderr, "%s\n", buffer);
	 break;
      default:
	 NOT_REACHED();
   }
   exit(1);
}
