#ifndef HDB_OBJECTS_H
#define HDB_OBJECTS_H

#include <stdint.h>
#include <string>
#include <list>

#include "harmonydb.h"

using namespace std;

struct HdbObject;

struct HdbHint {
    string name, value;
};

struct HdbPath : list<string> {
    string toString() {
        string r;

        for (auto it : *this)
            r += it;
        return r;
    }
};

struct HdbSymbolReference {
    bool absolute;
    HdbPath *path;
    virtual ~HdbSymbolReference();
};

struct HdbSetElement {
    virtual ~HdbSetElement() {}
};

struct HdbSetElementReference : HdbSetElement {
    HdbSymbolReference *reference;
    virtual ~HdbSetElementReference();
};

struct HdbSetElementNumber : HdbSetElement {
    int64_t value;
};

struct HarmonyObject;

struct HdbObject {
    HdbObject *parent;
    std::list<HdbObject *> children;
    std::list<HdbHint> hints;
    string label;
    bool temporary;
    HarmonyObject *harmony_object;

    HdbObject(): parent(NULL) {}
    virtual ~HdbObject();
};

struct HdbObjectText : HdbObject {
    string text;
};

struct HdbObjectReference : HdbObject {
    HdbSymbolReference *reference;
    virtual ~HdbObjectReference();
};

struct HdbObjectElement : HdbObject {
    bool relative;
    HdbSymbolReference *set;
    int64_t value;
    virtual ~HdbObjectElement();
};

struct HdbObjectType : HdbObject {
    bool complex;
    int64_t lower, higher;
};

struct HdbObjectCode : HdbObject {
    HarmonyObject::Type code;
};

struct HdbObjectProxy : HdbObject {
    HdbObject *reference;
    virtual ~HdbObjectProxy();
};

struct HdbObjectRelation : HdbObject {
    HarmonyRelation *harmony_relation;
    HdbSymbolReference *relation, *source, *destination, *owner;
    virtual ~HdbObjectRelation();
};

HdbObject *parseHdb(FILE *file);
HarmonyObject * getObjectByReference(HarmonyDB *db, HdbSymbolReference *reference, HdbObject *start);

#endif
