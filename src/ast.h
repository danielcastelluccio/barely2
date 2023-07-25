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
    Type_U8,
    Type_U4,
    Type_U2,
    Type_U1,
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
            Type computed_operand_type;
        } operator;
    } data;
    Array_Expression_Node arguments;
    Location location;
} Invoke_Node;

typedef struct {
    enum {
        Identifier_Single,
        Identifier_Multi,
    } kind;
    union {
        char* single;
        Array_String multi;
    } data;
} Item_Identifier;

typedef struct {
    enum {
        Retrieve_Assign_Identifier,
        Retrieve_Assign_Struct,
        Retrieve_Assign_Array,
    } kind;
    union {
        Item_Identifier identifier;
        struct {
            Expression_Node* expression;
            char* name;
            Type computed_struct_type;
        } struct_;
        struct {
            Expression_Node* expression_outer;
            Expression_Node* expression_inner;
            Type computed_array_type;
        } array;
    } data;
    Location location;
} Retrieve_Assign_Node;

typedef Retrieve_Assign_Node Retrieve_Node;

typedef struct {
    Array_Expression_Node expressions;
} Multi_Expression_Node;

typedef struct {
    Expression_Node* inner;
} Reference_Node;

typedef struct {
    Type type;
    Expression_Node* expression;
    Type computed_input_type;
    Location location;
} Cast_Node;

typedef struct {
    Type type;
    Type computed_result_type;
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

typedef Retrieve_Assign_Node Statement_Assign_Part;

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

struct Item_Node;
typedef struct Item_Node Item_Node;

Dynamic_Array_Def(Item_Node, Array_Item_Node, array_item_node_)

typedef struct {
    Array_Item_Node items;
} Module_Node;

typedef struct {
    Type type;
} Global_Node;

typedef struct {
    char* package;
    char* path;
} Use_Node;

struct Item_Node {
    char* name;
    enum {
        Item_Procedure,
        Item_Type,
        Item_Module,
        Item_Global,
        Item_Use,
    } kind;
    union {
        Procedure_Node procedure;
        Type_Node type;
        Module_Node module;
        Global_Node global;
        Use_Node use;
    } data;
};

typedef struct {
    char* path;
    size_t id;
    Array_Item_Node items;
} File_Node;

Dynamic_Array_Def(File_Node, Program, program_)

#endif
