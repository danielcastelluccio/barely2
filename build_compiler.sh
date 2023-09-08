gcc -g -Wall -Wextra src/tokenizer.c src/string_util.c src/parser.c src/ast.c src/main.c src/processor.c src/output/fasm_linux_x86_64.c src/file_util.c -o barely $@
