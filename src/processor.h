#ifndef PROCESSOR__
#define PROCESSOR__

#include "ast.h"

typedef struct {
    Program* program;
    File_Node* current_file;
} Generic_State;

Dynamic_Array_Def(Type, Stack_Type, stack_type_)
Dynamic_Array_Def(size_t, Array_Size, array_size_)

typedef struct {
    File_Node* file;
    Definition_Node* definition;
} Resolved_Definition;

Resolved_Definition resolve_definition(Generic_State* state, Definition_Identifier data);
bool is_type(Type* wanted, Type* given);
Type create_internal_type(Internal_Type type);
Type create_basic_single_type(char* name);
void process(Program* program);
Definition_Identifier retrieve_assign_to_definition_identifier(Retrieve_Assign_Node* retrieve_assign);
Definition_Identifier basic_type_to_definition_identifier(Basic_Type type);

#endif
