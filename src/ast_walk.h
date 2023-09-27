#include "ast.h"

typedef struct {
    void (*expression_func)(Ast_Expression* expression, void* internal_state);
    void (*statement_func)(Ast_Statement* statement, void* internal_state);
    void (*type_func)(Ast_Type* statement, void* internal_state);
    void* internal_state;
} Ast_Walk_State;

void walk_item(Ast_Item* item, Ast_Walk_State* state);
void walk_expression(Ast_Expression* expression, Ast_Walk_State* state);
void walk_statement(Ast_Statement* statement, Ast_Walk_State* state);
void walk_type(Ast_Type* type, Ast_Walk_State* state);
void walk_macro_syntax_data(Ast_Macro_Syntax_Data* syntax_data, Ast_Walk_State* state);
