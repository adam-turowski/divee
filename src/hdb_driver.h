#ifndef DRIVER_H
#define DRIVER_H

#include <map>
#include <list>
#include <string>
#include "hdb_parser.h"

#define YY_DECL \
     yy::parser::symbol_type yylex (hdb_driver& drv)
YY_DECL;

#include "hdb_objects.h"


class hdb_driver
{
public:
    hdb_driver();
    list<HdbObject *> *root;

    int parse(FILE *file);
    bool trace_parsing;

    void scan_begin(FILE *file);
    void scan_end();
    bool trace_scanning;
    yy::location location;
};

#endif
