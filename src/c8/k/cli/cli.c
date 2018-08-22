#include "cli.h"
#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "vm.h"
#include "core.h"

//执行脚本文件
static void runFile(const char* path) {
   const char* lastSlash = strrchr(path, '/');
   if (lastSlash != NULL) {
      char* root = (char*)malloc(lastSlash - path + 2);
      memcpy(root, path, lastSlash - path + 1);
      root[lastSlash - path + 1] = '\0';
      rootDir = root;
   }

   VM* vm = newVM();
   const char* sourceCode = readFile(path);
   executeModule(vm, OBJ_TO_VALUE(newObjString(vm, path, strlen(path))), sourceCode);
}

int main(int argc, const char** argv) {
   if (argc == 1) {
      ;
   } else {
      runFile(argv[1]);
   }
   return 0;
}
