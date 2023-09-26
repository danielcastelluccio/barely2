#include <stdlib.h>

#include "../processor.h"

typedef struct {
    size_t size;
    size_t location;
} Location_Size_Data;

size_t get_size(Ast_Type* type_in, Generic_State* state);
Location_Size_Data get_parent_item_location_size(Ast_Type* parent_type, char* item_name, Generic_State* state);
bool has_argument(char* name, Generic_State* state);
Location_Size_Data get_local_variable_location_size(char* name, Generic_State* state);
bool has_local_variable(char* name, Generic_State* state);
Location_Size_Data get_argument_location_size(char* name, Generic_State* state);
size_t get_locals_size(Ast_Item_Procedure* procedure, Generic_State* state);
size_t get_arguments_size(Generic_State* state);
size_t get_returns_size(Generic_State* state);
