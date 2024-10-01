%{
#include <stdio.h>
#include <string>
#include "hdb_driver.h"
#include "hdb_parser.h"

%}

%option noyywrap nounput noinput batch debug

%{
    #define YY_USER_ACTION loc.columns (yyleng);
%}

%%

%{
    yy::location &loc = drv.location;
    loc.step();
%}

\/\/[^\n]*      loc.step(); // ignore comments
[ \t]+          loc.step(); // ignore all whitespace
\n+             loc.lines(yyleng); loc.step();
-?[0-9]+\.[0-9]+    return yy::parser::make_FLOAT(stof(yytext), loc);
-?[0-9]+        return yy::parser::make_INT(stol(yytext), loc);
"["             return yy::parser::make_LEFT_SQUARE(loc);
"]"             return yy::parser::make_RIGHT_SQUARE(loc);
"<"             return yy::parser::make_LEFT_TRIANGLE(loc);
">"             return yy::parser::make_RIGHT_TRIANGLE(loc);
\.[a-zA-Z_][a-zA-Z0-9_]* return yy::parser::make_DOTSYMBOL(string(yytext + 1, strlen(yytext) - 1), loc);
"."             return yy::parser::make_DOT(loc);
","             return yy::parser::make_COMMA(loc);
":"             return yy::parser::make_COLON(loc);
"~"             return yy::parser::make_TILDE(loc);
"!"             return yy::parser::make_EXCLAMATION(loc);
"@"             return yy::parser::make_AT(loc);
"#"             return yy::parser::make_HASH(loc);
"$"             return yy::parser::make_DOLLAR(loc);
"%"             return yy::parser::make_PERCENT(loc);
"^"             return yy::parser::make_APOSTROPHE(loc);
"&"             return yy::parser::make_AMPERSAND(loc);
"*"             return yy::parser::make_ASTERISK(loc);
"("             return yy::parser::make_LEFT_ROUND(loc);
")"             return yy::parser::make_RIGHT_ROUND(loc);
"?"             return yy::parser::make_QUESTION(loc);
"_"             return yy::parser::make_UNDERSCORE(loc);
"="             return yy::parser::make_EQUAL(loc);
"+"             return yy::parser::make_PLUS(loc);
"-"             return yy::parser::make_MINUS(loc);
\"[^\"]*\"      return yy::parser::make_TEXT(string(yytext + 1, strlen(yytext)- 2), loc);
[a-zA-Z_][a-zA-Z0-9_]* return yy::parser::make_SYMBOL(yytext, loc);

<<EOF>>         return yy::parser::make_YYEOF(loc);

%%

void hdb_driver::scan_begin(FILE *file)
{
    yy_flex_debug = trace_scanning;
    yyin = file;
}

void hdb_driver::scan_end()
{
    fclose(yyin);
}
