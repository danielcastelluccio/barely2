#ifndef __ASTFILE
#define __ASTFILE

#include "dynamic_array.h"
#include "tokenizer.h"

Dynamic_Array_Def(char*, Array_String, array_string_)

struct Type;
typedef struct Type Type;

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

struct Type {
    enum {
        Type_Basic,
    } kind;
    union {
        Basic_Type basic;
    } data;
};

struct Statement_Node;
typedef struct Statement_Node Statement_Node;

struct Expression_Node;
typedef struct Expression_Node Expression_Node;

typedef struct {
    char* name;
    Type type;
} Declaration;

Dynamic_Array_Def(Statement_Node*, Array_Statement_Node, array_statement_node_)
Dynamic_Array_Def(Expression_Node*, Array_Expression_Node, array_expression_node_)
Dynamic_Array_Def(Declaration, Array_Declaration, array_declaration_)

typedef struct {
    Array_Statement_Node statements;
} Block_Node;

typedef struct {
    int value;
} Number_Node;

typedef struct {
    char* value;
} String_Node;

struct If_Node;
typedef struct If_Node If_Node;

struct If_Node {
    Expression_Node* condition;
    Expression_Node* inside;
    If_Node* next;
};

typedef struct {
    Expression_Node* condition;
    Expression_Node* inside;
} While_Node;

typedef enum {
    Operator_Add,
    Operator_Subtract,
    Operator_Multiply,
    Operator_Divide,
    Operator_Equal,
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
        Operator operator;
    } data;
    Array_Expression_Node arguments;
} Invoke_Node;

typedef union {
    struct {
        char* name;
        Expression_Node* expression;
    } single;
    Array_String multi;
} Assign_Retrieve_Data;

typedef struct {
    enum {
        Retrieve_Single,
        Retrieve_Multi,
    } kind;
    Assign_Retrieve_Data data;
} Retrieve_Node;

typedef struct {
    Array_Expression_Node expressions;
} Multi_Expression_Node;

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
    } data;
};

typedef struct {
    Expression_Node* expression;
} Statement_Expression_Node;

typedef struct {
    Array_Declaration declarations;
    Expression_Node* expression;
} Statement_Declare_Node;

typedef struct {
    enum {
        Assign_Single,
        Assign_Multi,
    } kind;
    Assign_Retrieve_Data data;
} Statement_Assign_Part;

Dynamic_Array_Def(Statement_Assign_Part, Array_Statement_Assign_Part, array_statement_assign_part_)

typedef struct {
    Array_Statement_Assign_Part parts;
    Expression_Node* expression;
} Statement_Assign_Node;

struct Statement_Node {
    enum {
        Statement_Expression,
        Statement_Declare,
        Statement_Assign,
    } kind;
    union {
        Statement_Expression_Node expression;
        Statement_Declare_Node declare;
        Statement_Assign_Node assign;
    } data;
};

typedef struct {
    Array_Declaration arguments;
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

typedef struct {
    char* name;
    enum {
        Definition_Procedure,
        Definition_Type,
    } kind;
    union {
        Procedure_Node procedure;
        Type_Node type;
    } data;
} Definition_Node;

Dynamic_Array_Def(Definition_Node, Array_Definition_Node, array_definition_node_)

typedef struct {
    Array_Definition_Node definitions;
} File_Node;

File_Node parse(Tokens* tokens);

#endif
