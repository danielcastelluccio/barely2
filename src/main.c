#include <stdio.h>

#include "tokenizer.h"
#include "parser.h"
#include "processor.h"
#include "file_util.h"
#include "ast_print.h"

#include "output/fasm_linux_x86_64.h"

int main(int argc, char** argv) {
    // Make this an array to support multiple file compiles
    File_Node ast;
    for (int i = 1; i < argc; i++) {
        char* file_name = argv[i];
        char* contents = read_file_to_string(file_name);
        Tokens tokens = tokenize(file_name, contents);

        ast = parse(&tokens);
    }

    process(&ast);
    output_fasm_linux_x86_64(ast, "output.asm");
}
