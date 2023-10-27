#include <assert.h>
#include <stdio.h>

#include "tokenizer.h"
#include "parser.h"
#include "processor.h"
#include "file_util.h"

#include "output/fasm_linux_x86_64.h"
#include "output/qbe.h"

int main(int argc, char** argv) {
    Program program = program_new(4);

    char* backend = "none";

    int i = 1;
    while (i < argc) {
        char* arg = argv[i];

        if (arg[0] == '-') {
            if (strcmp(arg, "-backend") == 0) {
                backend = argv[i + 1];
                i += 2;
            } else {
                assert(false);
            }
        } else {
            char* path = argv[i];
            char* real_path = realpath(path, NULL);

            char* contents = read_file_to_string(real_path);
            if (contents == NULL) {
                printf("Invalid file %s\n", path);
                exit(1);
            }
            Tokens tokens = tokenize(path, contents);

            program_append(&program, parse(&tokens));
            i++;
        }
    }

    process(&program);

    if (strcmp(backend, "fasm") == 0) {
        output_fasm_linux_x86_64(&program, "output.fasm");
    } else if (strcmp(backend, "qbe") == 0) {
        output_qbe(&program, "output.qbe");
    } else {
        assert(false);
    }
}
