#include <stdio.h>

#include "string_util.h"
#include "tokenizer.h"
#include "parser.h"
#include "processor.h"
#include "file_util.h"
#include "ast_print.h"

#include "output/fasm_linux_x86_64.h"

int main(int argc, char** argv) {
    Program program = program_new(4);

    Array_String package_names = array_string_new(1);
    Array_String package_paths = array_string_new(1);
    char* current_package_path = NULL;
    int i = 1;
    while (i < argc) {
        char* arg = argv[i];
        if (arg[0] == '-') {
            if (strcmp(arg, "-package") == 0) {
                char* name_path = argv[i + 1];
                size_t colon_index = string_index(name_path, ':');
                char* name = string_substring(name_path, 0, colon_index);
                char* path = string_substring(name_path, colon_index + 1, strlen(name_path));

                current_package_path = realpath(path, NULL);

                array_string_append(&package_names, name);
                array_string_append(&package_paths, current_package_path);

                i += 2;
            }
        } else {
            char* path = arg;
            if (current_package_path != NULL) {
                path = concatenate_folder_file_path(current_package_path, path);
            }
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
    }

    process(&program, &package_names, &package_paths);
    output_fasm_linux_x86_64(&program, "output.asm", &package_names, &package_paths);
}
