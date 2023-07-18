#ifndef PROCESSOR__
#define PROCESSOR__

#include "ast.h"

typedef struct {
    Program* program;
} Generic_State;

Dynamic_Array_Def(Type, Stack_Type, stack_type_)
Dynamic_Array_Def(size_t, Array_Size, array_size_)

Definition_Node* resolve_definition(Program* program, Complex_Name* data);
bool is_type(Type* wanted, Type* given);
Type create_internal_type(Internal_Type type);
Type create_basic_single_type(char* name);
void process(Program* program);

#endif
