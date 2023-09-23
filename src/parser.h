#include "ast.h"
#include "tokenizer.h"

typedef struct {
    Tokens* tokens;
    size_t index;
    Array_Ast_Directive directives;
} Parser_State;

Ast_File parse(char* path, Tokens* tokens);
