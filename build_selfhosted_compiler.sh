command="./build_with_core.sh"

if [ "$1" = "gdb" ]; then
    command="$command gdb"
    shift
fi

if [ "$1" = "valgrind" ]; then
    command="$command valgrind"
    shift
fi

command="$command ${@} compiler/main.barely"
eval "$command"
