#include "core.h"
#include <string.h>
#include <sys/stat.h>
#include "utils.h"
#include "vm.h"
#include "obj_thread.h"

char* rootDir = NULL;   //根目录

#define CORE_MODULE VT_TO_VALUE(VT_NULL)

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

//执行模块,目前为空,桩函数
VMResult executeModule(VM* vm, Value moduleName, const char* moduleCode) {
   return VM_RESULT_ERROR;
}

//编译核心模块
void buildCore(VM* vm) {
   //创建核心模块,录入到vm->allModules
   ObjModule* coreModule = newObjModule(vm, NULL); //NULL为核心模块.name
   mapSet(vm, vm->allModules, CORE_MODULE, OBJ_TO_VALUE(coreModule));
}
