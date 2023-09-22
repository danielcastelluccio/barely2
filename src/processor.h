#ifndef PROCESSOR__
#define PROCESSOR__

#include "ast.h"

Dynamic_Array_Def(Type, Stack_Type, stack_type_)
Dynamic_Array_Def(size_t, Array_Size, array_size_)

typedef struct {
    Program* program;
    File_Node* current_file;
    Array_String* package_names;
    Array_String* package_paths;
} Generic_State;

typedef struct {
    Generic_State generic;
    Stack_Type stack;
    Item_Node* current_procedure;
    Array_Declaration current_declares;
    Array_Size scoped_declares;
    Array_Declaration current_arguments;
    Array_Type current_returns;
    Expression_Node* current_body;
    bool in_reference;
    Type* wanted_type;
} Process_State;

typedef struct {
    File_Node* file;
    enum {
        Unresolved,
        Resolved_Item,
        Resolved_Enum_Variant,
    } kind;
    union {
        Item_Node* item;
        struct {
            Enum_Type* enum_;
            char* variant;
        } enum_;
    } data;
} Resolved;

Resolved resolve(Generic_State* state, Identifier data);
bool is_type(Type* wanted, Type* given, Process_State* state);
bool is_internal_type(Internal_Type wanted, Type* given);
Type create_internal_type(Internal_Type type);
Type create_basic_single_type(char* name);
void process(Program* program, Array_String* package_names, Array_String* package_paths);

bool has_directive(Array_Directive* directives, Directive_Kind kind);
Directive_Node* get_directive(Array_Directive* directives, Directive_Kind kind);

Type evaluate_type(Type* type);
Type evaluate_type_complete(Type* type, Generic_State* state);

#endif
