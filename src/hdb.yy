%skeleton "lalr1.cc"
%require "3.7.6"
%defines

%define api.token.raw
%define api.token.constructor
%define api.value.type variant
%define parse.assert

%code requires {
#include <string>
class hdb_driver;
#include "hdb_objects.h"
}

%param { hdb_driver &drv }
%locations

%define parse.trace
%define parse.error detailed
%define parse.lac full

%code {
#include "hdb_driver.h"
}

%define api.token.prefix {TOK_}

%token
    LEFT_ROUND "("
    RIGHT_ROUND ")"
    LEFT_SQUARE "["
    RIGHT_SQUARE "]"
    LEFT_CURLY "{"
    RIGHT_CURLY "}"
    LEFT_TRIANGLE "<"
    RIGHT_TRIANGLE ">"
    COLON ":"
    DOT "."
    COMMA ","
    TILDE "~"
    EXCLAMATION "!"
    AT "@"
    HASH "#"
    DOLLAR "$"
    PERCENT "%"
    APOSTROPHE "^"
    AMPERSAND "&"
    ASTERISK "*"
    UNDERSCORE "_"
    QUESTION "?"
    EQUAL "="
    PLUS "+"
    MINUS "-"
;
%token<int64_t> INT
%token<double> FLOAT
%token<string> TEXT NAME SYMBOL DOTSYMBOL
%type<list<HdbObject *> *> final_expression expr_next set set_next
%type<HdbObject *> expression item item_proxy object
%type<HdbSymbolReference *> reference
%type<HdbPath *> absolute_path path_next
%type<list<HdbHint>> hints
%type<HdbHint> hint

%start final_expression

%%

final_expression: expression expr_next {
        $2->push_front($1);
        drv.root = $2;
    }

expr_next: {
        $$ = new list<HdbObject *>;
    }
    | "," expression expr_next {
        $3->push_front($2);
        $$ = $3;
    }

expression: item_proxy hints {
        $1->temporary = false;
        $1->hints = $2;
        $$ = $1;
    }
    | SYMBOL ":" item_proxy hints {
        $3->temporary = false;
        $3->label = $1;
        $3->hints = $4;
        $$ = $3;
    }
    | DOTSYMBOL ":" item_proxy hints {
        $3->temporary = true;
        $3->label = $1;
        $3->hints = $4;
        $$ = $3;
    }

reference: absolute_path {
        auto reference = new HdbSymbolReference();
        reference->path = $1;
        reference->absolute = true;
        $$ = reference;
    }
    | SYMBOL absolute_path {
        auto reference = new HdbSymbolReference();
        $2->push_front($1);
        reference->path = $2;
        reference->absolute = false;
        $$ = reference;
    }
    | DOT {
        auto reference = new HdbSymbolReference();
        reference->path = NULL;
        reference->absolute = true;
        $$ = reference;
    }
    | SYMBOL {
        auto reference = new HdbSymbolReference();
        reference->path = new HdbPath;
        reference->path->push_front($1);
        reference->absolute = false;
        $$ = reference;
    }

absolute_path: DOTSYMBOL path_next {
        $2->push_front($1);
        $$ = $2;
    }

path_next: DOTSYMBOL path_next {
        $2->push_front($1); 
        $$ = $2;
    }
    | {
        auto path = new HdbPath;
        $$ = path;
    }

item_proxy: "$" { // empty proxy
        auto p = new HdbObjectProxy;
        p->reference = NULL;
        $$ = p;
    }
    | "$" item { // proxy
        auto p = new HdbObjectProxy;
        p->reference = $2;
        $$ = p;
    }
    | item { 
        $$ = $1;
    }

item: object {
        $$ = $1;
    }
    | reference {
        auto o = new HdbObjectReference;
        o->reference = $1;
        $$ = o;
    }
    | "_" { // null
        auto o = new HdbObject;
        $$ = o;
    }
    | object set {
        $1->children = *$2;
        for (auto c : $1->children)
            c->parent = $1;
        delete $2;
        $$ = $1;
    }
    | set {
        auto object = new HdbObject;
        object->children = *$1;
        for (auto c : object->children)
            c->parent = object;
        delete $1;
        $$ = object;
    }    
    | "[" reference "," reference "," reference "]" {
        auto r = new HdbObjectRelation;
        r->relation = $2;
        r->source = $4;
        r->destination = $6;
        r->owner = NULL;
        $$ = r;
    }
    | "[" reference "," reference "," reference "," reference "]" {
        auto r = new HdbObjectRelation;
        r->relation = $2;
        r->source = $4;
        r->destination = $6;
        r->owner = $8;
        $$ = r;
    }

hints: {
        std::list<HdbHint> hints;
        $$ = hints;
    }
    | "#" hint hints {
        $3.push_front($2);
        $$ = $3;
    }

hint: TEXT ":" TEXT {
        HdbHint h;
        h.name = $1;
        h.value = $3;
        $$ = h;
    }

set: "(" expression set_next {
        $3->push_front($2);
        $$ = $3;
    }
    | "(" ")" {
        $$ = new list<HdbObject *>;
    }

set_next: RIGHT_ROUND {
        $$ = new list<HdbObject *>;
    }
    | "," expression set_next {
        $3->push_front($2);
        $$ = $3;
    }

object:
    reference "[" INT "]" { // element
        auto o = new HdbObjectElement;
        o->relative = 0;
        o->set = $1;
        o->value = $3;
        $$ = o;
    }
    | "<" INT "," INT ">" { // enum type
        auto o = new HdbObjectType;
        o->complex = false;
        o->lower = $2;
        o->higher = $4;
        $$ = o;
    }
    | "<" ">" { // complex type
        auto o = new HdbObjectType;
        o->complex = true;
        $$ = o;
    }
    | TEXT {
        auto t = new HdbObjectText;
        t->text = $1;
        $$ = t;
    }
// instructions
    | "?" { // match and resolve (pattern, where, unknowns, negatives, cont)
            // match and resolve (pattern, where, unknowns, negatives)
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::MATCH;
        $$ = o;
    }
    | "*" { // create object (where)
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::CREATE;
        $$ = o;
    }
    | "=" { // assign value (dest, src) or (dest) to clear
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::ASSIGN;
        $$ = o;
    }
    | "+" { // add to set (set, element)
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::ADD;
        $$ = o;
    }        // add to set with label (set, element, label)
    | "-" { // remove from set (set, element) or remove relation (set, $relation)
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::REMOVE;
        $$ = o;
    }
    | "!" { // launcher? (args, cont)
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::LAUNCH;
        $$ = o;
    }
    | "<" { // receive (args)  // (args, cont)
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::RECEIVE;
        $$ = o;
    }
    | ">" { // send (receiver, args)
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::SEND;
        $$ = o;
    }
    | "^" { // link (dest, src) or unlink (dest)
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::LINK;
        $$ = o;
    }
    | "~" { // relate (source, destination, relation, owner)
        auto o = new HdbObjectCode;
        o->code = HarmonyObject::Type::RELATE;
        $$ = o;
    }

%%

void yy::parser::error(const location_type &l, const std::string &m)
{
    std::cerr << l << ": " << m << endl;
}
