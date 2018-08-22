#ifndef _OBJECT_HEADER_H
#define _OBJECT_HEADER_H
#include "utils.h"
typedef enum {
   OT_CLASS,   //此项是class类型,以下都是object类型
   OT_LIST,
   OT_MAP,
   OT_MODULE,
   OT_RANGE,
   OT_STRING,
   OT_UPVALUE,
   OT_FUNCTION,
   OT_CLOSURE,
   OT_INSTANCE,
   OT_THREAD
} ObjType;  //对象类型

typedef struct objHeader {
   ObjType type;
   bool isDark;	   //对象是否可达
   Class* class;   //对象所属的类
   struct objHeader* next;   //用于链接所有已分配对象
} ObjHeader;	  //对象头,用于记录元信息和垃圾回收

typedef enum {
   VT_UNDEFINED,
   VT_NULL,
   VT_FALSE,
   VT_TRUE,
   VT_NUM,
   VT_OBJ   //值为对象,指向对象头
} ValueType;     //value类型

typedef struct {
   ValueType type;
   union {
      double num;
      ObjHeader* objHeader;
   };
} Value;   //通用的值结构

DECLARE_BUFFER_TYPE(Value)

void initObjHeader(VM* vm, ObjHeader* objHeader, ObjType objType, Class* class);
#endif
