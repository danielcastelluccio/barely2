#ifndef PROCESSOR__
#define PROCESSOR__

#include "ast.h"

Dynamic_Array_Def(Ast_Type, Stack_Ast_Type, stack_type_)
Dynamic_Array_Def(size_t, Array_Size, array_size_)

typedef struct {
    Program* program;
    Ast_File* current_file;
    Array_String* package_names;
    Array_String* package_paths;
} Generic_State;

typedef struct {
    Generic_State generic;
    Stack_Ast_Type stack;
    Ast_Item* current_procedure;
    Array_Ast_Declaration current_declares;
    Array_Size scoped_declares;
    Array_Ast_Declaration current_arguments;
    Array_Ast_Type current_returns;
    Ast_Expression* current_body;
    bool in_reference;
    Ast_Type* wanted_type;
} Process_State;

typedef struct {
    Ast_File* file;
    enum {
        Unresolved,
        Resolved_Item,
    } kind;
    union {
        Ast_Item* item;
    } data;
} Resolved;

Resolved resolve(Generic_State* state, Ast_Identifier data);
bool is_type(Ast_Type* wanted, Ast_Type* given, Process_State* state);
bool is_internal_type(Ast_Type_Internal wanted, Ast_Type* given);
Ast_Type create_internal_type(Ast_Type_Internal type);
Ast_Type create_basic_single_type(char* name);
void process(Program* program, Array_String* package_names, Array_String* package_paths);

bool has_directive(Array_Ast_Directive* directives, Directive_Kind kind);
Ast_Directive* get_directive(Array_Ast_Directive* directives, Directive_Kind kind);

Ast_Type evaluate_type(Ast_Type* type);
Ast_Type evaluate_type_complete(Ast_Type* type, Generic_State* state);

#endif
