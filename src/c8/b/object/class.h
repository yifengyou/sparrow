#ifndef _OBJECT_CLASS_H
#define _OBJECT_CLASS_H
#include "common.h"
#include "utils.h"
#include "header_obj.h"
#include "obj_string.h"
#include "obj_fn.h"

typedef enum {
   MT_NONE,     //空方法类型,并不等同于undefined
   MT_PRIMITIVE,    //在vm中用c实现的原生方法
   MT_SCRIPT,	//脚本中定义的方法
   MT_FN_CALL,  //有关函数对象的调用方法,用来实现函数重载
} MethodType;   //方法类型

#define VT_TO_VALUE(vt) \
   ((Value){vt, {0}})

#define BOOL_TO_VALUE(boolean) (boolean ? VT_TO_VALUE(VT_TRUE) : VT_TO_VALUE(VT_FALSE))
#define VALUE_TO_BOOL(value) ((value).type == VT_TRUE ? true : false)

#define NUM_TO_VALUE(num) ((Value){VT_NUM, {num}})
#define VALUE_TO_NUM(value) value.num

#define OBJ_TO_VALUE(objPtr) ({ \
   Value value; \
   value.type = VT_OBJ; \
   value.objHeader = (ObjHeader*)(objPtr); \
   value; \
})

#define VALUE_TO_OBJ(value) (value.objHeader)
#define VALUE_TO_OBJSTR(value) ((ObjString*)VALUE_TO_OBJ(value))
#define VALUE_TO_OBJFN(value) ((ObjFn*)VALUE_TO_OBJ(value))
#define VALUE_TO_OBJRANGE(value) ((ObjRange*)VALUE_TO_OBJ(value))
#define VALUE_TO_OBJINSTANCE(value) ((ObjInstance*)VALUE_TO_OBJ(value))
#define VALUE_TO_OBJLIST(value) ((ObjList*)VALUE_TO_OBJ(value))
#define VALUE_TO_OBJMAP(value) ((ObjMap*)VALUE_TO_OBJ(value))
#define VALUE_TO_OBJCLOSURE(value) ((ObjClosure*)VALUE_TO_OBJ(value))
#define VALUE_TO_OBJTHREAD(value) ((ObjThread*)VALUE_TO_OBJ(value))
#define VALUE_TO_OBJMODULE(value) ((ObjModule*)VALUE_TO_OBJ(value))
#define VALUE_TO_CLASS(value) ((Class*)VALUE_TO_OBJ(value))

#define VALUE_IS_UNDEFINED(value) ((value).type == VT_UNDEFINED)
#define VALUE_IS_NULL(value) ((value).type == VT_NULL)
#define VALUE_IS_TRUE(value) ((value).type == VT_TRUE)
#define VALUE_IS_FALSE(value) ((value).type == VT_FALSE)
#define VALUE_IS_NUM(value) ((value).type == VT_NUM)
#define VALUE_IS_OBJ(value) ((value).type == VT_OBJ)
#define VALUE_IS_CERTAIN_OBJ(value, objType) (VALUE_IS_OBJ(value) && VALUE_TO_OBJ(value)->type == objType)
#define VALUE_IS_OBJSTR(value) (VALUE_IS_CERTAIN_OBJ(value, OT_STRING))
#define VALUE_IS_OBJINSTANCE(value) (VALUE_IS_CERTAIN_OBJ(value, OT_INSTANCE))
#define VALUE_IS_OBJCLOSURE(value) (VALUE_IS_CERTAIN_OBJ(value, OT_CLOSURE))
#define VALUE_IS_OBJRANGE(value) (VALUE_IS_CERTAIN_OBJ(value, OT_RANGE))
#define VALUE_IS_CLASS(value) (VALUE_IS_CERTAIN_OBJ(value, OT_CLASS))
#define VALUE_IS_0(value) (VALUE_IS_NUM(value) && (value).num == 0)

//原生方法指针
typedef bool (*Primitive)(VM* vm, Value* args);

typedef struct {
   MethodType type;  //union中的值由type的值决定
   union {      
      //指向脚本方法所关联的c实现
      Primitive primFn;

      //指向脚本代码编译后的ObjClosure或ObjFn
      ObjClosure* obj;
   };
} Method;

DECLARE_BUFFER_TYPE(Method)

//类是对象的模板
struct class {
   ObjHeader objHeader;
   struct class* superClass; //父类
   uint32_t fieldNum;	   //本类的字段数,包括基类的字段数
   MethodBuffer methods;   //本类的方法
   ObjString* name;   //类名
};  //对象类

typedef union {
   uint64_t bits64;
   uint32_t bits32[2];
   double num;
} Bits64;

#define CAPACITY_GROW_FACTOR 4 
#define MIN_CAPACITY 64
bool valueIsEqual(Value a, Value b);
Class* newRawClass(VM* vm, const char* name, uint32_t fieldNum); 
inline Class* getClassOfObj(VM* vm, Value object);
Class* newClass(VM* vm, ObjString* className, uint32_t fieldNum, Class* superClass);
#endif
