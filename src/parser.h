#include "ast.h"
#include "tokenizer.h"

Expression_Node parse_expression(Tokens* tokens, size_t* index_in);
File_Node parse(char* path, Tokens* tokens);
