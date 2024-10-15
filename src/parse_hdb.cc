#include <stdio.h>
#include <map>
#include <string>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>

#include "hdb_driver.h"
#include "hdb_objects.h"
#include "harmonydb.h"
#include "common.h"

using namespace std;

HdbObject::~HdbObject()
{
    // PF("%p", this);
    for (auto it : children)
        delete it;
}

HdbObjectElement::~HdbObjectElement()
{
    // PF("%p", this);
    delete set;
}

HdbObjectReference::~HdbObjectReference()
{
    // PF("%p", this);
    delete reference;
}

HdbObjectRelation::~HdbObjectRelation()
{
    // PF("%p", this);
    delete relation;
    delete source;
    delete destination;
    if (owner)
        delete owner;
}

HdbSymbolReference::~HdbSymbolReference()
{
    // PF("%p", this);
    delete path;
}

HdbObjectProxy::~HdbObjectProxy()
{
    if (reference)
        delete reference;
}

HarmonyObject * getObjectByReference(HarmonyDB *db, HdbSymbolReference *reference, HdbObject *start)
{
    HarmonyObject *last_start_object = NULL;
    HarmonyObjectPath path;
    HarmonyObject *destination;

    // PF("%p:%p:%C %p[%s] %p", reference, reference->path, reference->absolute ? 'a': 'r', start, start->label.c_str(), start->harmony_object);

    if (!reference->path)
        return db->root.object;

    path = *reference->path;
    if (!reference->absolute) {
        auto pi = path.begin();

        do {
            // PF("%p [%s]", start, pi->c_str());
            for (auto o : start->children) {
                // PF("%p %p [%s] [%s]", start, o, o->label.c_str(), pi->c_str());
                if (o->label == *pi) {
                    // PF("Found");
                    pi++;
                    if (pi == path.end()) {
                        // PF("Found %p", o->harmony_object);
                        return o->harmony_object;
                    }
                    start = o;
                    goto out;
                }
            }
            last_start_object = start->harmony_object;
            // PF("%p %p %p [%s]", start, start->harmony_object, start->parent, start->label.c_str());
            if (start->label == *pi) {
                    // PF("Found");
                    pi++;
                    if (pi == path.end()) {
                        // PF("Found %p", o->harmony_object);
                        return start->harmony_object;
                    }
                    continue;
            }
            start = start->parent;
out:
            ;
        } while (start);
    }

    // PF("%p %p [%s]", db->root.object, start, path.toString().c_str());
    // if (reference->global_name.empty()) {
    //     destination = db->getObjectByPath(path, db->local_root);
    //     if (destination)
    //         return destination;
    // }
    destination = db->getObjectByPath(path, last_start_object);
    // PF("Destination [%s] %p", path.toString().c_str(), destination);
    assertf(destination, "Destination object [%c%s] not found!", reference->absolute ? '.': ' ', path.toString().c_str());
    return destination;
}

void hdbIterate(HarmonyDB *db, struct HdbObject *object, bool isroot = false)
{
    if (auto o = dynamic_cast<HdbObjectElement *>(object)) {
        auto element = new HarmonyObject(HarmonyObject::Type::ELEMENT);
        element->element_value = o->value;
        object->harmony_object = element;
    } else if (auto o = dynamic_cast<HdbObjectType *>(object)) {
        auto type = new HarmonyObject(HarmonyObject::Type::TYPE);
        type->type_lower = o->lower;
        type->type_higher = o->higher;
        object->harmony_object = type;
    } else if (auto o = dynamic_cast<HdbObjectCode *>(object)) {
        auto code = new HarmonyObject(o->code);
        object->harmony_object = code;
    } else if (auto o = dynamic_cast<HdbObjectRelation *>(object)) {
        if (!o->owner) {
            HarmonyRelation *r = new HarmonyRelation;
            r->owner = object->harmony_object;
            o->harmony_relation = r;
            r->label = object->label;
            object->harmony_object = r;
        } else {
            auto p = new HarmonyObject(HarmonyObject::Type::PATTERN);
            object->harmony_object = p;
        }
    } else if (auto o = dynamic_cast<HdbObjectReference *>(object)) {
        auto *stub = new HarmonyObject;
        object->harmony_object = stub;
    } else if (auto o = dynamic_cast<HdbObjectProxy *>(object)) {
        auto *proxy = new HarmonyObject(HarmonyObject::Type::PROXY);
        if (auto r = dynamic_cast<HdbObjectReference *>(o->reference)) {
        } else if (o->reference != NULL) {
            hdbIterate(db, o->reference);
            proxy->link(o->reference->harmony_object);
        }
        object->harmony_object = proxy;
    } else  if (auto o = dynamic_cast<HdbObject *>(object)) {
        assert(o != NULL);
        auto nil = new HarmonyObject;
        object->harmony_object = nil;
    } else {
        P("Unknown object!\n");
        assert(0);
    }
    if (!object->hints.empty()) {
        for (auto i: object->hints) {
            object->harmony_object->hints[i.name] = i.value;
        }
    }
    if (isroot)
        object->harmony_object->root_distance = 1;
    for (auto it: object->children) {
        hdbIterate(db, it);
        if (auto r = dynamic_cast<HarmonyRelation *>(it->harmony_object)) {
            // PF("ADD REL");
            object->harmony_object->addRelation(r, !it->temporary ? it->label: string());
        } else {
            // PF("[%s]%d: %p", it->label.c_str(), it->temporary, it->harmony_object);
            auto i = object->harmony_object->add(it->harmony_object, !it->temporary ? it->label: string(), true); // make structural and primary
        }
    }
}

HarmonyObjectPath createPath(const HdbPath &path)
{
    HarmonyObjectPath new_path;

    new_path = static_cast<list<string> >(path);
    return new_path;
}

void hdbResolveReferences(struct HarmonyDB *db, int level, struct HarmonyObject *parent, struct HdbObject *object)
{
    if (auto o = dynamic_cast<HdbObjectElement *>(object)) {
        auto type = getObjectByReference(db, o->set, object);
        assert(type);
        object->harmony_object->element_type.setReference(type);
    } else if (auto o = dynamic_cast<HdbObjectProxy *>(object)) {
        if (o->reference) {
            if (auto r = dynamic_cast<HdbObjectReference *>(o->reference)) {
                auto ref = getObjectByReference(db, r->reference, object);
                object->harmony_object->link(ref);
            } else {
                hdbResolveReferences(db, level + 1, object->harmony_object, o->reference);
            }
        }
    // } else if (auto o = dynamic_cast<HdbObjectCode *>(object)) {
    //     if (object->harmony_object->type == HarmonyObject::Type::RECEIVE) {
    //         PF("GOT RECEIVE");
    //         for (auto it: object->children) {
    //             if (!it->harmony_object->isEmpty()) {
    //                 it->harmony_object->parent_receiver = object->harmony_object;
    //             }
    //         }
    //     }
    } else if (auto o = dynamic_cast<HdbObjectRelation *>(object)) {
        // PF("%p %p %p", parent, o->harmony_relation, o->harmony_relation->owner);
        if (!o->owner) {
            o->harmony_relation->relation.setReference(getObjectByReference(db, o->relation, object));
            o->harmony_relation->source.setReference(getObjectByReference(db, o->source, object));
            o->harmony_relation->destination.setReference(getObjectByReference(db, o->destination, object));
        } else {
            auto p = o->harmony_object;
            p->relation.setReference(getObjectByReference(db, o->relation, object));
            p->source.setReference(getObjectByReference(db, o->source, object));
            p->destination.setReference(getObjectByReference(db, o->destination, object));
            p->pattern_owner.setReference(getObjectByReference(db, o->owner, object));
        }
    } else if (auto o = dynamic_cast<HdbObjectReference *>(object)) {
        HarmonyObject *destination;

        destination = getObjectByReference(db, o->reference, object);
        db->substituteObject(object->harmony_object, destination);
    }
    for (auto it: object->children) {
        hdbResolveReferences(db, level + 1, object->harmony_object, it);
    }
}

void HarmonyDB::loadFile(string filepath, HarmonyObject *root, string relative_path)
{
    list<HdbObject *> *hdb_objects;
    hdb_driver drv;
    int i;
    FILE *file;

    file = fopen((filepath + relative_path).c_str(), "r");
    assert(file);

    PF("Loading %s%s...", filepath.c_str(), relative_path.c_str());
    i = drv.parse(file);
    assert(i == 0);
    hdb_objects = drv.root;
    for (auto o: *hdb_objects) {
        hdbIterate(this, o, root == NULL);
        if (root)
            root->add(o->harmony_object, o->label, true);
        else
            setRoot(o->harmony_object);
        o->harmony_object->hints[HINT_BACKEND] = HINT_BACKEND_FILE;
        o->harmony_object->hints[HINT_FILEPATH] = relative_path;
        local_root = root;
        PF("Resolve references [%s]", root ? o->label.c_str(): ".");
        hdbResolveReferences(this, 0, NULL, o);
        delete o;
    }
    delete hdb_objects;
}

void HarmonyDB::loadDir(string filepath, HarmonyObject *root, string relative_path)
{
    DIR *dir;

    // PF(" [%s] %p", filepath.c_str(), root);
    dir = opendir((filepath + relative_path).c_str());
    if (dir) {
        struct dirent *entry;

        while ((entry = readdir(dir))) { // first load files
            if (entry->d_name[0] != '.' && entry->d_type != DT_DIR) {
                // PF("---> [%s] %c", entry->d_name, entry->d_type == DT_DIR ? 'd': 'f');
                HarmonyObjectPath object_path;
                HarmonyObject *o;
                string fname = string(entry->d_name);

                if (fname.length() > 4 && fname.substr(fname.length() - 4) == ".hdb") {
                    object_path.push_back(entry->d_name);
                    o = getObjectByPath(object_path, root);
                    assert(!o);
                    loadFile(filepath, root, relative_path + "/" + entry->d_name);
                }
            }
        }
        rewinddir(dir);
        while ((entry = readdir(dir))) { // go to subdirs
            if (entry->d_name[0] != '.' && entry->d_type == DT_DIR) {
                HarmonyObjectPath object_path;
                HarmonyObject *o;

                object_path.push_back(entry->d_name);
                o = getObjectByPath(object_path, root);
                if (!o) {
                    // PF("subdir not found, creating one");
                    o = new HarmonyObject;
                    root->add(o, entry->d_name, true);
                }
                loadDir(filepath, o, relative_path + "/" + entry->d_name);
                // PF("---> [%s] %c", entry->d_name, entry->d_type == DT_DIR ? 'd': 'f');
            }
        }
        closedir(dir);
    }
}

HarmonyDB * buildBase(const char *filepath)
{
    HarmonyDB *db;
    struct stat sb;

    assertf(stat(filepath, &sb) == 0, "Couldn't stat %s!", filepath);

    db = new HarmonyDB;
    if ((sb.st_mode & S_IFMT) == S_IFDIR) {
        auto root = new HarmonyObject;
        db->setRoot(root);
        db->loadDir(filepath, root);
    } else if ((sb.st_mode & S_IFMT) == S_IFREG) {
        db->loadFile(filepath, NULL);
    }

    return db;
}
