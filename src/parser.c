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

char* consume_char(Parser_State* state) {
    Token* current = &state->tokens->elements[state->index];
    if (current->kind != Token_Char) {
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

Ast_Type parse_type(Parser_State* state);
Ast_Item parse_item(Parser_State* state);
Ast_Expression parse_expression(Parser_State* state);

void parse_directives(Parser_State* state) {
    while (peek(state) == Token_Identifier && state->tokens->elements[state->index].data[0] == '#') {
        Ast_Directive directive;
        char* directive_string = consume_identifier(state);

        if (strcmp(directive_string, "#if") == 0) {
            Ast_Directive_If if_node;
            consume_check(state, Token_LeftParenthesis);

            Ast_Expression* expression = malloc(sizeof(Ast_Expression));
            *expression = parse_expression(state);
            if_node.expression = expression;

            consume_check(state, Token_RightParenthesis);

            directive.kind = Directive_If;
            directive.data.if_ = if_node;
        }

        array_ast_directive_append(&state->directives, directive);
    }
}

void filter_add_directive(Parser_State* state, Array_Ast_Directive* directives, Directive_Kind kind) {
    size_t i = 0;
    while (i < state->directives.count) {
        if (state->directives.elements[i].kind == kind) {
            array_ast_directive_append(directives, state->directives.elements[i]);
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

Ast_Macro_Syntax_Kind parse_macro_kind(Parser_State* state, Ast_Macro_Syntax_Kind default_kind) {
    Ast_Macro_Syntax_Kind result = default_kind;
    if (peek(state) == Token_Identifier) {
        char* name = state->tokens->elements[state->index].data;
        if (strcmp(name, "$expr") == 0) {
            result = Macro_Expression;
            consume(state);
        } else if (strcmp(name, "$type") == 0) {
            result = Macro_Type;
            consume(state);
        }
    }
    return result;
}

Ast_Macro_Argument parse_macro_argument(Parser_State* state, Ast_Macro_Syntax_Kind default_kind) {
    Ast_Macro_Argument result = (Ast_Macro_Argument) { .kind = parse_macro_kind(state, default_kind) };

    assert(result.kind != Macro_None);

    if (peek(state) == Token_DoublePeriod) {
        consume(state);
        result.multiple = true;
    }
    return result;
}

Ast_Macro_Syntax_Data parse_macro_syntax_data_inner(Parser_State* state, Ast_Macro_Syntax_Kind kind) {
    Ast_Macro_Syntax_Data result;

    switch (kind) {
        case Macro_Expression: {
            Ast_Expression* expression = malloc(sizeof(Ast_Expression));
            *expression = parse_expression(state);

            result.kind = Macro_Expression;
            result.data.expression = expression;
            break;
        }
        case Macro_Type: {
            Ast_Type* type = malloc(sizeof(Ast_Type));
            *type = parse_type(state);

            result.kind = Macro_Type;
            result.data.type = type;
            break;
        }
        default:
            assert(false);
    }

    return result;
}

Ast_Macro_Syntax_Data parse_macro_syntax_data(Parser_State* state, Ast_Macro_Syntax_Kind default_kind) {
    Ast_Macro_Syntax_Kind kind = parse_macro_kind(state, default_kind);
    return parse_macro_syntax_data_inner(state, kind);
}

Ast_RunMacro parse_run_macro(Parser_State* state, Ast_Identifier identifier, Ast_Macro_Syntax_Kind default_kind) {
    Ast_RunMacro run_macro = { .arguments = array_ast_macro_syntax_data_new(2) };
    run_macro.identifier = identifier;
    run_macro.location = state->tokens->elements[state->index].location;

    consume(state);
    consume_check(state, Token_LeftParenthesis);

    while (peek(state) != Token_RightParenthesis) {
        if (peek(state) == Token_Comma) {
            consume(state);
            continue;
        }

        Ast_Macro_Syntax_Data* data = malloc(sizeof(Ast_Macro_Syntax_Data));;
        *data = parse_macro_syntax_data(state, default_kind);

        array_ast_macro_syntax_data_append(&run_macro.arguments, data);
    }

    consume(state);

    return run_macro;
}

Ast_Type parse_type(Parser_State* state) {
    Ast_Type result = { .directives = array_ast_directive_new(1) };

    parse_directives(state);

    if (peek(state) == Token_Asterisk) {
        Ast_Type_Pointer pointer;

        consume(state);

        Ast_Type child = parse_type(state);
        Ast_Type* child_allocated = malloc(sizeof(Ast_Type));
        *child_allocated = child;
        pointer.child = child_allocated;

        result.kind = Type_Pointer;
        result.data.pointer = pointer;
    } else if (peek(state) == Token_LeftBracket) {
        consume(state);
        
        Ast_Type_Array array = {};

        if (peek(state) != Token_RightBracket) {
            array.has_size = true;
            Ast_Type* size_type = malloc(sizeof(Ast_Type));
            *size_type = parse_type(state);
            array.size_type = size_type;
        }

        consume_check(state, Token_RightBracket);

        Ast_Type child = parse_type(state);
        Ast_Type* child_allocated = malloc(sizeof(Ast_Type));
        *child_allocated = child;
        array.element_type = child_allocated;

        result.kind = Type_Array;
        result.data.array = array;
    } else if (peek(state) == Token_Keyword) {
        char* keyword = consume_keyword(state);

        if (strcmp(keyword, "proc") == 0) {
            consume(state);

            Ast_Type_Procedure procedure = {
                .arguments = array_ast_type_new(2),
                .returns = array_ast_type_new(1),
            };

            while (peek(state) != Token_RightParenthesis) {
                if (peek(state) == Token_Comma) {
                    consume(state);
                    continue;
                }

                Ast_Type* type = malloc(sizeof(Ast_Type));
                *type = parse_type(state);
                array_ast_type_append(&procedure.arguments, type);
            }

            consume(state);

            if (peek(state) == Token_Colon) {
                consume(state);

                bool looking_at_returns = true;
                while (looking_at_returns) {
                    Ast_Type* type = malloc(sizeof(Ast_Type));
                    *type = parse_type(state);
                    array_ast_type_append(&procedure.returns, type);

                    looking_at_returns = peek(state) == Token_Comma;
                }
            }

            result.kind = Type_Procedure;
            result.data.procedure = procedure;
        } else if (strcmp(keyword, "struct") == 0 || strcmp(keyword, "union") == 0) {
            bool is_struct = strcmp(keyword, "struct") == 0;
            consume(state);

            Array_Ast_Declaration_Pointer items = array_ast_declaration_pointer_new(4);
            while (peek(state) != Token_RightCurlyBrace) {
                if (peek(state) == Token_Comma) {
                    consume(state);
                    continue;
                }

                Location location = state->tokens->elements[state->index].location;
                char* name = consume_identifier(state);
                consume_check(state, Token_Colon);
                Ast_Type type = parse_type(state);

                Ast_Declaration* declaration = malloc(sizeof(Ast_Declaration));
                declaration->name = name;
                declaration->type = type;
                declaration->location = location;
                array_ast_declaration_pointer_append(&items, declaration);
            }
            consume(state);

            if (is_struct) {
                Ast_Type_Struct struct_type;
                struct_type.items = items;
                result.kind = Type_Struct;
                result.data.struct_ = struct_type;
            } else {
                Ast_Type_Union union_type;
                union_type.items = items;
                result.kind = Type_Union;
                result.data.union_ = union_type;
            }
        } else if (strcmp(keyword, "enum") == 0) {
            Ast_Type_Enum enum_type;
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
        Ast_Type_Number number_type;
        char* value_string = consume_number(state);
        number_type.value = atoll(value_string);

        result.kind = Type_Number;
        result.data.number = number_type;
    } else {
        char* name = consume_identifier(state);
        Ast_Type_Internal internal;
        bool found = false;

        if (!found) {
            if (strcmp(name, "@typeof") == 0) {
                Ast_Type_TypeOf type_of;

                consume_check(state, Token_LeftParenthesis);

                Ast_Expression* expression = malloc(sizeof(Ast_Expression));
                *expression = parse_expression(state);
                type_of.expression = expression;

                consume_check(state, Token_RightParenthesis);

                result.kind = Type_TypeOf;
                result.data.type_of = type_of;
                found = true;
            }
        }

        if (!found) {
            if (strcmp(name, "uint") == 0) {
                internal = Type_UInt;
                found = true;
            } else if (strcmp(name, "uint64") == 0) {
                internal = Type_UInt64;
                found = true;
            } else if (strcmp(name, "uint32") == 0) {
                internal = Type_UInt32;
                found = true;
            } else if (strcmp(name, "uint16") == 0) {
                internal = Type_UInt16;
                found = true;
            } else if (strcmp(name, "uint8") == 0) {
                internal = Type_UInt8;
                found = true;
            } else if (strcmp(name, "float64") == 0) {
                internal = Type_Float64;
                found = true;
            } else if (strcmp(name, "byte") == 0) {
                internal = Type_Byte;
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
            Ast_Type_Basic basic = {};

            basic.identifier.name = name;

            result.kind = Type_Basic;
            result.data.basic = basic;

            if (peek(state) == Token_Exclamation) {
                assert(result.kind == Type_Basic);
                Ast_RunMacro type = parse_run_macro(state, result.data.basic.identifier, Macro_Type);

                result.kind = Type_RunMacro;
                result.data.run_macro = type;
            }
        }
    }

    return result;
}

Ast_Expression parse_multiple_expression(Parser_State* state) {
    Ast_Expression result;
    Ast_Expression_Multiple node = { .expressions = array_ast_expression_new(2) };
    do {
        if (peek(state) == Token_Comma) {
            consume(state);
        }

        Ast_Expression* expression = malloc(sizeof(Ast_Expression));
        *expression = parse_expression(state);
        array_ast_expression_append(&node.expressions, expression);
    } while (peek(state) == Token_Comma);

    result.kind = Expression_Multiple;
    result.data.multiple = node;
    return result;
};

Ast_Statement parse_statement(Parser_State* state) {
    Ast_Statement result = { .directives = array_ast_directive_new(1) };

    parse_directives(state);
    filter_add_directive(state, &result.directives, Directive_If);

    Token_Kind token = peek(state);
    if (token == Token_Keyword && strcmp(state->tokens->elements[state->index].data, "var") == 0) {
        Ast_Statement_Declare node = {};

        consume(state);

        Array_Ast_Declaration declarations = array_ast_declaration_new(8);
        do {
            if (peek(state) == Token_Comma) {
                consume(state);
                continue;
            }
            Ast_Declaration declaration;
            declaration.location = state->tokens->elements[state->index].location;
            declaration.name = consume_identifier(state);
            consume_check(state, Token_Colon);
            declaration.type = parse_type(state);
            array_ast_declaration_append(&declarations, declaration);
        } while(peek(state) == Token_Comma);

        node.declarations = declarations;

        Token_Kind next = peek(state);
        if (next == Token_Equals) {
            consume(state);

            Ast_Expression* expression = malloc(sizeof(Ast_Expression));
            *expression = parse_multiple_expression(state);
            node.expression = expression;

            consume_check(state, Token_Semicolon);
        } else {
            consume_check(state, Token_Semicolon);
        }

        result.kind = Statement_Declare;
        result.data.declare = node;
    } else if (token == Token_Keyword && strcmp(state->tokens->elements[state->index].data, "return") == 0) {
        Ast_Statement_Return node;
        node.location = state->tokens->elements[state->index].location;

        consume(state);

        Ast_Expression expression = parse_expression(state);
        Ast_Expression* expression_allocated = malloc(sizeof(Ast_Expression));
        *expression_allocated = expression;
        node.expression = expression_allocated;

        consume_check(state, Token_Semicolon);

        result.kind = Statement_Return;
        result.data.return_ = node;
    } else {
        Ast_Statement_Expression node;

        Ast_Expression* expression = malloc(sizeof(Ast_Expression));
        *expression = parse_multiple_expression(state);
        node.expression = expression;

        Token_Kind token = peek(state);
        if (token == Token_Equals) {
            consume(state);

            Ast_Statement_Assign assign = {};
            assign.parts = array_statement_assign_part_new(2);

            if (expression->kind == Expression_Retrieve) {
                Statement_Assign_Part assign_part;
                assign_part = expression->data.retrieve;
                array_statement_assign_part_append(&assign.parts, assign_part);
            } else if (expression->kind == Expression_Multiple) {
                Ast_Expression_Multiple* multiple = &expression->data.multiple;
                for (size_t i = 0; i < multiple->expressions.count; i++) {
                    Ast_Expression* expression = multiple->expressions.elements[i];
                    Statement_Assign_Part assign_part;
                    assign_part = expression->data.retrieve;
                    array_statement_assign_part_append(&assign.parts, assign_part);
                }
            }

            Ast_Expression* expression = malloc(sizeof(Ast_Expression));
            *expression = parse_multiple_expression(state);
            assign.expression = expression;

            result.kind = Statement_Assign;
            result.data.assign = assign;

            consume_check(state, Token_Semicolon);
        } else {
            result.statement_end_location = state->tokens->elements[state->index - 1].location;
            result.kind = Statement_Expression;
            result.data.expression = node;
            consume_check(state, Token_Semicolon);
        }
    }

    return result;
}

int get_precedence(Parser_State* state) {
    Token_Kind kind = peek(state);

    if (kind == Token_Plus ||
            kind == Token_Minus) {
        return 3;
    }
    if (kind == Token_Asterisk ||
            kind == Token_Slash ||
            kind == Token_Percent) {
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

Ast_Expression parse_expression_without_operators(Parser_State* state) {
    Ast_Expression result = { .directives = array_ast_directive_new(1) };

    parse_directives(state);

    switch (peek(state)) {
        case Token_LeftCurlyBrace: {
            Ast_Expression_Block node;
            consume(state);

            Array_Ast_Statement statements = array_ast_statement_new(32);

            while (peek(state) != Token_RightCurlyBrace) {
                Ast_Statement* statement = malloc(sizeof(Ast_Statement));
                *statement = parse_statement(state);
                array_ast_statement_append(&statements, statement);
            }
            node.statements = statements;
            consume(state);

            result.kind = Expression_Block;
            result.data.block = node;
            break;
        }
        case Token_Number: {
            Ast_Expression_Number node = {};

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
            Ast_Expression_String node;

            node.value = consume_string(state);

            result.kind = Expression_String;
            result.data.string = node;
            break;
        }
        case Token_Char: {
            Ast_Expression_Char node;

            node.value = consume_char(state)[0];

            result.kind = Expression_Char;
            result.data.char_ = node;
            break;
        }
        case Token_Boolean: {
            Ast_Expression_Boolean node;

            node.value = strcmp(consume_boolean(state), "true") == 0;

            result.kind = Expression_Boolean;
            result.data.boolean = node;
            break;
        }
        case Token_Null: {
            result.kind = Expression_Null;
            consume(state);
            break;
        }
        case Token_Identifier: {
            Location location = state->tokens->elements[state->index].location;
            char* name = consume_identifier(state);
            if (strcmp(name, "@sizeof") == 0) {
                Ast_Expression_SizeOf node;

                consume_check(state, Token_LeftParenthesis);

                Ast_Type type = parse_type(state);
                node.type = type;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_SizeOf;
                result.data.size_of = node;
            } else if (strcmp(name, "@lengthof") == 0) {
                Ast_Expression_LengthOf node;

                consume_check(state, Token_LeftParenthesis);

                Ast_Type type = parse_type(state);
                node.type = type;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_LengthOf;
                result.data.length_of = node;
            } else if (strcmp(name, "@istype") == 0) {
                Ast_Expression_IsType node;

                consume_check(state, Token_LeftParenthesis);

                node.wanted = parse_type(state);

                consume_check(state, Token_Comma);

                node.given = parse_type(state);

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_IsType;
                result.data.is_type = node;
            } else if (strcmp(name, "@cast") == 0) {
                Ast_Expression_Cast node;
                node.location = location;

                consume_check(state, Token_LeftParenthesis);

                Ast_Type type = parse_type(state);
                node.type = type;

                consume_check(state, Token_Comma);

                Ast_Expression inner = parse_expression(state);
                Ast_Expression* inner_allocated = malloc(sizeof(Ast_Expression));
                *inner_allocated = inner;
                node.expression = inner_allocated;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_Cast;
                result.data.cast = node;
                break;
            } else if (strcmp(name, "@init") == 0) {
                Ast_Expression_Init node;
                node.location = location;

                consume_check(state, Token_LeftParenthesis);

                Ast_Type type = parse_type(state);
                node.type = type;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_Init;
                result.data.init = node;
                break;
            } else if (strcmp(name, "@build") == 0) {
                Ast_Expression_Build node;
                node.location = location;

                consume_check(state, Token_LeftParenthesis);

                Ast_Type type = parse_type(state);
                node.type = type;

                Array_Ast_Expression arguments = array_ast_expression_new(32);

                while (peek(state) != Token_RightParenthesis) {
                    if (peek(state) == Token_Comma) {
                        consume(state);
                        continue;
                    }

                    Ast_Expression* expression = malloc(sizeof(Ast_Expression));
                    *expression = parse_expression(state);

                    array_ast_expression_append(&arguments, expression);
                }

                node.arguments = arguments;

                consume_check(state, Token_RightParenthesis);

                result.kind = Expression_Build;
                result.data.build = node;
                break;
            } else {
                Ast_Expression_Retrieve node = {};
                node.location = location;
                node.kind = Retrieve_Assign_Identifier;
                Ast_Identifier identifier = {};
                identifier.name = name;

                node.kind = Retrieve_Assign_Identifier;
                node.data.identifier = identifier;
                result.kind = Expression_Retrieve;
                result.data.retrieve = node;
            }
            break;
        }
        case Token_Exclamation: {
            Ast_Expression_Invoke node = { .arguments = array_ast_expression_new(1) };
            node.kind = Invoke_Operator;
            node.location = state->tokens->elements[state->index].location;

            node.data.operator_.operator_ = Operator_Not;

            consume(state);

            Ast_Expression* expression = malloc(sizeof(Ast_Expression));
            *expression = parse_expression_without_operators(state);
            array_ast_expression_append(&node.arguments, expression);

            result.kind = Expression_Invoke;
            result.data.invoke = node;
            break;
        }
        case Token_Keyword: {
            char* name = consume_keyword(state);

            if (strcmp(name, "if") == 0) {
                Ast_Expression_If node = {};
                node.location = state->tokens->elements[state->index].location;

                Ast_Expression condition = parse_expression(state);
                Ast_Expression* condition_allocated = malloc(sizeof(Ast_Expression));
                *condition_allocated = condition;
                node.condition = condition_allocated;

                Ast_Expression* expression = malloc(sizeof(Ast_Expression));
                *expression = parse_expression(state);
                node.if_expression = expression;

                if (state->tokens->elements[state->index].kind == Token_Keyword && strcmp(state->tokens->elements[state->index].data, "else") == 0) {
                    consume(state);
                    Ast_Expression* expression = malloc(sizeof(Ast_Expression));
                    *expression = parse_expression(state);
                    node.else_expression = expression;
                }

                result.kind = Expression_If;
                result.data.if_ = node;
            } else if (strcmp(name, "while") == 0) {
                Ast_Expression_While node;

                Ast_Expression condition = parse_expression(state);
                Ast_Expression* condition_allocated = malloc(sizeof(Ast_Expression));
                *condition_allocated = condition;
                node.condition = condition_allocated;

                Ast_Expression inside = parse_expression(state);
                Ast_Expression* inside_allocated = malloc(sizeof(Ast_Expression));
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
            Ast_Expression_Reference node;

            consume(state);
            
            Ast_Expression inner = parse_expression(state);
            Ast_Expression* inner_allocated = malloc(sizeof(Ast_Expression));
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
            Ast_Expression_Retrieve node = {};
            node.kind = Retrieve_Assign_Parent;

            consume(state);

            Ast_Expression* previous_result = malloc(sizeof(Ast_Expression));
            *previous_result = result;
            node.data.parent.expression = previous_result;

            if (peek(state) == Token_Asterisk) {
                consume(state);
                node.data.parent.name = "*";
            } else {
                node.data.parent.name = consume_identifier(state);
            }

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            continue;
        }

        if (peek(state) == Token_LeftBracket) {
            running = true;
            Ast_Expression_Retrieve node;
            node.kind = Retrieve_Assign_Array;
            node.location = state->tokens->elements[state->index].location;

            Ast_Expression* previous_result = malloc(sizeof(Ast_Expression));
            *previous_result = result;
            node.data.array.expression_outer = previous_result;

            consume(state);

            Ast_Expression inner = parse_expression(state);
            Ast_Expression* inner_allocated = malloc(sizeof(Ast_Expression));
            *inner_allocated = inner;
            node.data.array.expression_inner = inner_allocated;

            consume_check(state, Token_RightBracket);

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            continue;
        }

        if (peek(state) == Token_LeftParenthesis) {
            running = true;
            Ast_Expression_Invoke node;
            node.location = state->tokens->elements[state->index].location;
            node.kind = Invoke_Standard;

            Ast_Expression* previous_result = malloc(sizeof(Ast_Expression));
            *previous_result = result;
            node.data.procedure = previous_result;
            consume(state);

            Array_Ast_Expression arguments = array_ast_expression_new(32);

            while (peek(state) != Token_RightParenthesis) {
                if (peek(state) == Token_Comma) {
                    consume(state);
                    continue;
                }

                Ast_Expression* expression = malloc(sizeof(Ast_Expression));
                *expression = parse_expression(state);

                array_ast_expression_append(&arguments, expression);
            }

            node.arguments = arguments;

            consume(state);

            result.kind = Expression_Invoke;
            result.data.invoke = node;
            continue;
        }

        if (peek(state) == Token_Exclamation) {
            running = true;
            assert(result.kind == Expression_Retrieve && result.data.retrieve.kind == Retrieve_Assign_Identifier);
            Ast_RunMacro node = parse_run_macro(state, result.data.retrieve.data.identifier, Macro_Expression);

            result.kind = Expression_RunMacro;
            result.data.run_macro = node;
            continue;
        }
    }

    return result;
}

Ast_Expression parse_expression(Parser_State* state) {
    Ast_Expression result = parse_expression_without_operators(state);

    if (get_precedence(state) > 0) {
        int previous_precedence = INT_MAX;
        while (get_precedence(state) > 0) {
            int current_precedence = get_precedence(state);

            Ast_Expression_Invoke node;
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

            Ast_Expression* parsed = malloc(sizeof(Ast_Expression));
            *parsed = parse_expression_without_operators(state);

            if (current_precedence > previous_precedence) {
                Ast_Expression* result_inner = malloc(sizeof(Ast_Expression));

                Array_Ast_Expression arguments = array_ast_expression_new(32);
                array_ast_expression_append(&arguments, array_ast_expression_get(&result.data.invoke.arguments, 1));
                array_ast_expression_append(&arguments, parsed);
                node.arguments = arguments;

                result_inner->kind = Expression_Invoke;
                result_inner->data.invoke = node;

                array_ast_expression_set(&result.data.invoke.arguments, 1, result_inner);
            } else {
                Ast_Expression* result_allocated = malloc(sizeof(Ast_Expression));
                *result_allocated = result;

                Array_Ast_Expression arguments = array_ast_expression_new(32);
                array_ast_expression_append(&arguments, result_allocated);
                array_ast_expression_append(&arguments, parsed);
                node.arguments = arguments;

                result.kind = Expression_Invoke;
                result.data.invoke = node;
            }

            previous_precedence = current_precedence;
        }
    }

    return result;
}

Ast_Item parse_item(Parser_State* state) {
    Ast_Item result = { .directives = array_ast_directive_new(1) };

    parse_directives(state);
    filter_add_directive(state, &result.directives, Directive_If);

    switch (peek(state)) {
        case Token_Keyword: {
            char* keyword = consume_keyword(state);
            if (strcmp(keyword, "proc") == 0) {
                Ast_Item_Procedure node;

                node.name = consume_identifier(state);

                consume_check(state, Token_LeftParenthesis);

                Array_Ast_Declaration arguments = array_ast_declaration_new(4);
                while (peek(state) != Token_RightParenthesis) {
                    if (peek(state) == Token_Comma) {
                        consume(state);
                        continue;
                    }

                    Location location = state->tokens->elements[state->index].location;
                    char* name = consume_identifier(state);
                    consume_check(state, Token_Colon);
                    Ast_Type type = parse_type(state);
                    array_ast_declaration_append(&arguments, (Ast_Declaration) { name, type, location });
                }
                node.arguments = arguments;
                consume_check(state, Token_RightParenthesis);

                node.returns = array_ast_type_new(1);
                if (peek(state) == Token_Colon) {
                    consume(state);

                    bool running = true;
                    while (running) {
                        Ast_Type type = parse_type(state);
                        Ast_Type* type_allocated = malloc(sizeof(Ast_Type));
                        *type_allocated = type;

                        array_ast_type_append(&node.returns, type_allocated);
                        running = false;
                        if (peek(state) == Token_Comma) {
                            running = true;
                            consume(state);
                        }
                    }
                }

                Ast_Expression body = parse_expression(state);
                Ast_Expression* body_allocated = malloc(sizeof(Ast_Expression));
                *body_allocated = body;
                node.body = body_allocated;

                result.kind = Item_Procedure;
                result.data.procedure = node;
            } else if (strcmp(keyword, "macro") == 0) {
                Ast_Item_Macro node = { .arguments = array_ast_macro_syntax_kind_new(2), .variants = array_ast_macro_variant_new(2) };

                node.name = consume_identifier(state);

                consume_check(state, Token_Exclamation);
                consume_check(state, Token_LeftParenthesis);

                while (peek(state) != Token_RightParenthesis) {
                    if (peek(state) == Token_Comma) {
                        consume(state);
                        continue;
                    }

                    Ast_Macro_Argument argument = parse_macro_argument(state, Macro_None);
                    array_ast_macro_syntax_kind_append(&node.arguments, argument);
                }

                consume(state);
                consume_check(state, Token_Colon);

                node.return_ = parse_macro_argument(state, Macro_None);

                consume_check(state, Token_LeftCurlyBrace);

                while (peek(state) != Token_RightCurlyBrace) {
                    if (peek(state) == Token_Comma) {
                        consume(state);
                    }

                    Ast_Macro_Variant variant = { .bindings = array_string_new(2) };
                    consume_check(state, Token_LeftParenthesis);

                    while (peek(state) != Token_RightParenthesis) {
                        if (peek(state) == Token_Comma) {
                            consume(state);
                            continue;
                        }

                        array_string_append(&variant.bindings, consume_identifier(state));
                        if (peek(state) == Token_DoublePeriod) {
                            variant.varargs = true;
                            consume(state);
                        }
                    }

                    consume(state);

                    if (node.return_.kind == Macro_Expression) {
                        Ast_Expression* node = malloc(sizeof(Ast_Expression));
                        *node = parse_expression(state);
                        variant.data.kind = Macro_Expression;
                        variant.data.data.expression = node;
                    } else if (node.return_.kind == Macro_Type) {
                        Ast_Type* type = malloc(sizeof(Ast_Type));
                        *type = parse_type(state);
                        variant.data.kind = Macro_Type;
                        variant.data.data.type = type;
                    } else {
                        assert(false);
                    }

                    array_ast_macro_variant_append(&node.variants, variant);
                }

                consume(state);

                result.kind = Item_Macro;
                result.data.macro = node;
            } else if (strcmp(keyword, "type") == 0) {
                Ast_Item_Type node = {};

                node.name = consume_identifier(state);

                consume_check(state, Token_Colon);

                node.type = parse_type(state);

                result.kind = Item_Type;
                result.data.type = node;
            } else if (strcmp(keyword, "global") == 0) {
                Ast_Item_Global node = {};

                node.name = consume_identifier(state);
                consume_check(state, Token_Colon);

                node.type = parse_type(state);

                result.kind = Item_Global;
                result.data.global = node;
            } else if (strcmp(keyword, "const") == 0) {
                Ast_Item_Constant node = {};

                node.name = consume_identifier(state);
                consume_check(state, Token_Colon);

                Ast_Expression expression = parse_expression(state);
                assert(expression.kind == Expression_Number);

                node.expression = expression.data.number;

                result.kind = Item_Constant;
                result.data.constant = node;
            } else {
                print_token_error_stub(&state->tokens->elements[state->index - 1]);
                printf("Unexpected token ");
                print_token(&state->tokens->elements[state->index - 1], false);
                printf("\n");
                exit(1);
            }
            break;
        }
        default:
            assert(false);
    }

    return result;
}

static size_t file_id;

Ast_File parse(char* path, Tokens* tokens) {
    Ast_File result;
    result.path = path;
    result.id = file_id++;

    Parser_State state = { .tokens = tokens, .directives = array_ast_directive_new(4), .index = 0 };

    Array_Ast_Item array = array_ast_item_new(32);
    while (state.index < tokens->count) {
        Ast_Item node = parse_item(&state);
        array_ast_item_append(&array, node);
    }

    result.items = array;
    return result;
}
