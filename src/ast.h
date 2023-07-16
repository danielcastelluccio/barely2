#ifndef __ASTFILE
#define __ASTFILE

#include "dynamic_array.h"
#include "tokenizer.h"

Dynamic_Array_Def(char*, Array_String, array_string_)

struct Type;
typedef struct Type Type;

Dynamic_Array_Def(Type*, Array_Type, array_type_)

typedef enum {
    Type_USize,
    Type_U64,
    Type_U32,
    Type_U16,
    Type_U8,
} Internal_Type;

typedef struct {
    enum {
        Type_Single,
        Type_Multi,
    } kind;
    union {
        char* single;
        Array_String multi;
    } data;
} Basic_Type;

typedef struct {
    Type* child;
} Pointer_Type;

typedef struct {
    Array_Type arguments;
    Array_Type returns;
} Procedure_Type;

typedef struct {
    size_t size;
    bool has_size;
    Type* element_type;
} BArray_Type;

struct Type {
    enum {
        Type_None,
        Type_Basic,
        Type_Pointer,
        Type_Procedure,
        Type_Array,
        Type_Internal,
        Type_RegisterSize,
    } kind;
    union {
        Basic_Type basic;
        Pointer_Type pointer;
        Procedure_Type procedure;
        BArray_Type array;
        Internal_Type internal;
    } data;
};

struct Statement_Node;
typedef struct Statement_Node Statement_Node;

struct Expression_Node;
typedef struct Expression_Node Expression_Node;

typedef struct {
    char* name;
    Type type;
    Location location;
} Declaration;

Dynamic_Array_Def(Statement_Node*, Array_Statement_Node, array_statement_node_)
Dynamic_Array_Def(Expression_Node*, Array_Expression_Node, array_expression_node_)
Dynamic_Array_Def(Declaration, Array_Declaration, array_declaration_)

typedef struct {
    Array_Statement_Node statements;
} Block_Node;

typedef struct {
    size_t value;
    Type* type;
} Number_Node;

typedef struct {
    char* value;
} String_Node;

typedef struct {
    bool value;
} Boolean_Node;

struct If_Node;
typedef struct If_Node If_Node;

struct If_Node {
    Expression_Node* condition;
    Expression_Node* inside;
    If_Node* next;
    Location location;
};

typedef struct {
    Expression_Node* condition;
    Expression_Node* inside;
    Location location;
} While_Node;

typedef enum {
    Operator_Add,
    Operator_Subtract,
    Operator_Multiply,
    Operator_Divide,
    Operator_Modulus,
    Operator_Equal,
    Operator_NotEqual,
    Operator_Greater,
    Operator_GreaterEqual,
    Operator_Less,
    Operator_LessEqual,
} Operator;

typedef struct {
    enum {
        Invoke_Standard,
        Invoke_Operator,
    } kind;
    union {
        Expression_Node* procedure;
        struct {
            Operator operator;
            Type added_type;
        } operator;
    } data;
    Array_Expression_Node arguments;
    Location location;
} Invoke_Node;

typedef union {
    struct {
        char* name;
        Expression_Node* expression;
        // TODO: should we be adding extra info during processing like this?
        Type added_type;
    } single;
    Array_String multi;
    struct {
        Expression_Node* expression_outer;
        Expression_Node* expression_inner;
        Type added_type;
    } array;
} Complex_Name_Data;

typedef struct {
    enum {
        Complex_Single,
        Complex_Multi,
        Complex_Array,
    } kind;
    Complex_Name_Data data;
    Location location;
} Complex_Name;

typedef Complex_Name Retrieve_Node;

typedef struct {
    Array_Expression_Node expressions;
} Multi_Expression_Node;

typedef struct {
    Expression_Node* inner;
} Reference_Node;

typedef struct {
    Type type;
    Expression_Node* expression;
    Type added_type;
    Location location;
} Cast_Node;

typedef struct {
    Type type;
    Type added_type;
    Location location;
} SizeOf_Node;

struct Expression_Node {
    enum {
        Expression_Block,
        Expression_Number,
        Expression_String,
        Expression_Invoke,
        Expression_Retrieve,
        Expression_If,
        Expression_While,
        Expression_Multi,
        Expression_Reference,
        Expression_Boolean,
        Expression_Cast,
        Expression_SizeOf,
    } kind;
    union {
        Block_Node block;
        Number_Node number;
        String_Node string;
        Invoke_Node invoke;
        Retrieve_Node retrieve;
        If_Node if_;
        While_Node while_;
        Multi_Expression_Node multi;
        Reference_Node reference;
        Boolean_Node boolean;
        Cast_Node cast;
        SizeOf_Node size_of;
    } data;
};

typedef struct {
    Expression_Node* expression;
} Statement_Expression_Node;

typedef struct {
    Array_Declaration declarations;
    Expression_Node* expression;
} Statement_Declare_Node;

typedef Complex_Name Statement_Assign_Part;

Dynamic_Array_Def(Statement_Assign_Part, Array_Statement_Assign_Part, array_statement_assign_part_)

typedef struct {
    Array_Statement_Assign_Part parts;
    Expression_Node* expression;
} Statement_Assign_Node;

typedef struct {
    Expression_Node* expression;
    Location location;
} Statement_Return_Node;

struct Statement_Node {
    enum {
        Statement_Expression,
        Statement_Declare,
        Statement_Assign,
        Statement_Return,
    } kind;
    union {
        Statement_Expression_Node expression;
        Statement_Declare_Node declare;
        Statement_Assign_Node assign;
        Statement_Return_Node return_;
    } data;
};

typedef struct {
    Array_Declaration arguments;
    Array_Type returns;
    Expression_Node* body;
} Procedure_Literal_Node;

typedef struct {
    enum {
        Procedure_Literal,
    } kind;
    union {
        Procedure_Literal_Node literal;
    } data;
} Procedure_Node;

typedef struct {
    Array_Declaration items;
} Struct_Node;

typedef struct {
    enum {
        Type_Node_Struct,
    } kind;
    union {
        Struct_Node struct_;
    } data;
} Type_Node;

struct Definition_Node;
typedef struct Definition_Node Definition_Node;

Dynamic_Array_Def(Definition_Node, Array_Definition_Node, array_definition_node_)

typedef struct {
    Array_Definition_Node definitions;
} Module_Node;

typedef struct {
    Type type;
} Global_Node;

struct Definition_Node {
    char* name;
    enum {
        Definition_Procedure,
        Definition_Type,
        Definition_Module,
        Definition_Global,
    } kind;
    union {
        Procedure_Node procedure;
        Type_Node type;
        Module_Node module;
        Global_Node global;
    } data;
};

typedef struct {
    Array_Definition_Node definitions;
} File_Node;

File_Node parse(Tokens* tokens);

#endif
