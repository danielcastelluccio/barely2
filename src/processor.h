#ifndef PROCESSOR__
#define PROCESSOR__

#include "ast.h"

typedef struct {
    Program* program;
    File_Node* current_file;
    Array_String* package_names;
    Array_String* package_paths;
} Generic_State;

Dynamic_Array_Def(Type, Stack_Type, stack_type_)
Dynamic_Array_Def(size_t, Array_Size, array_size_)

typedef struct {
    File_Node* file;
    Module_Node* parent_module;
    Item_Node* item;
} Resolved_Item;

Resolved_Item resolve_item(Generic_State* state, Item_Identifier data);
bool is_type(Type* wanted, Type* given);
bool is_internal_type(Internal_Type wanted, Type* given);
Type create_internal_type(Internal_Type type);
Type create_basic_single_type(char* name);
void process(Program* program, Array_String* package_names, Array_String* package_paths);
Item_Identifier basic_type_to_item_identifier(Basic_Type type);

#endif
