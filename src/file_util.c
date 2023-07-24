#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

char* concatenate_folder_file_path(char* folder_path, char* file_path) {
    char* path_new = malloc(128);
    size_t folder_path_len = strlen(folder_path);
    for (size_t i = 0; i < folder_path_len; i++) {
        path_new[i] = folder_path[i];
    }
    path_new[folder_path_len] = '/';
    size_t file_path_len = strlen(file_path);
    for (size_t i = 0; i < file_path_len; i++) {
        path_new[i + folder_path_len + 1] = file_path[i];
    }
    path_new[folder_path_len + 1 + file_path_len] = '\0';
    return path_new;
}
