#include <stdio.h>

#include "tokenizer.h"
#include "parser.h"
#include "file_util.h"
#include "ast_print.h"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        char* file_name = argv[i];
        char* contents = read_file_to_string(file_name);
        Tokens tokens = tokenize(contents);
        File_Node ast = parse(&tokens);
        print_file_node(&ast);
    }
}
