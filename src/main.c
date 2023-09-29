#include <stdio.h>

#include "tokenizer.h"
#include "parser.h"
#include "processor.h"
#include "file_util.h"

#include "output/fasm_linux_x86_64.h"

int main(int argc, char** argv) {
    Program program = program_new(4);

    int i = 1;
    while (i < argc) {
        char* path = argv[i];
        char* real_path = realpath(path, NULL);

        char* contents = read_file_to_string(real_path);
        if (contents == NULL) {
            printf("Invalid file %s\n", path);
            exit(1);
        }
        Tokens tokens = tokenize(path, contents);

        program_append(&program, parse(real_path, &tokens));
        i++;
    }

    process(&program);
    output_fasm_linux_x86_64(&program, "output");
}
