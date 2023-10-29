if [ "$1" = "gdb" ]; then
    command="gdb --args"
    shift
fi

if [ "$1" = "valgrind" ]; then
    command="DEBUGINFOD_URLS="https://debuginfod.archlinux.org" valgrind"
    shift
fi

command="$command ${@} core/write.barely core/file.barely core/print.barely core/allocate.barely core/brk_allocator.barely core/linked_list.barely core/dynamic_array.barely core/assert.barely core/string.barely core/syscall.barely core/read.barely core/memory.barely core/string_parse.barely core/buffer.barely"
command+=" core/barely/lexer.barely core/barely/parser.barely core/barely/ast.barely core/elf64.barely core/barely/backend/elf_linux_x64.barely core/x64.barely"
eval "$command"
