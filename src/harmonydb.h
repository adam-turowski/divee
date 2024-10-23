#ifndef HARMONYDB_H
#define HARMONYDB_H

#include "common.h"
#include <stdint.h>
#include <string>
#include <list>
#include <map>

using namespace std;

struct HarmonyObject;

struct HarmonyObjectPath : list<string> {
    HarmonyObjectPath & operator=(const list<string> &l);
    bool parse(const string &s);
    const string toString();
    const string toString() const;
};

struct HarmonyObjectReference {
    HarmonyObject *object;
    HarmonyObjectReference *next, *prev;
    bool structural, primary;
    unsigned int structural_references;

    HarmonyObjectReference();
    virtual ~HarmonyObjectReference();
    void setReference(HarmonyObject *object, bool structural = false, bool primary = false);
    void _removeReference();
    void removeReference();
    int countReferences();
    bool isEmpty();

    virtual void dereference() { assert(0); }
};

struct HarmonyItem : HarmonyObjectReference {
    string label;
    struct HarmonyItem *next, *prev;
    struct HarmonyObject *parent;

    HarmonyItem * nextItem(HarmonyObject *set);
    HarmonyItem * previousItem(HarmonyObject *set);
    HarmonyItem * nextRelation(HarmonyObject *set);
    ~HarmonyItem();
};

struct HarmonyRelation;

#define START_SWEEP \
{ \
    HarmonyObject::_old_sweep_mark = HarmonyObject::_current_sweep_mark++; \
    assert(HarmonyObject::_sweeping == false); \
    HarmonyObject::_sweeping = true; \
}
// PF("INCREASE_SWEEP_MARK old:%d  current:%d", HarmonyObject::_old_sweep_mark, HarmonyObject::_current_sweep_mark);

#define FINISH_SWEEP \
{ \
    assert(HarmonyObject::_sweeping == true); \
    HarmonyObject::_sweeping = false; \
}

struct HarmonyObject {
    HarmonyObjectReference reference;
    map<string, string> hints;

    static unsigned _object_count;

    unsigned int root_distance;
    bool has_primary;
    // HarmonyObject root_distance_parent;

    static bool _sweeping;
    static unsigned _current_sweep_mark;
    static unsigned _old_sweep_mark;

    unsigned int sweep_mark;
    unsigned int temporary_label_sweep_mark;
    union {
        HarmonyItem *sweep_parent;
        HarmonyObject *sweep_object;
    };

// executioner specific
    HarmonyObject *context, *parent_receiver;
    unsigned receiver_armed, receiver_got;
    bool unknown, negative, loop;

    HarmonyObjectReference relation, source, destination, pattern_owner;

    HarmonyItem items;
    HarmonyItem relations;

    enum Type {
        NUL = 0,
        ELEMENT,
        TYPE,
        PROXY,
        MATCH,          // ? (pattern, unknowns, negatives, [cont])
        CREATE,         // * (dest - must be proxy)
        ASSIGN,         // = (dest - must be proxy, src)
        ADD,            // + (set, object)
        REMOVE,         // - (set, object)
        LAUNCH,         // ! (arg, body)
        RECEIVE,        // < (arg)
        SEND,           // > (recipient, arg) can be parallelized, () to stop
        LINK,           // ^ (dest, [src]) ?, might be replaced by relation proxy
        RELATE,         // ~
        PATTERN         // pattern relation
    } type;

    HarmonyObjectReference element_type;
    int64_t element_value;              // ELEMENT
    int64_t type_lower, type_higher;    // TYPE
    HarmonyItem proxy;                  // PROXY

    HarmonyObject(Type t = Type::NUL);
    virtual ~HarmonyObject();

    const string getHint(const string &hint);
    bool isNul() {
        return type == Type::NUL;
    }
    bool isElement() {
        return type == Type::ELEMENT;
    }
    bool isType() {
        return type == Type::TYPE;
    }
    bool isProxy() {
        return type == Type::PROXY;
    }
    bool isCode() {
        return type >= Type::MATCH;
    }
    bool isPattern() {
        return type == Type::PATTERN;
    }
    bool isSend() {
        return type == Type::SEND;
    }
    int isEmpty();
    void clear();
    void clearRelations();
    HarmonyItem * first();
    HarmonyItem * last();
    HarmonyItem * next(HarmonyObject *object);
    HarmonyItem * prev(HarmonyObject *object);
    HarmonyItem * add(HarmonyObject *object, string label = string(), bool primary = false);
    HarmonyItem * remove(HarmonyItem *item, bool internal = false);
    HarmonyItem * findItem(const string &label);
    HarmonyItem * findItem(HarmonyObject *object);
    HarmonyItem * findRelation(const string &label);
    virtual void findPath(HarmonyObject *start);
    virtual string getPath(HarmonyObject *start);
    string getKey();
    void addRelation(HarmonyObject *relation, HarmonyObject *source, HarmonyObject *destination);
    void addRelation(HarmonyRelation *r, HarmonyObject *relation, HarmonyObject *source, HarmonyObject *destination);
    HarmonyItem * addRelation(HarmonyRelation *r, string label = string());
    HarmonyItem * removeRelation(HarmonyItem *relation);

    HarmonyObject * getObject();

    void updateDistance(HarmonyObject *start, HarmonyObject * parent_root_distance = 0);
    int isReachable(HarmonyObject *start);
    void ripCycles(unsigned mark);

    void copy(HarmonyObject *source);
    virtual HarmonyObject * clone();
    void link(HarmonyObject *object);
    bool compare(HarmonyObject *object);

    unsigned getItems(unsigned size, HarmonyObject *items[]);
};

struct HarmonyRelation : HarmonyObject {
    string label;
    HarmonyObject *owner;

    HarmonyObject * clone();
};


// Hints
#define HINT_BACKEND "backend"
#define HINT_BACKEND_FILE "file"
#define HINT_FILEPATH "filepath"

struct HarmonyDB
{
    HarmonyItem root;
    HarmonyObject *local_root;

    HarmonyDB();
    ~HarmonyDB();
    void setRoot(HarmonyObject *object);
    HarmonyObject * getRoot();

    void clear();
    void deleteNode(HarmonyObject *object);
    HarmonyObject * getObjectByPath(const HarmonyObjectPath &path, HarmonyObject *start = NULL);
    HarmonyObject * queryRelation(HarmonyObject *relation, HarmonyObject *source_set, HarmonyObject *source_object,
        HarmonyObject *destination_set);

    void loadFile(string filepath, HarmonyObject *root, string relative_path = string());
    void loadDir(string filepath, HarmonyObject *root, string relative_path = string());
    void dumpBase(HarmonyObject *object = NULL, string const &filepath = string());
    void dumpBase(HarmonyObject *object, int indent_level, bool dont_indent, bool primary, string const &label, string const &filepath, FILE *config_file);
// sweep
    void sweep(HarmonyObject *object, HarmonyItem *parent = NULL, bool nonstructural = false);
// sweep
    void findTemporaryLabels(HarmonyObject *object, bool first = true);
    void substituteObject(HarmonyObject *old_object, HarmonyObject *new_object);

    HarmonyObject * createContext(HarmonyObject *source, string name = string(), HarmonyObject *return_object = NULL, HarmonyObject *arg = NULL);
    HarmonyObject * cloneObject(HarmonyObject *source, HarmonyObject *parent = NULL, string label = string(), bool primary = false);
// sweep
    HarmonyObject * cloneArgument(HarmonyObject *source, HarmonyObject *parent = NULL);
// swwep
    void fillClonedObject(HarmonyObject *object, HarmonyObject *context, HarmonyObject *parent_receiver = NULL);
    void copyArgument(HarmonyObject *source, HarmonyObject *named, HarmonyObject *unnamed = NULL, HarmonyObject *return_object = NULL);
    bool clearArguments(HarmonyObject *receiver, unsigned level = 0);
};

HarmonyDB * buildBase(const char *filepathS);

#endif