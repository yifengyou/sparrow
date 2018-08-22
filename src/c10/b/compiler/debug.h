#ifdef DEBUG
   #ifndef _COMPILER_DEBUG_H
   #define _COMPILER_DEBUG_H
   #include "utils.h"
   #include "obj_fn.h"
   #include "obj_thread.h"

   void bindDebugFnName(VM* vm, FnDebug* fnDebug,
	 const char* name, uint32_t length);
   void dumpValue(Value value);
   void dumpInstructions(VM* vm, ObjFn* fn);
   void dumpStack(ObjThread* thread);
   #endif
#endif
