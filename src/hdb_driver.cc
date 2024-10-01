#include "hdb_driver.h"
#include "hdb_parser.h"

hdb_driver::hdb_driver()
    : trace_parsing(false), trace_scanning(false)
{
}

int hdb_driver::parse(FILE *file)
{
    // location.initialize();
    scan_begin(file);
    yy::parser parse(*this);
    parse.set_debug_level(trace_parsing);
    int res = parse();
    scan_end();
    return res;
}
