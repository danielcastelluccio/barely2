#ifndef FILE_TOKENIZERH
#define FILE_TOKENIZERH

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "dynamic_array.h"

typedef enum {
    Token_Unknown, // 0 case is invalid
    Token_LeftParenthesis,
    Token_RightParenthesis,
    Token_Colon,
    Token_DoubleColon,
    Token_Semicolon,
    Token_Comma,
    Token_Period,
    Token_Exclamation,
    Token_Equals,
    Token_DoubleEquals,
    Token_ExclamationEquals,
    Token_LessThan,
    Token_GreaterThan,
    Token_LessThanEqual,
    Token_GreaterThanEqual,
    Token_Plus,
    Token_Minus,
    Token_Asterisk,
    Token_Slash,
    Token_Percent,
    Token_Ampersand,
    Token_LeftCurlyBrace,
    Token_RightCurlyBrace,
    Token_LeftBracket,
    Token_RightBracket,
    Token_QuestionMark,
    Token_DoubleQuestionMark,
    Token_NewLine,
    Token_Space,
    Token_Identifier,
    Token_Keyword,
    Token_Number,
    Token_String,
    Token_Boolean,
} Token_Kind;

typedef struct {
    char* file;
    size_t row;
    size_t col;
} Location;

typedef struct {
    Token_Kind kind;
    char* data;
    Location location;
} Token;

Dynamic_Array_Def(Token, Tokens, tokens_)

Tokens tokenize(char* file, char* contents);
void print_token(Token* token, bool newline);

#endif
