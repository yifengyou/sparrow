#include "obj_string.h"
#include <string.h>
#include "vm.h"
#include "utils.h"
#include "common.h"
#include <stdlib.h>

//fnv-1a算法
uint32_t hashString(char* str, uint32_t length) {
   uint32_t hashCode = 2166136261, idx = 0;
   while (idx < length) {
      hashCode ^= str[idx];
      hashCode *= 16777619;
      idx++;
   }
   return hashCode;
}

//为string计算哈希码并将值存储到string->hash
void hashObjString(ObjString* objString) {
   objString->hashCode = 
      hashString(objString->value.start, objString->value.length);
}

//以str字符串创建ObjString对象,允许空串""
ObjString* newObjString(VM* vm, const char* str, uint32_t length) {
   //length为0时str必为NULL length不为0时str不为NULL
   ASSERT(length == 0 || str != NULL, "str length don`t match str!");

   //+1是为了结尾的'\0'
   ObjString* objString = ALLOCATE_EXTRA(vm, ObjString, length + 1);

   if (objString != NULL) {
      initObjHeader(vm, &objString->objHeader, OT_STRING, vm->stringClass);
      objString->value.length = length;

      //支持空字符串: str为null,length为0
      //如果非空则复制其内容
      if (length > 0) {
	 memcpy(objString->value.start, str, length);
      }
      objString->value.start[length] = '\0';
      hashObjString(objString);
   } else {
      MEM_ERROR("Allocating ObjString failed!");
   }
   return objString;
}
