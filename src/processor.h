#ifndef PROCESSOR__
#define PROCESSOR__

#include "ast.h"

typedef struct {
    File_Node* file_node;
} Generic_State;

Dynamic_Array_Def(Type, Stack_Type, stack_type_)

Definition_Node* resolve_definition(File_Node* file_node, Complex_Name* data);
bool is_type(Type* wanted, Type* given);
Type create_basic_single_type(char* name);
void process(File_Node* file_node);

#endif
