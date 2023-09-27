#ifndef __ASTFILE
#define __ASTFILE

#include "dynamic_array.h"
#include "tokenizer.h"

#include "string_util.h"

struct Ast_Type;
typedef struct Ast_Type Ast_Type;

Dynamic_Array_Def(Ast_Type*, Array_Ast_Type, array_ast_type_)

Dynamic_Array_Def(Array_Ast_Type, Array_Array_Ast_Type, array_array_ast_type_)

struct Ast_Expression;
typedef struct Ast_Expression Ast_Expression;

typedef struct {
    Ast_Expression* expression;
    bool result;
} Ast_Directive_If;

typedef enum {
    Directive_If,
} Directive_Kind;

typedef struct {
    Directive_Kind kind;
    union {
        Ast_Directive_If if_;
    } data;
} Ast_Directive;

Dynamic_Array_Def(Ast_Directive, Array_Ast_Directive, array_ast_directive_)

struct Ast_Item;
typedef struct Ast_Item Ast_Item;

typedef struct {
    char* name;
} Ast_Identifier;

typedef enum {
    Type_UInt,
    Type_UInt64,
    Type_UInt32,
    Type_UInt16,
    Type_UInt8,
    Type_Float64,
    Type_Byte,
    Type_Ptr,
    Type_Bool,
} Ast_Type_Internal;

typedef struct {
    Ast_Identifier identifier;
} Ast_Type_Basic;

typedef struct {
    Ast_Type* child;
} Ast_Type_Pointer;

typedef struct {
    Array_Ast_Type arguments;
    Array_Ast_Type returns;
} Ast_Type_Procedure;

typedef struct {
    Ast_Type* size_type;
    bool has_size;
    Ast_Type* element_type;
} Ast_Type_Array;

struct Ast_Declaration;
typedef struct Ast_Declaration Ast_Declaration;

Dynamic_Array_Def(Ast_Declaration*, Array_Ast_Declaration_Pointer, array_ast_declaration_pointer_)

struct Ast_Macro_Syntax_Data;
typedef struct Ast_Macro_Syntax_Data Ast_Macro_Syntax_Data;

Dynamic_Array_Def(Ast_Macro_Syntax_Data*, Array_Ast_Macro_Syntax_Data, array_ast_macro_syntax_data_)

struct Ast_Macro_Argument;
typedef struct Ast_Macro_Argument Ast_Macro_Argument;

typedef enum {
    Macro_None,
    Macro_Expression,
    Macro_Type,
} Ast_Macro_Syntax_Kind;

struct Ast_Macro_Argument {
    Ast_Macro_Syntax_Kind kind;
    bool multiple;
};

struct Ast_Macro_Syntax_Data {
    Ast_Macro_Syntax_Kind kind;
    union {
        Ast_Expression* expression;
        Ast_Type* type;
    } data;
};

typedef struct {
    Array_Ast_Declaration_Pointer items;
} Ast_Type_Struct;

typedef struct {
    Array_Ast_Declaration_Pointer items;
} Ast_Type_Union;

typedef struct {
    Array_String items;
} Ast_Type_Enum;

typedef struct {
    size_t value;
} Ast_Type_Number;

typedef struct {
    Ast_Expression* expression;
    Ast_Type* computed_result_type;
} Ast_Type_TypeOf;

typedef struct {
    Ast_Identifier identifier;
    Array_Ast_Macro_Syntax_Data arguments;
    Location location;

    Ast_Macro_Syntax_Data result;
} Ast_RunMacro;

struct Ast_Type {
    Array_Ast_Directive directives;
    enum {
        Type_None,
        Type_Basic,
        Type_Pointer,
        Type_Procedure,
        Type_Array,
        Type_Internal,
        Type_Struct,
        Type_Union,
        Type_Enum,
        Type_Number,
        Type_TypeOf,
        Type_RegisterSize,
        Type_RunMacro,
    } kind;
    union {
        Ast_Type_Basic basic;
        Ast_Type_Pointer pointer;
        Ast_Type_Procedure procedure;
        Ast_Type_Array array;
        Ast_Type_Internal internal;
        Ast_Type_Struct struct_;
        Ast_Type_Union union_;
        Ast_Type_Enum enum_;
        Ast_Type_Number number;
        Ast_Type_TypeOf type_of;
        Ast_RunMacro run_macro;
    } data;
};

struct Ast_Statement;
typedef struct Ast_Statement Ast_Statement;

struct Ast_Declaration {
    char* name;
    Ast_Type type;
    Location location;
};

Dynamic_Array_Def(Ast_Statement*, Array_Ast_Statement, array_ast_statement_)
Dynamic_Array_Def(Ast_Expression*, Array_Ast_Expression, array_ast_expression_)
Dynamic_Array_Def(Ast_Declaration, Array_Ast_Declaration, array_ast_declaration_)

typedef struct {
    Array_Ast_Statement statements;
} Ast_Expression_Block;

typedef struct {
    enum {
        Number_Integer,
        Number_Decimal,
    } kind;
    union {
        size_t integer;
        double decimal;
    } value;
    Ast_Type* type;
} Ast_Expression_Number;

typedef struct {
    char* value;
} Ast_Expression_String;

typedef struct {
    char value;
} Ast_Expression_Char;

typedef struct {
    bool value;
} Ast_Expression_Boolean;

typedef struct {
    Ast_Type* type;
} Ast_Expression_Null;

typedef struct {
    Ast_Expression* condition;
    Ast_Expression* if_expression;
    Ast_Expression* else_expression;
    Location location;
} Ast_Expression_If;

typedef struct {
    Ast_Expression* condition;
    Ast_Expression* inside;
    Location location;
} Ast_Expression_While;

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
    Operator_Not,
    Operator_And,
    Operator_Or,
} Operator;

typedef struct {
    enum {
        Invoke_Standard,
        Invoke_Operator,
    } kind;
    union {
        Ast_Expression* procedure;
        struct {
            Operator operator_;
            Ast_Type computed_operand_type;
        } operator_;
    } data;
    Array_Ast_Expression arguments;
    Location location;
} Ast_Expression_Invoke;

typedef struct {
    enum {
        Retrieve_Assign_Identifier,
        Retrieve_Assign_Parent,
        Retrieve_Assign_Array,
    } kind;
    union {
        Ast_Identifier identifier;
        struct {
            Ast_Expression* expression;
            char* name;
            Ast_Type computed_parent_type;
            bool needs_reference;
        } parent;
        struct {
            Ast_Expression* expression_outer;
            Ast_Expression* expression_inner;
            Ast_Type computed_array_type;
        } array;
    } data;
    Ast_Type* computed_result_type;
    Location location;
} Retrieve_Assign_Node;

typedef Retrieve_Assign_Node Ast_Expression_Retrieve;

typedef struct {
    Array_Ast_Expression expressions;
} Ast_Expression_Multiple;

typedef struct {
    Ast_Expression* inner;
} Ast_Expression_Reference;

typedef struct {
    Ast_Type type;
    Ast_Expression* expression;
    Ast_Type computed_input_type;
    Location location;
} Ast_Expression_Cast;

typedef struct {
    Ast_Type type;
    Array_Ast_Expression arguments;
    Location location;
} Ast_Expression_Build;

typedef struct {
    Ast_Type type;
    Location location;
} Ast_Expression_Init;

typedef struct {
    Ast_Type type;
    Ast_Type computed_result_type;
    Location location;
} Ast_Expression_SizeOf;

typedef struct {
    Ast_Type type;
    Ast_Type computed_result_type;
    Location location;
} Ast_Expression_LengthOf;

typedef struct {
    Ast_Type wanted;
    Ast_Type given;
} Ast_Expression_IsType;

struct Ast_Expression {
    Array_Ast_Directive directives;
    enum {
        Expression_Invalid,
        Expression_Block,
        Expression_Number,
        Expression_String,
        Expression_Char,
        Expression_Invoke,
        Expression_RunMacro,
        Expression_Retrieve,
        Expression_If,
        Expression_While,
        Expression_Multiple,
        Expression_Reference,
        Expression_Boolean,
        Expression_Null,
        Expression_Cast,
        Expression_Init,
        Expression_Build,
        Expression_SizeOf,
        Expression_LengthOf,
        Expression_IsType,
    } kind;
    union {
        Ast_Expression_Block block;
        Ast_Expression_Number number;
        Ast_Expression_String string;
        Ast_Expression_Char char_;
        Ast_Expression_Invoke invoke;
        Ast_RunMacro run_macro;
        Ast_Expression_Retrieve retrieve;
        Ast_Expression_If if_;
        Ast_Expression_While while_;
        Ast_Expression_Multiple multiple;
        Ast_Expression_Reference reference;
        Ast_Expression_Boolean boolean;
        Ast_Expression_Null null;
        Ast_Expression_Cast cast;
        Ast_Expression_Init init;
        Ast_Expression_Build build;
        Ast_Expression_SizeOf size_of;
        Ast_Expression_LengthOf length_of;
        Ast_Expression_IsType is_type;
    } data;
};

typedef struct {
    Ast_Expression* expression;
} Ast_Statement_Expression;

typedef struct {
    Array_Ast_Declaration declarations;
    Ast_Expression* expression;
} Ast_Statement_Declare;

typedef Retrieve_Assign_Node Statement_Assign_Part;

Dynamic_Array_Def(Statement_Assign_Part, Array_Statement_Assign_Part, array_statement_assign_part_)

typedef struct {
    Array_Statement_Assign_Part parts;
    Ast_Expression* expression;
} Ast_Statement_Assign;

typedef struct {
    Ast_Expression* expression;
    Location location;
} Ast_Statement_Return;

struct Ast_Statement {
    Array_Ast_Directive directives;
    enum {
        Statement_Expression,
        Statement_Declare,
        Statement_Assign,
        Statement_Return,
    } kind;
    union {
        Ast_Statement_Expression expression;
        Ast_Statement_Declare declare;
        Ast_Statement_Assign assign;
        Ast_Statement_Return return_;
    } data;
    Location statement_end_location;
};

typedef struct {
    Array_String bindings;
    bool varargs;
    Ast_Macro_Syntax_Data data;
} Ast_Macro_Variant;

Dynamic_Array_Def(Ast_Macro_Variant, Array_Ast_Macro_Variant, array_ast_macro_variant_)
Dynamic_Array_Def(Ast_Macro_Argument, Array_Ast_Macro_Argument, array_ast_macro_syntax_kind_)

Dynamic_Array_Def(Ast_Item, Array_Ast_Item, array_ast_item_)

typedef struct {
    char* name;
    Array_Ast_Declaration arguments;
    Array_Ast_Type returns;
    Ast_Expression* body;
} Ast_Item_Procedure;

typedef struct {
    char* name;
    Array_Ast_Macro_Argument arguments;
    Ast_Macro_Argument return_;
    Array_Ast_Macro_Variant variants;
} Ast_Item_Macro;

typedef struct {
    char* name;
    Ast_Type type;
} Ast_Item_Type;

typedef struct {
    char* name;
    Ast_Type type;
} Ast_Item_Global;

typedef struct {
    char* name;
    Ast_Expression_Number expression;
} Ast_Item_Constant;

struct Ast_Item {
    Array_Ast_Directive directives;
    enum {
        Item_Procedure,
        Item_Macro,
        Item_Type,
        Item_Global,
        Item_Constant,
    } kind;
    union {
        Ast_Item_Procedure procedure;
        Ast_Item_Macro macro;
        Ast_Item_Type type;
        Ast_Item_Global global;
        Ast_Item_Constant constant;
    } data;
};

typedef struct {
    char* path;
    size_t id;
    Array_Ast_Item items;
} Ast_File;

Dynamic_Array_Def(Ast_File, Program, program_)

#endif
