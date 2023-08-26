#include "ast.h"
#include "tokenizer.h"

typedef struct {
    Tokens* tokens;
    Array_Directive directives;
} Parser_State;

Expression_Node parse_expression(Parser_State* state, size_t* index_in);
File_Node parse(char* path, Tokens* tokens);
