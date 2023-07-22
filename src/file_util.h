#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

char* read_file_to_string(char* path) {
    FILE* file = fopen(path, "r");
    if (file == NULL) return 0;

    fseek(file, 0, SEEK_END);
    long int file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = malloc(file_size + 1);
    fread(buffer,1, file_size, file);

    return buffer;
}
