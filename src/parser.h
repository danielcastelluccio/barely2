#include "ast.h"
#include "tokenizer.h"

typedef struct {
    Tokens* tokens;
    size_t index;
    Array_Directive directives;
} Parser_State;

Expression_Node parse_expression(Parser_State* state);
File_Node parse(char* path, Tokens* tokens);
