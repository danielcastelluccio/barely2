#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "tokenizer.h"

void print_token_error_stub(Token* token) {
    printf("%s:%zu:%zu: ", token->location.file, token->location.row, token->location.col);
}

Token_Kind peek(Parser_State* state) {
    Token* current = &state->tokens->elements[state->index];
    return current->kind;
}

Token_Kind consume(Parser_State* state) {
    Token* current = &state->tokens->elements[state->index];
    state->index++;
    return current->kind;
}

void consume_check(Parser_State* state, Token_Kind wanted_kind) {
    Token* current = &state->tokens->elements[state->index];
    if (current->kind != wanted_kind) {
        Token temp = (Token) {
            wanted_kind,
            NULL,
            (Location) {
                NULL,
                0,
                0,
            },
        };

        print_token_error_stub(current);
        printf("Expected ");
        print_token(&temp, false);
        printf(", got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    state->index++;
}

char* consume_string(Parser_State* state) {
    Token* current = &state->tokens->elements[state->index];
    if (current->kind != Token_String) {
        printf("Error: Expected String, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    state->index++;
    return current->data;
}

char* consume_boolean(Parser_State* state) {
    Token* current = &state->tokens->elements[state->index];
    if (current->kind != Token_Boolean) {
        printf("Error: Expected Boolean, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    state->index++;
    return current->data;
}

char* consume_number(Parser_State* state) {
    Token* current = &state->tokens->elements[state->index];
    if (current->kind != Token_Number) {
        printf("Error: Expected Number, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    state->index++;
    return current->data;
}

char* consume_keyword(Parser_State* state) {
    Token* current = &state->tokens->elements[state->index];
    if (current->kind != Token_Keyword) {
        print_token_error_stub(current);
        printf("Expected Keyword, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    state->index++;
    return current->data;
}

char* consume_identifier(Parser_State* state) {
    Token* current = &state->tokens->elements[state->index];
    if (current->kind != Token_Identifier) {
        print_token_error_stub(current);
        printf("Expected Identifier, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    state->index++;
    return current->data;
}

Type parse_type(Parser_State* state);

void parse_directives(Parser_State* state) {
    while (peek(state) == Token_Identifier && state->tokens->elements[state->index].data[0] == '#') {
        Directive_Node directive;
        char* directive_string = consume_identifier(state);

        if (strcmp(directive_string, "#if") == 0) {
            Directive_If_Node if_node;
            consume_check(state, Token_LeftParenthesis);

            Expression_Node* expression = malloc(sizeof(Expression_Node));
            *expression = parse_expression(state);
            if_node.expression = expression;

            consume_check(state, Token_RightParenthesis);

            directive.kind = Directive_If;
            directive.data.if_ = if_node;
        }

        array_directive_append(&state->directives, directive);
    }
}

void filter_add_directive(Parser_State* state, Array_Directive* directives, Directive_Kind kind) {
    size_t i = 0;
    while (i < state->directives.count) {
        if (state->directives.elements[i].kind == kind) {
            array_directive_append(directives, state->directives.elements[i]);
            size_t j = i + 1;
            while (j < state->directives.count) {
                state->directives.elements[j - 1] = state->directives.elements[j];
                j++;
            }
            state->directives.count--;
        } else {
            i++;
        }
    }
}

Type parse_type(Parser_State* state) {
    Type result = { .directives = array_directive_new(1) };

    parse_directives(state);

    if (peek(state) == Token_Asterisk) {
        Pointer_Type pointer;

        consume(state);

        Type child = parse_type(state);
        Type* child_allocated = malloc(sizeof(Type));
        *child_allocated = child;
        pointer.child = child_allocated;

        result.kind = Type_Pointer;
        result.data.pointer = pointer;
    } else if (peek(state) == Token_LeftBracket) {
        consume(state);
        
        BArray_Type array = {};

        if (peek(state) != Token_RightBracket) {
            array.has_size = true;
            Type* size_type = malloc(sizeof(Type));
            *size_type = parse_type(state);
            array.size_type = size_type;
        }

        consume_check(state, Token_RightBracket);

        Type child = parse_type(state);
        Type* child_allocated = malloc(sizeof(Type));
        *child_allocated = child;
        array.element_type = child_allocated;

        result.kind = Type_Array;
        result.data.array = array;
    } else if (peek(state) == Token_Keyword) {
        char* keyword = consume_keyword(state);

        if (strcmp(keyword, "proc") == 0) {
            consume(state);

            Procedure_Type procedure = {
                .arguments = array_type_new(2),
                .returns = array_type_new(1),
            };

            while (peek(state) != Token_RightParenthesis) {
                if (peek(state) == Token_Comma) {
                    consume(state);
                    continue;
                }

                Type* type = malloc(sizeof(Type));
                *type = parse_type(state);
                array_type_append(&procedure.arguments, type);
            }

            consume(state);

            if (peek(state) == Token_Colon) {
                consume(state);

                bool looking_at_returns = true;
                while (looking_at_returns) {
                    Type* type = malloc(sizeof(Type));
                    *type = parse_type(state);
                    array_type_append(&procedure.returns, type);

                    looking_at_returns = peek(state) == Token_Comma;
                }
            }

            result.kind = Type_Procedure;
            result.data.procedure = procedure;
        } else if (strcmp(keyword, "struct") == 0 || strcmp(keyword, "union") == 0) {
            bool is_struct = strcmp(keyword, "struct") == 0;
            consume(state);

            Array_Declaration_Pointer items = array_declaration_pointer_new(4);
            while (peek(state) != Token_RightCurlyBrace) {
                if (peek(state) == Token_Comma) {
                    consume(state);
                    continue;
                }

                Location location = state->tokens->elements[state->index].location;
                char* name = consume_identifier(state);
                consume_check(state, Token_Colon);
                Type type = parse_type(state);

                Declaration* declaration = malloc(sizeof(Declaration));
                declaration->name = name;
                declaration->type = type;
                declaration->location = location;
                array_declaration_pointer_append(&items, declaration);
            }
            consume(state);

            if (is_struct) {
                Struct_Type struct_type;
                struct_type.items = items;
                result.kind = Type_Struct;
                result.data.struct_ = struct_type;
            } else {
                Union_Type union_type;
                union_type.items = items;
                result.kind = Type_Union;
                result.data.union_ = union_type;
            }
        } else if (strcmp(keyword, "enum") == 0) {
            Enum_Type enum_type;
            consume(state);

            Array_String items = array_string_new(4);
            while (peek(state) != Token_RightCurlyBrace) {
                if (peek(state) == Token_Comma) {
                    consume(state);
                    continue;
                }

                char* name = consume_identifier(state);
                array_string_append(&items, name);
            }
            enum_type.items = items;
            consume(state);

            result.kind = Type_Enum;
            result.data.enum_ = enum_type;
        } else {
            assert(false);
        }
    } else if (peek(state) == Token_Number) {
        Number_Type number_type;
        char* value_string = consume_number(state);
        number_type.value = atoll(value_string);

        result.kind = Type_Number;
        result.data.number = number_type;
    } else {
        char* name = consume_identifier(state);
        Internal_Type internal;
        bool found = false;

        if (!found) {
            if (strcmp(name, "@typeof") == 0) {
                TypeOf_Type type_of;

                consume_check(state, Token_LeftParenthesis);

                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = parse_expression(state);
                type_of.expression = expression;

                consume_check(state, Token_RightParenthesis);

                result.kind = Type_TypeOf;
                result.data.type_of = type_of;
                found = true;
            }
        }

        if (!found) {
            if (strcmp(name, "usize") == 0) {
                internal = Type_USize;
                found = true;
            } else if (strcmp(name, "u64") == 0) {
                internal = Type_U64;
                found = true;
            } else if (strcmp(name, "u32") == 0) {
                internal = Type_U32;
                found = true;
            } else if (strcmp(name, "u16") == 0) {
                internal = Type_U16;
                found = true;
            } else if (strcmp(name, "u8") == 0) {
                internal = Type_U8;
                found = true;
            } else if (strcmp(name, "f64") == 0) {
                internal = Type_F8;
                found = true;
            } else if (strcmp(name, "ptr") == 0) {
                internal = Type_Ptr;
                found = true;
            } else if (strcmp(name, "bool") == 0) {
                internal = Type_Bool;
                found = true;
            }

            if (found) {
                result.kind = Type_Internal;
                result.data.internal = internal;
            }
        }

        if (!found) {
            Basic_Type basic = {};
            basic.kind = Type_Single;

            basic.data.single = name;

            if (peek(state) == Token_DoubleColon) {
                Array_String names = array_string_new(2);
                array_string_append(&names, basic.data.single);
                while (peek(state) == Token_DoubleColon) {
                    consume(state);
                    array_string_append(&names, consume_identifier(state));
                }

                basic.kind = Type_Multi;
                basic.data.multi = names;
            }

            result.kind = Type_Basic;
            result.data.basic = basic;
        }
    }

    return result;
}

Expression_Node parse_multi_expression(Parser_State* state) {
    Expression_Node result = parse_expression(state);

    if (peek(state) == Token_Comma) {
        consume(state);
        if (result.kind == Expression_Multi) {
            Expression_Node* next = malloc(sizeof(Expression_Node));
            *next = parse_expression(state);
            array_expression_node_append(&result.data.multi.expressions, next);
        } else {
            Multi_Expression_Node node;
            node.expressions = array_expression_node_new(2);

            Expression_Node* previous_expression = malloc(sizeof(Expression_Node));
            *previous_expression = result;
            array_expression_node_append(&node.expressions, previous_expression);

            Expression_Node next = parse_expression(state);
            Expression_Node* next_allocated = malloc(sizeof(Expression_Node));
            *next_allocated = next;
            if (next_allocated->kind == Expression_Multi) {
                for (size_t i = 0; i < next_allocated->data.multi.expressions.count; i++) {
                    array_expression_node_append(&node.expressions, next_allocated->data.multi.expressions.elements[i]);
                }
            } else {
                array_expression_node_append(&node.expressions, next_allocated);
            }

            result.kind = Expression_Multi;
            result.data.multi = node;
        }
    }

    return result;
};

Statement_Node parse_statement(Parser_State* state) {
    Statement_Node result = { .directives = array_directive_new(1) };

    parse_directives(state);
    filter_add_directive(state, &result.directives, Directive_If);

    Token_Kind token = peek(state);
    if (token == Token_Keyword && strcmp(state->tokens->elements[state->index].data, "var") == 0) {
        Statement_Declare_Node node = {};

        consume(state);

        Array_Declaration declarations = array_declaration_new(8);
        while (peek(state) != Token_Equals && peek(state) != Token_Semicolon) {
            if (peek(state) == Token_Comma) {
                consume(state);
                continue;
            }
            Declaration declaration;
            declaration.location = state->tokens->elements[state->index].location;
            declaration.name = consume_identifier(state);
            consume_check(state, Token_Colon);
            declaration.type = parse_type(state);
            array_declaration_append(&declarations, declaration);
        }
        node.declarations = declarations;

        Token_Kind next = consume(state);
        if (next == Token_Equals) {
            Expression_Node* expression = malloc(sizeof(Expression_Node));
            *expression = parse_multi_expression(state);
            node.expression = expression;

            consume_check(state, Token_Semicolon);
        } else if (next == Token_Semicolon) {
        } else {
            print_token_error_stub(&state->tokens->elements[state->index - 1]);
            printf("Unexpected token ");
            print_token(&state->tokens->elements[state->index - 1], false);
            printf("\n");
            exit(1);
        }

        result.kind = Statement_Declare;
        result.data.declare = node;
    } else if (token == Token_Keyword && strcmp(state->tokens->elements[state->index].data, "return") == 0) {
        Statement_Return_Node node;
        node.location = state->tokens->elements[state->index].location;

        consume(state);

        Expression_Node expression = parse_expression(state);
        Expression_Node* expression_allocated = malloc(sizeof(Expression_Node));
        *expression_allocated = expression;
        node.expression = expression_allocated;

        consume_check(state, Token_Semicolon);

        result.kind = Statement_Return;
        result.data.return_ = node;
    } else {
        Statement_Expression_Node node;

        Expression_Node* expression = malloc(sizeof(Expression_Node));
        *expression = parse_multi_expression(state);
        node.expression = expression;

        Token_Kind token = consume(state);
        switch (token) {
            case Token_Equals: {
                Statement_Assign_Node assign = {};
                assign.parts = array_statement_assign_part_new(2);

                if (expression->kind == Expression_Retrieve) {
                    Statement_Assign_Part assign_part;
                    assign_part = expression->data.retrieve;
                    array_statement_assign_part_append(&assign.parts, assign_part);
                } else if (expression->kind == Expression_Multi) {
                    Multi_Expression_Node* multi = &expression->data.multi;
                    for (size_t i = 0; i < multi->expressions.count; i++) {
                        Expression_Node* expression = multi->expressions.elements[i];
                        Statement_Assign_Part assign_part;
                        assign_part = expression->data.retrieve;
                        array_statement_assign_part_append(&assign.parts, assign_part);
                    }
                }

                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = parse_multi_expression(state);
                assign.expression = expression;

                result.kind = Statement_Assign;
                result.data.assign = assign;

                consume_check(state, Token_Semicolon);
                break;
            }
            case Token_Semicolon: {
                result.statement_end_location = state->tokens->elements[state->index - 1].location;
                result.kind = Statement_Expression;
                result.data.expression = node;
                break;
            }
            default: {
                print_token_error_stub(&state->tokens->elements[state->index - 1]);
                printf("Unexpected token ");
                print_token(&state->tokens->elements[state->index - 1], false);
                printf("\n");
                exit(1);
                break;
            }
        }
    }

    return result;
}

int get_precedence(Parser_State* state) {
    Token_Kind kind = peek(state);

    if (kind == Token_Plus ||
            kind == Token_Minus ||
            kind == Token_Percent) {
        return 3;
    }
    if (kind == Token_Asterisk ||
            kind == Token_Slash) {
        return 4;
    }
    if (kind == Token_DoubleEquals ||
            kind == Token_ExclamationEquals ||
            kind == Token_GreaterThan ||
            kind == Token_LessThan ||
            kind == Token_LessThanEqual ||
            kind == Token_GreaterThanEqual) {
        return 2;
    }
    if (kind == Token_DoubleAmpersand ||
            kind == Token_DoubleBar) {
        return 1;
    }
    return -1;
}

Macro_Syntax_Kind parse_macro_syntax_kind(Parser_State* state) {
    char* name = consume_identifier(state);
    Macro_Syntax_Kind result;
    if (strcmp(name, "$expr") == 0) {
        result = (Macro_Syntax_Kind) { .kind = Macro_Expression };
    } else {
        assert(false);
    }

    if (peek(state) == Token_DoublePeriod) {
        consume(state);
        Macro_Syntax_Kind* result_allocated = malloc(sizeof(Macro_Syntax_Kind));
        *result_allocated = result;
        result = (Macro_Syntax_Kind) { .kind = Macro_Multiple, .data = { .multiple = result_allocated } };
    }
    return result;
}

Macro_Syntax_Data parse_macro_syntax_data_inner(Parser_State* state, Macro_Syntax_Kind kind) {
    Macro_Syntax_Data result;

    switch (kind.kind) {
        case Macro_Expression: {
            Expression_Node* expression = malloc(sizeof(Expression_Node));
            *expression = parse_expression(state);

            result.kind.kind = Macro_Expression;
            result.data.expression = expression;
            break;
        }
        case Macro_Multiple: {
            Macro_Syntax_Kind* inner = kind.data.multiple;

            result.kind.kind = Macro_Multiple;
            result.kind.data.multiple = kind.data.multiple;
            result.data.multiple = malloc(sizeof(Macro_Syntax_Data));

            *result.data.multiple = parse_macro_syntax_data_inner(state, *inner);
            break;
        }
        default:
            assert(false);
    }

    return result;
}

Macro_Syntax_Data parse_macro_syntax_data(Parser_State* state) {
    Macro_Syntax_Kind kind = parse_macro_syntax_kind(state);
    return parse_macro_syntax_data_inner(state, kind);
}

Expression_Node parse_expression_without_operators(Parser_State* state) {
    Expression_Node result = { .directives = array_directive_new(1) };

    parse_directives(state);

    switch (peek(state)) {
        case Token_LeftCurlyBrace: {
            Block_Node node;
            consume(state);

            Array_Statement_Node statements = array_statement_node_new(32);

            while (peek(state) != Token_RightCurlyBrace) {
                Statement_Node* statement = malloc(sizeof(Statement_Node));
                *statement = parse_statement(state);
                array_statement_node_append(&statements, statement);
            }
            node.statements = statements;
            consume(state);

            result.kind = Expression_Block;
            result.data.block = node;
            break;
        }
        case Token_Number: {
            Number_Node node = {};

            char* string_value = consume_number(state);

            if (string_contains(string_value, '.')) {
                double value = strtod(string_value, NULL);
                node.kind = Number_Decimal;
                node.value.decimal = value;
            } else {
                size_t value = strtoul(string_value, NULL, 0);
                node.kind = Number_Integer;
                node.value.integer = value;
            }

            result.kind = Expression_Number;
            result.data.number = node;
            break;
        }
        case Token_String: {
            String_Node node;

            node.value = consume_string(state);

            result.kind = Expression_String;
            result.data.string = node;
            break;
        }
        case Token_Boolean: {
            Boolean_Node node;

            node.value = strcmp(consume_boolean(state), "true") == 0;

            result.kind = Expression_Boolean;
            result.data.boolean = node;
            break;
        }
        case Token_Identifier: {
            Location location = state->tokens->elements[state->index].location;
            char* name = consume_identifier(state);
            if (strcmp(name, "@sizeof") == 0) {
                SizeOf_Node node;

                consume_check(state, Token_LeftParenthesis);

                Type type = parse_type(state);
                node.type = type;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_SizeOf;
                result.data.size_of = node;
            } else if (strcmp(name, "@lengthof") == 0) {
                LengthOf_Node node;

                consume_check(state, Token_LeftParenthesis);

                Type type = parse_type(state);
                node.type = type;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_LengthOf;
                result.data.length_of = node;
            } else if (strcmp(name, "@istype") == 0) {
                IsType_Node node;

                consume_check(state, Token_LeftParenthesis);

                node.wanted = parse_type(state);

                consume_check(state, Token_Comma);

                node.given = parse_type(state);

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_IsType;
                result.data.is_type = node;
            } else if (strcmp(name, "@cast") == 0) {
                Cast_Node node;
                node.location = location;

                consume_check(state, Token_LeftParenthesis);

                Type type = parse_type(state);
                node.type = type;

                consume_check(state, Token_Comma);

                Expression_Node inner = parse_expression(state);
                Expression_Node* inner_allocated = malloc(sizeof(Expression_Node));
                *inner_allocated = inner;
                node.expression = inner_allocated;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_Cast;
                result.data.cast = node;
                break;
            } else if (strcmp(name, "@init") == 0) {
                Init_Node node;
                node.location = location;

                consume_check(state, Token_LeftParenthesis);

                Type type = parse_type(state);
                node.type = type;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_Init;
                result.data.init = node;
                break;
            } else if (strcmp(name, "@build") == 0) {
                Build_Node node;
                node.location = location;

                consume_check(state, Token_LeftParenthesis);

                Type type = parse_type(state);
                node.type = type;

                Array_Expression_Node arguments = array_expression_node_new(32);

                while (peek(state) != Token_RightParenthesis) {
                    if (peek(state) == Token_Comma) {
                        consume(state);
                        continue;
                    }

                    Expression_Node* expression = malloc(sizeof(Expression_Node));
                    *expression = parse_expression(state);

                    array_expression_node_append(&arguments, expression);
                }

                node.arguments = arguments;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_Build;
                result.data.build = node;
                break;
            } else {
                Retrieve_Node node = {};
                node.location = location;
                node.kind = Retrieve_Assign_Identifier;
                Identifier identifier = {};
                identifier.kind = Identifier_Single;
                identifier.data.single = name;

                if (peek(state) == Token_DoubleColon) {
                    identifier.kind = Identifier_Multi;
                    Array_String names = array_string_new(2);
                    array_string_append(&names, name);
                    while (peek(state) == Token_DoubleColon) {
                        consume(state);

                        array_string_append(&names, consume_identifier(state));
                    }
                    identifier.data.multi = names;
                }

                node.kind = Retrieve_Assign_Identifier;
                node.data.identifier = identifier;
                result.kind = Expression_Retrieve;
                result.data.retrieve = node;
            }
            break;
        }
        case Token_DoublePeriod: {
            consume(state);

            Retrieve_Node node = {};
            node.kind = Retrieve_Assign_Identifier;
            Identifier identifier = {};
            identifier.kind = Identifier_Single;
            identifier.data.single = "..";

            node.kind = Retrieve_Assign_Identifier;
            node.data.identifier = identifier;
            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            break;
        }
        case Token_DoubleColon: {
            Retrieve_Node node = {};
            node.location = state->tokens->elements[state->index].location;
            node.kind = Retrieve_Assign_Identifier;
            Identifier identifier = {};

            identifier.kind = Identifier_Multi;
            Array_String names = array_string_new(2);
            array_string_append(&names, "");
            while (peek(state) == Token_DoubleColon) {
                consume(state);

                array_string_append(&names, consume_identifier(state));
            }
            identifier.data.multi = names;

            node.kind = Retrieve_Assign_Identifier;
            node.data.identifier = identifier;
            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            break;
        }
        case Token_Exclamation: {
            Invoke_Node node = { .arguments = array_expression_node_new(1) };
            node.kind = Invoke_Operator;
            node.location = state->tokens->elements[state->index].location;

            node.data.operator_.operator_ = Operator_Not;

            consume(state);

            Expression_Node* expression = malloc(sizeof(Expression_Node));
            *expression = parse_expression_without_operators(state);
            array_expression_node_append(&node.arguments, expression);

            result.kind = Expression_Invoke;
            result.data.invoke = node;
            break;
        }
        case Token_Keyword: {
            char* name = consume_keyword(state);

            if (strcmp(name, "if") == 0) {
                If_Node node = {};

                Expression_Node condition = parse_expression(state);
                Expression_Node* condition_allocated = malloc(sizeof(Expression_Node));
                *condition_allocated = condition;
                node.condition = condition_allocated;

                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = parse_expression(state);
                node.if_expression = expression;

                if (state->tokens->elements[state->index].kind == Token_Keyword && strcmp(state->tokens->elements[state->index].data, "else") == 0) {
                    consume(state);
                    Expression_Node* expression = malloc(sizeof(Expression_Node));
                    *expression = parse_expression(state);
                    node.else_expression = expression;
                }

                result.kind = Expression_If;
                result.data.if_ = node;
            } else if (strcmp(name, "while") == 0) {
                While_Node node;

                Expression_Node condition = parse_expression(state);
                Expression_Node* condition_allocated = malloc(sizeof(Expression_Node));
                *condition_allocated = condition;
                node.condition = condition_allocated;

                Expression_Node inside = parse_expression(state);
                Expression_Node* inside_allocated = malloc(sizeof(Expression_Node));
                *inside_allocated = inside;
                node.inside = inside_allocated;

                result.kind = Expression_While;
                result.data.while_ = node;
            } else {
                print_token_error_stub(&state->tokens->elements[state->index - 1]);
                printf("Unexpected token ");
                print_token(&state->tokens->elements[state->index - 1], false);
                printf("\n");
                exit(1);
            }
            break;
        }
        case Token_Ampersand: {
            Reference_Node node;

            consume(state);
            
            Expression_Node inner = parse_expression(state);
            Expression_Node* inner_allocated = malloc(sizeof(Expression_Node));
            *inner_allocated = inner;
            node.inner = inner_allocated;

            result.kind = Expression_Reference;
            result.data.reference = node;
            break;
        }
        case Token_LeftParenthesis: {
            consume(state);

            result = parse_expression(state);

            consume_check(state, Token_RightParenthesis);
            break;
        }
        default: {
            print_token_error_stub(&state->tokens->elements[state->index - 1]);
            printf("Unexpected token ");
            print_token(&state->tokens->elements[state->index], false);
            printf("\n");
            exit(1);
        }
    }

    bool running = true;
    while (running) {
        running = false;
        if (peek(state) == Token_Period) {
            running = true;
            Retrieve_Node node;
            node.kind = Retrieve_Assign_Parent;

            consume(state);

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.parent.expression = previous_result;

            if (peek(state) == Token_Asterisk) {
                consume(state);
                node.data.parent.name = "*";
            } else if (peek(state) == Token_QuestionMark) {
                consume(state);
                node.data.parent.name = "?";
            } else if (peek(state) == Token_DoubleQuestionMark) {
                consume(state);
                node.data.parent.name = "??";
            } else {
                node.data.parent.name = consume_identifier(state);
            }

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            continue;
        }

        if (peek(state) == Token_LeftBracket) {
            running = true;
            Retrieve_Node node;
            node.kind = Retrieve_Assign_Array;
            node.location = state->tokens->elements[state->index].location;

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.array.expression_outer = previous_result;

            consume(state);

            Expression_Node inner = parse_expression(state);
            Expression_Node* inner_allocated = malloc(sizeof(Expression_Node));
            *inner_allocated = inner;
            node.data.array.expression_inner = inner_allocated;

            consume_check(state, Token_RightBracket);

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            continue;
        }

        if (peek(state) == Token_LeftParenthesis) {
            running = true;
            Invoke_Node node;
            node.location = state->tokens->elements[state->index].location;
            node.kind = Invoke_Standard;

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.procedure = previous_result;
            consume(state);

            Array_Expression_Node arguments = array_expression_node_new(32);

            while (peek(state) != Token_RightParenthesis) {
                if (peek(state) == Token_Comma) {
                    consume(state);
                    continue;
                }

                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = parse_expression(state);

                array_expression_node_append(&arguments, expression);
            }

            node.arguments = arguments;

            consume(state);

            result.kind = Expression_Invoke;
            result.data.invoke = node;
            continue;
        }

        if (peek(state) == Token_Exclamation) {
            running = true;
            Run_Macro_Node node = { .arguments = array_macro_syntax_data_new(2) };
            assert(result.kind == Expression_Retrieve && result.data.retrieve.kind == Retrieve_Assign_Identifier);
            node.identifier = result.data.retrieve.data.identifier;
            node.location = state->tokens->elements[state->index].location;

            consume(state);
            consume_check(state, Token_LeftParenthesis);

            while (peek(state) != Token_RightParenthesis) {
                if (peek(state) == Token_Comma) {
                    consume(state);
                    continue;
                }

                Macro_Syntax_Data* data = malloc(sizeof(Macro_Syntax_Data));;
                *data = parse_macro_syntax_data(state);

                array_macro_syntax_data_append(&node.arguments, data);
            }

            consume(state);

            result.kind = Expression_RunMacro;
            result.data.run_macro = node;
            continue;
        }
    }

    return result;
}

Expression_Node parse_expression(Parser_State* state) {
    Expression_Node result = parse_expression_without_operators(state);

    if (get_precedence(state) > 0) {
        int previous_precedence = INT_MAX;
        while (get_precedence(state) > 0) {
            int current_precedence = get_precedence(state);

            Invoke_Node node;
            node.kind = Invoke_Operator;
            node.location = state->tokens->elements[state->index].location;

            Operator operator;
            switch (consume(state)) {
                case Token_Plus:
                    operator = Operator_Add;
                    break;
                case Token_Minus:
                    operator = Operator_Subtract;
                    break;
                case Token_Asterisk:
                    operator = Operator_Multiply;
                    break;
                case Token_Slash:
                    operator = Operator_Divide;
                    break;
                case Token_Percent:
                    operator = Operator_Modulus;
                    break;
                case Token_DoubleEquals:
                    operator = Operator_Equal;
                    break;
                case Token_ExclamationEquals:
                    operator = Operator_NotEqual;
                    break;
                case Token_GreaterThan:
                    operator = Operator_Greater;
                    break;
                case Token_LessThan:
                    operator = Operator_Less;
                    break;
                case Token_GreaterThanEqual:
                    operator = Operator_GreaterEqual;
                    break;
                case Token_LessThanEqual:
                    operator = Operator_LessEqual;
                    break;
                case Token_DoubleAmpersand:
                    operator = Operator_And;
                    break;
                case Token_DoubleBar:
                    operator = Operator_Or;
                    break;
                default:
                    assert(false);
                    break;
            }
            node.data.operator_.operator_ = operator;

            Expression_Node* parsed = malloc(sizeof(Expression_Node));
            *parsed = parse_expression_without_operators(state);

            if (current_precedence > previous_precedence) {
                Expression_Node* result_inner = malloc(sizeof(Expression_Node));

                Array_Expression_Node arguments = array_expression_node_new(32);
                array_expression_node_append(&arguments, array_expression_node_get(&result.data.invoke.arguments, 1));
                array_expression_node_append(&arguments, parsed);
                node.arguments = arguments;

                result_inner->kind = Expression_Invoke;
                result_inner->data.invoke = node;

                array_expression_node_set(&result.data.invoke.arguments, 1, result_inner);
            } else {
                Expression_Node* result_allocated = malloc(sizeof(Expression_Node));
                *result_allocated = result;

                Array_Expression_Node arguments = array_expression_node_new(32);
                array_expression_node_append(&arguments, result_allocated);
                array_expression_node_append(&arguments, parsed);
                node.arguments = arguments;

                result.kind = Expression_Invoke;
                result.data.invoke = node;
            }

            previous_precedence = current_precedence;
        }
    }

    return result;
}

Item_Node parse_item(Parser_State* state) {
    Item_Node result = { .directives = array_directive_new(1) };

    parse_directives(state);
    filter_add_directive(state, &result.directives, Directive_If);

    char* keyword = consume_keyword(state);
    if (strcmp(keyword, "proc") == 0) {
        char* name = consume_identifier(state);
        result.name = name;

        Procedure_Node node;
        consume_check(state, Token_LeftParenthesis);

        Array_Declaration arguments = array_declaration_new(4);
        while (peek(state) != Token_RightParenthesis) {
            if (peek(state) == Token_Comma) {
                consume(state);
                continue;
            }

            Location location = state->tokens->elements[state->index].location;
            char* name = consume_identifier(state);
            consume_check(state, Token_Colon);
            Type type = parse_type(state);
            array_declaration_append(&arguments, (Declaration) { name, type, location });
        }
        node.arguments = arguments;
        consume_check(state, Token_RightParenthesis);

        node.returns = array_type_new(1);
        if (peek(state) == Token_Colon) {
            consume(state);

            bool running = true;
            while (running) {
                Type type = parse_type(state);
                Type* type_allocated = malloc(sizeof(Type));
                *type_allocated = type;

                array_type_append(&node.returns, type_allocated);
                running = false;
                if (peek(state) == Token_Comma) {
                    running = true;
                    consume(state);
                }
            }
        }

        Expression_Node body = parse_expression(state);
        Expression_Node* body_allocated = malloc(sizeof(Expression_Node));
        *body_allocated = body;
        node.body = body_allocated;

        result.kind = Item_Procedure;
        result.data.procedure = node;
    } else if (strcmp(keyword, "macro") == 0) {
        char* name = consume_identifier(state);
        result.name = name;

        consume_check(state, Token_Exclamation);

        Macro_Node node = { .arguments = array_macro_syntax_kind_new(2), .variants = array_macro_variant_new(2) };
        consume_check(state, Token_LeftParenthesis);

        while (peek(state) != Token_RightParenthesis) {
            if (peek(state) == Token_Comma) {
                consume(state);
                continue;
            }

            Macro_Syntax_Kind argument = {};
            argument = parse_macro_syntax_kind(state);

            array_macro_syntax_kind_append(&node.arguments, argument);
        }

        consume(state);
        consume_check(state, Token_Colon);

        node.return_ = parse_macro_syntax_kind(state);

        consume_check(state, Token_LeftCurlyBrace);

        while (peek(state) != Token_RightCurlyBrace) {
            if (peek(state) == Token_Comma) {
                consume(state);
            }

            Macro_Variant variant = { .bindings = array_string_new(2) };
            consume_check(state, Token_LeftParenthesis);

            while (peek(state) != Token_RightParenthesis) {
                if (peek(state) == Token_Comma) {
                    consume(state);
                    continue;
                }

                if (peek(state) == Token_DoublePeriod) {
                    array_string_append(&variant.bindings, "..");
                    consume(state);
                } else {
                    array_string_append(&variant.bindings, consume_identifier(state));
                }
            }

            consume(state);

            if (node.return_.kind == Macro_Expression) {
                Expression_Node* node = malloc(sizeof(Expression_Node));
                *node = parse_expression(state);
                variant.data.kind.kind = Macro_Expression;
                variant.data.data.expression = node;
            } else {
                assert(false);
            }

            array_macro_variant_append(&node.variants, variant);
        }

        consume(state);

        result.kind = Item_Macro;
        result.data.macro = node;
    } else if (strcmp(keyword, "type") == 0) {
        Type_Node node;

        char* name = consume_identifier(state);
        result.name = name;

        consume_check(state, Token_Colon);

        node.type = parse_type(state);

        result.kind = Item_Type;
        result.data.type = node;
    } else if (strcmp(keyword, "global") == 0) {
        Global_Node node;

        result.name = consume_identifier(state);
        consume_check(state, Token_Colon);

        node.type = parse_type(state);

        result.kind = Item_Global;
        result.data.global = node;
    } else if (strcmp(keyword, "const") == 0) {
        Constant_Node node;

        result.name = consume_identifier(state);
        consume_check(state, Token_Colon);

        Expression_Node expression = parse_expression(state);
        assert(expression.kind == Expression_Number);

        node.expression = expression.data.number;

        result.kind = Item_Constant;
        result.data.constant = node;
    } else if (strcmp(keyword, "use") == 0) {
        Use_Node node = {};

        char* path = consume_string(state);
        if (string_contains(path, ':')) {
            int index = string_index(path, ':');
            node.package = string_substring(path, 0, index);
            node.path = string_substring(path + 1, index, strlen(path));
        } else {
            node.path = path;
        }

        result.name = "";

        result.kind = Item_Use;
        result.data.use = node;
    } else {
        print_token_error_stub(&state->tokens->elements[state->index - 1]);
        printf("Unexpected token ");
        print_token(&state->tokens->elements[state->index - 1], false);
        printf("\n");
        exit(1);
    }

    return result;
}

static size_t file_id;

File_Node parse(char* path, Tokens* tokens) {
    File_Node result;
    result.path = path;
    result.id = file_id++;

    Parser_State state = { .tokens = tokens, .directives = array_directive_new(4), .index = 0 };

    Array_Item_Node array = array_item_node_new(32);
    while (state.index < tokens->count) {
        Item_Node node = parse_item(&state);
        array_item_node_append(&array, node);
    }

    result.items = array;
    return result;
}
