#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <typeinfo>
#include <sys/stat.h>

#include "harmonydb.h"
#include "common.h"

// HarmonyObjectReference


HarmonyObjectReference::HarmonyObjectReference()
{
    // PF("%p", this);
    object = NULL;
    prev = this;
    next = this;
    structural = false;
    primary = false;
    structural_references = 0;
}

HarmonyObjectReference::~HarmonyObjectReference()
{
    // PF("%p %p", this, object);
    if (object)
        removeReference();
}

void HarmonyObjectReference::setReference(HarmonyObject *object, bool structural, bool primary)
{
    // PF("r:%p  o:%p", this, object);
    assert(this->object == NULL);
    assert(object);
    this->object = object;
    prev = object->reference.next->prev;
    next = object->reference.next;
    next->prev = this;
    prev->next = this;
    this->structural = structural;
    this->primary = primary;
    if (primary) {
        assert(object->has_primary == false);
        object->has_primary = true;
    }
    if (structural) {
        object->reference.structural_references++;
        // PF(">>>>>>>. %p->%p %d", this, object, object->reference.structural_references);
    }
}

void HarmonyObjectReference::_removeReference()
{
    // PF("%p %d:%d", object, structural, object->reference.structural_references);
    if (structural) {
        structural = false;
        assert(object->reference.structural_references > 0);
        object->reference.structural_references--;
    }
    prev->next = next;
    next->prev = prev;
    prev = this;
    next = this;
    object = NULL;
}

void HarmonyObjectReference::removeReference()
{
    auto was_structural = structural;

    // PF("%p s:%d sr:%d", object, structural, object->reference.structural_references);
    if (structural) {
        structural = false;
        assert(object->reference.structural_references > 0);
        object->reference.structural_references--;

        if (primary)
            object->has_primary = false;

        if (object->reference.structural_references == 0) { // no more structural references
            object->clearRelations();
            object->clear();
        }
    }
    if (object->reference.next == object->reference.prev) { // last one
        // PF("~%p", object);
        prev->next = next;
        next->prev = prev;
        prev = this;
        next = this;
        auto object_to_delete = object;
        object = NULL;
        delete object_to_delete;
    } else {
        if (was_structural) {
            // PF("CONVERGENCE sr:%d", object->reference.structural_references);

            START_SWEEP
            int is_reachable = object->isReachable(object);
            FINISH_SWEEP

            // PF("is reachable? : %d", is_reachable);
            if (is_reachable == 2) { // disconnected with cycles found, break them
                START_SWEEP
                object->ripCycles(HarmonyObject::_current_sweep_mark);
                FINISH_SWEEP

                // PF("SR %p %d", object, object->reference.structural_references);
                if (object->reference.structural_references == 0) {
                    prev->next = next;
                    next->prev = prev;
                    prev = this;
                    next = this;
                    auto object_to_delete = object;
                    object = NULL;
                    delete object_to_delete;
                } else {
                    object->updateDistance(object);
                    prev->next = next;
                    next->prev = prev;
                    prev = this;
                    next = this;
                    object = NULL;
                }
            } else {
                object->updateDistance(object);
                prev->next = next;
                next->prev = prev;
                prev = this;
                next = this;
                object = NULL;
            }
        } else {
            prev->next = next;
            next->prev = prev;
            prev = this;
            next = this;
            object = NULL;
        }
    }
}

int HarmonyObjectReference::countReferences()
{
    uint64_t count = 0;

    for (auto r = next; r != this; r = r->next) {
        count++;
    }
    return count;
}

bool HarmonyObjectReference::isEmpty()
{
    if (next == this)
        return true;
    return false;
}

// HarmonyItem

HarmonyItem * HarmonyItem::nextItem(HarmonyObject *set)
{
    if (next == &set->items) { // the end
        return NULL;
    } else {
        return next;
    }
}

HarmonyItem * HarmonyItem::previousItem(HarmonyObject *set)
{
    if (prev == &set->items) { // the end
        return NULL;
    } else {
        return prev;
    }
}

HarmonyItem * HarmonyItem::nextRelation(HarmonyObject *set)
{
    if (next == &set->relations) { // the end
        return NULL;
    } else {
        return next;
    }
}

HarmonyItem::~HarmonyItem()
{
}

// HarmonyObjectPath

HarmonyObjectPath & HarmonyObjectPath::operator=(const list<string> &l)
{
    this->list::operator=(l);
    return *this;
}

bool HarmonyObjectPath::parse(const string &s)
{
    bool absolute = false;
    size_t e = 0;

    clear();
    for (;;) {
        auto n = s.find(".", e);
        if (n == 0) {
            absolute = true;
            e = 1;
            continue;
        }
        if (n == std::string::npos) {
            push_back(s.substr(e, n));
            break;
        }
        push_back(s.substr(e, n - e));
        e = n + 1;
    }
    return absolute;
}

const string HarmonyObjectPath::toString()
{
    string r;

    for (auto it : *this) {
        r += ".";
        r += it;
    }
    return r;
}

const string HarmonyObjectPath::toString() const
{
    string r;

    for (auto it : *this) {
        r += ".";
        r += it;
    }
    return r;
}

//////////////////////////////////////////////////////////////////////////////////////////
// HarmonyObject

unsigned HarmonyObject::_object_count = 0;
bool HarmonyObject::_sweeping = false;
unsigned HarmonyObject::_current_sweep_mark = 0;
unsigned HarmonyObject::_old_sweep_mark = 0;

HarmonyObject::HarmonyObject(Type t)
{
    // PF("%p", this);
    items.next = &items;
    items.prev = &items;
    relations.next = &relations;
    relations.prev = &relations;
    root_distance = 0;
    has_primary = false;
    sweep_mark = 0;
    sweep_parent = NULL;
    temporary_label_sweep_mark = 0;
    _object_count++;
    type = t;
    context = NULL;
    parent_receiver = NULL;
    receiver_armed = 0;
    receiver_got = 0;
    unknown = false;
    negative = false;
    loop = false;

    element_value = 0;
    type_lower = INT64_MIN;
    type_higher = INT64_MAX;

    // PF("object_count: %d", _object_count);
}

HarmonyObject::~HarmonyObject()
{
    if (isElement()) {
        element_type.removeReference();
    } else if (isProxy()) {
        link(NULL);
    }
    // PF("<%p> Deleting relations...", this);
    clearRelations();
    // PF("<%p> Clearing...", this);
    clear();
    // PF("<%p> Cleared", this);
    assert(isEmpty() == true);

    while (reference.next != &reference) {
        // PF("%p removing reference to %p", this, reference.next->object);
        reference.next->_removeReference();
    }

    assertf(reference.isEmpty() == true, "Object <%p> %p %p still referenced!", this, reference.prev, reference.next);
    _object_count--;
    // PF("object_count: %d", _object_count);
}

const string HarmonyObject::getHint(const string &hint)
{
    auto it = hints.find(hint);
    if (it != hints.end())
        return it->second;
    return string();
}

int HarmonyObject::isEmpty()
{
    return items.prev == &items;
}

void HarmonyObject::clear()
{
    HarmonyItem *item;

    // PF("1 %p  %p %p %p", this, items.prev, &items, items.next);
    while (!isEmpty()) {
        item = items.next;
        // PF("Removing %p", item->object);
        remove(item);
    }
    // PF("2 %p  %p %p %p", this, items.prev, &items, items.next);
}

void HarmonyObject::clearRelations()
{
    while (relations.next != &relations) {
        auto r = static_cast<HarmonyRelation *>(relations.next->object);
        // PF("Deleting relation %p", r);
        r->source.removeReference();
        r->destination.removeReference();
        r->relation.removeReference();
        removeRelation(relations.next);
    }
}

HarmonyItem * HarmonyObject::first()
{
    HarmonyItem *item;

    item = NULL;
    if (!isEmpty()) {
        item = items.next;
    }
    return item;
}

HarmonyItem * HarmonyObject::last()
{
    HarmonyItem *item;

    item = NULL;
    if (!isEmpty()) {
        item = items.prev;
    }
    return item;
}

HarmonyItem * HarmonyObject::next(HarmonyObject *object)
{
    auto o = first();
    for (; o != NULL; o = o->nextItem(this)) {
        if (o->object == object) {
            return o->nextItem(this);
        }
    }
    return NULL;
}

HarmonyItem * HarmonyObject::prev(HarmonyObject *object)
{
    auto o = last();
    for (; o != NULL; o = o->previousItem(this)) {
        if (o->object == object) {
            return o->previousItem(this);
        }
    }
    return NULL;
}

void HarmonyObject::updateDistance(HarmonyObject *start, HarmonyObject * parent_root_distance)
{
    HarmonyObjectReference *ref;
    HarmonyObject *nrdp = NULL;

    // PF("%p st:%p -> rd:%d  prd:%p", this, start, root_distance, parent_root_distance);
    ref = reference.next;
    unsigned lowest = 1000000;
    // PF("  %p %p", ref, &reference);
    while (ref != &reference) {
        // PF(" %p -> s:%d", ref->object, ref->structural);
        if (ref->structural) {
            HarmonyItem *item = static_cast<HarmonyItem *>(ref);
            // PF("     %p -> p:%p", ref->object, item->parent);
            if (item->parent) {
            // PF("     %p -> rd:%d", ref->object, item->parent->root_distance);
                if (item->parent != this && lowest > item->parent->root_distance) {
                    lowest = item->parent->root_distance;
                    nrdp = item->parent;
                } else if (lowest == item->parent->root_distance && parent_root_distance != item->parent) {
                    nrdp = item->parent;
                }
            } else { // updating root, set lowest to 0
                lowest = 0;
            }
        }
        ref = ref->next;
    }
    // PF("%p rd:%d  lowest:%d  sc:%d  nrdp:%p", this, root_distance, lowest, reference.structural_references, nrdp);
    if (lowest == 1000000)
        return;
    lowest++;
    if (root_distance != lowest) { // lowest can be only equal or higher
        if (parent_root_distance != nrdp) {
            start = this;
            // PF("New start %p", start);
        }
        root_distance = lowest;
        // PF("%p %p -> UPDATED %d", this, start, root_distance);

        if (isProxy()) {
            if (proxy.object) {
                proxy.object->updateDistance(start, this);
            }
        } else {
            auto o = first();
            for (; o != NULL; o = o->nextItem(this)) {
                o->object->updateDistance(start, this);
            }
        }
    }
}

int HarmonyObject::isReachable(HarmonyObject *start)
{
    HarmonyObjectReference *ref;
    int found_cycle = 0;

    // PF("%p:%p  rd:%d  was_here:%d", this, start, root_distance, sweep_mark == _current_sweep_mark);

    if (sweep_mark == _current_sweep_mark)
        return 0;
    sweep_mark = _current_sweep_mark;

    if (root_distance == 1) // is root
        return 1;
    ref = reference.next;
    while (ref != &reference) {
        if (ref->structural) {
            int r;

            HarmonyItem *item = static_cast<HarmonyItem *>(ref);
            auto parent = item->parent;

            // PF(" %p -> s:%d", parent, ref->structural);
            r = 0;
            if (parent == start)
                found_cycle = 2;
            if (parent != start && ((r = parent->isReachable(start)) & 1)) {
                // PF("%p reachable", this);
                return found_cycle | 1 | r;
            } else {
                found_cycle |= r;
            }
        }
        ref = ref->next;
    }
    // PF("%p unreachable %d", this, found_cycle);
    return found_cycle;
}

void HarmonyObject::ripCycles(unsigned mark)
{
    sweep_mark = mark;
    // PF("%p %d", this, mark);
    if (isProxy()) {
        if (auto o = proxy.object) {
            // PF(" -> %p %d", o, o->sweep_mark);
            if (o->sweep_mark != mark) {
                o->ripCycles(mark);
            } else {
                // PF("UNLINK %p -> %p", p, p->reference.object);
                proxy._removeReference();
                proxy.parent = NULL;
            }
        }
    } else {
        for (auto i = first(); i != NULL;) {
            // PF(" -> %p %d", i->object, i->object->sweep_mark);
            if (i->object->sweep_mark != mark) {
                i->object->ripCycles(mark);
                i = i->nextItem(this);
            } else {
                // PF("%p -> %p %d", this, i->object, i->object->sweep_mark);
                i = remove(i, true);
            }
            // PF(" next i:%p", i);
        }
    }
    // PF("%p %d done", this, mark);
}

HarmonyItem * HarmonyObject::add(HarmonyObject *object, string label, bool primary)
{
    HarmonyItem *item;

    if (!label.empty()) {
        auto o = findItem(label);
        auto r = findRelation(label);
        assertf(o == NULL && r == NULL, "Local label \"%s\" already used!", label.c_str());
    }
    item = new HarmonyItem;
    item->parent = this;
    item->setReference(object, true, primary);
    item->label = label;

    item->prev = items.prev;
    item->next = items.prev->next;
    item->next->prev = item;
    item->prev->next = item;

    if (object->root_distance == 0 or object->root_distance > root_distance + 1) {
        // object->root_distance = root_distance + 1;
        object->updateDistance(object);
        // update children
    }
    return item;
}

HarmonyItem * HarmonyObject::remove(HarmonyItem *item, bool internal)
{
    auto next = item->next;
    item->prev->next = item->next;
    item->next->prev = item->prev;
    if (internal)
        item->_removeReference();
    else
        item->removeReference();
    delete item;

    if (next == &items) { // the end
        return NULL;
    } else {
        return next;
    }
}

HarmonyItem * HarmonyObject::findItem(const string &label)
{
    if (label.empty())
        return NULL;
    auto o = first();
    for (; o != NULL; o = o->nextItem(this)) {
        if (o->label == label) {
            return o;
        }
    }
    return NULL;
}

HarmonyItem * HarmonyObject::findItem(HarmonyObject *object)
{
    if (!object)
        return NULL;
    auto o = first();
    for (; o != NULL; o = o->nextItem(this)) {
        if (o->object == object) {
            return o;
        }
    }
    return NULL;
}

HarmonyItem * HarmonyObject::findRelation(const string &label)
{
    auto r = relations.next;
    while (r != &relations) {
        if (r->label == label)
            return r;
        r = r->next;
    }
    return NULL;
}

string HarmonyObject::getKey()
{
    char key[32];

    snprintf(key, sizeof(key), "K%pK", this);
    return string(key);
}

void HarmonyObject::addRelation(HarmonyObject *relation, HarmonyObject *source, HarmonyObject *destination)
{
PF("*** addRelation 1\n");
    // auto r = new HarmonyRelation;
    // r->relation.setReference(relation);
    // r->source.setReference(source);
    // r->destination.setReference(destination);
    // source_set->destination_relations.insert(make_pair(make_pair(destination_set, relation), r));
}

HarmonyItem * HarmonyObject::addRelation(HarmonyRelation *r, string label)
{
    // PF("%p -> %p", r, this);
    HarmonyItem *item;

    item = new HarmonyItem;
    item->parent = this;
    item->setReference(r);
    item->label = label;

    item->prev = relations.prev;
    item->next = relations.prev->next;
    item->next->prev = item;
    item->prev->next = item;

    r->owner = this;

    // r->sweep_mark = _current_sweep_mark;

    return item;
}

HarmonyItem * HarmonyObject::removeRelation(HarmonyItem *item)
{
    auto next = item->next;
    item->prev->next = item->next;
    item->next->prev = item->prev;
    item->removeReference();
    delete item;

    if (next == &relations) { // the end
        return NULL;
    } else {
        return next;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// HarmonyDB

void HarmonyDB::setRoot(HarmonyObject *object)
{
    if (root.object)
        root.removeReference();
    if (object) {
        root.setReference(object, true);
        root.parent = NULL;
        object->sweep_parent = NULL;
        object->root_distance = 1;
        object->has_primary = true;
    }
}

HarmonyObject * HarmonyDB::getRoot()
{
    return root.object;
}

void HarmonyDB::substituteObject(HarmonyObject *old_object, HarmonyObject *new_object)
{
    // PF("%p -> %p", old_object, new_object);
    auto it = old_object->reference.next;
    while (it != &old_object->reference) {
        it->object = new_object;
        it->primary = false;
        it = it->next;
    }
    if (old_object->reference.next != &old_object->reference) {
        new_object->reference.next->prev = old_object->reference.prev;
        old_object->reference.prev->next = new_object->reference.next;

        new_object->reference.next = old_object->reference.next;
        old_object->reference.next->prev = &new_object->reference;

        old_object->reference.prev = &old_object->reference;
        old_object->reference.next = &old_object->reference;
    }
    new_object->reference.structural_references = new_object->reference.structural_references + 1;
    delete old_object;

    new_object->updateDistance(new_object);
    // PF("%p %d:%d", new_object, new_object->reference.structural_references, new_object->root_distance);
    // int i = 0;
    // it = new_object->reference.next;
    // while (it != &new_object->reference) {
    //     i++;
    //     it = it->next;
    // }
}

HarmonyObject * HarmonyDB::getObjectByPath(const HarmonyObjectPath &path, HarmonyObject *start)
{
    auto r = start;

    if (!start)
        r = root.object;
    // PF("%p", r);
    for (auto it : path) {
        auto oit = r->findItem(it);
        if (oit)
            r = oit->object;
        else {
            auto rit = r->findRelation(it);
            if (rit)
                return rit->object;
            return NULL;
        }
    }
    return r;
}

void HarmonyDB::sweep(HarmonyObject *object, HarmonyItem *parent, bool nonstructural)
{
    if (!parent) {
        START_SWEEP
        object->sweep_parent = NULL;
    }
    // PF("%p %p %d %d", object, parent, object->has_primary, parent ? parent->primary: -1);
    if (!nonstructural) {
        if (parent && object->has_primary && !parent->primary)
            goto out;
        if (parent && !object->has_primary && object->root_distance <= parent->parent->root_distance)
            goto out;
        if (!object->has_primary && object->sweep_mark == object->_current_sweep_mark)
            goto out;
    } else {
        if (parent && object->root_distance <= parent->parent->root_distance)
            goto out;
        if (object->sweep_mark == object->_current_sweep_mark)
            goto out;
    }
    // PF("%p marked", object);
    object->sweep_mark = object->_current_sweep_mark;
    object->sweep_parent = parent;

    if (object->isProxy() && object->proxy.object) {
        auto proxy = object->getObject();
        // PF("proxy %d", object->proxy.primary);
        sweep(proxy, &object->proxy);
        goto out;
    }

    HarmonyItem *item;

    for (item = object->items.next; item != &object->items; item = item->next) {
        // PF(">%p %p  isproxy:%d", item->object, item, item->object->isProxy());
        sweep(item->object, item);
    }

    for (item = object->relations.next; item != &object->relations; item = item->next) {
        auto r = static_cast<HarmonyRelation *>(item->object);

        item->object->sweep_parent = parent;
        sweep(r->relation.object, item, true);
        sweep(r->source.object, item, true);
        sweep(r->destination.object, item, true);
    }
    // PF("%p %d", object, object->type);
    if (object->type == HarmonyObject::Type::PATTERN) {
        // PF("%p %p %p %p", object->relation.object, object->source.object, object->destination.object, object->pattern_owner.object);
        sweep(object->relation.object, parent, true);
        sweep(object->source.object, parent, true);
        sweep(object->destination.object, parent, true);
        sweep(object->pattern_owner.object, parent, true);
    }
out:
    if (!parent) {
        FINISH_SWEEP
    }
    // PF("out");
}

void HarmonyDB::findTemporaryLabels(HarmonyObject *object, bool first)
{
    // PF("%p:%d", object, first);
    if (first) {
        START_SWEEP
    }
    object->sweep_mark = object->_current_sweep_mark;

    if (object->isProxy()) {
        if (object->proxy.object) {
            auto proxy = object->getObject();

            if (proxy->has_primary &&
                (proxy->sweep_mark == object->_old_sweep_mark) &&
                object->proxy.primary) {
                findTemporaryLabels(proxy, false);
            } else {
                proxy->findPath(object);
            }
            // if (proxy->has_primary && object->proxy.primary || proxy->sweep_mark == object->_current_sweep_mark) {
            //     proxy->findPath(object);
            // } else {
            //     // proxy->sweep_parent = object->sweep_parent;
            //     findTemporaryLabels(proxy, false);
            // }
        }
        if (first) {
            FINISH_SWEEP
        }
        return;
    } else if (object->isElement()) {
        assert(object->element_type.object);
        object->element_type.object->findPath(object);
    }

    HarmonyItem *item;

    for (item = object->items.next; item != &object->items; item = item->next) {
        if ((item->object->has_primary && !item->primary) ||
            (!item->object->has_primary && (item->object->root_distance <= object->root_distance || item->object->sweep_mark == item->object->_current_sweep_mark))) {
            /* is referenced? */
            item->object->findPath(object);
            continue;
        }

        // item->object->sweep_parent = item;
        // PF("%p %p:%p %p", object, item, item->object, item->object->sweep_parent);
        findTemporaryLabels(item->object, false);
    }
    for (item = object->relations.next; item != &object->relations; item = item->next) {
        auto r = static_cast<HarmonyRelation *>(item->object);
        // PF("%p %p %p", r->relation.object, r->source.object, r->destination.object);
        r->relation.object->findPath(object);
        r->source.object->findPath(object);
        r->destination.object->findPath(object);
    }
    // PF("%p %d", object, object->type);
    if (object->type == HarmonyObject::Type::PATTERN) {
        // PF("%p %p %p %p", object->relation.object, object->source.object, object->destination.object, object->pattern_owner.object);
        object->relation.object->findPath(object);
        object->source.object->findPath(object);
        object->destination.object->findPath(object);
        object->pattern_owner.object->findPath(object);
    }
    if (first) {
        FINISH_SWEEP
    }
}

void HarmonyObject::findPath(HarmonyObject *start)
{
    list<HarmonyItem *> path;
    list<HarmonyItem *> start_path;

    // PF("%p:%p -> %p:%p", start, start->sweep_parent, this, sweep_parent);
    HarmonyItem *i;

    assert(this);

    for (i = sweep_parent; i; i = i->parent->sweep_parent) {
        path.push_front(i);
    }

    for (i = start->sweep_parent; i; i = i->parent->sweep_parent) {
        start_path.push_front(i);
    }

    // assert(pit != path.end());
    // assert(spit != start_path.end());

    // for (auto pit: path) {
    //     PF("pit [%s]", pit->label.c_str());
    // }
    // for (auto spit: start_path) {
    //     PF("spit [%s]", spit->label.c_str());
    // }

    auto pit = path.begin();
    auto spit = start_path.begin();

    for (; pit != path.end(); pit++) {
        if (spit != start_path.end() && (*pit)->object == (*spit)->object && (*spit)->object != this) {
            spit++;
        } else {
            for (; pit != path.end(); pit++) {
                if ((*pit)->label.empty()) {
                    (*pit)->object->temporary_label_sweep_mark = _current_sweep_mark;
                    // PF("make temp %p [%s]", (*pit)->object, (*pit)->label.c_str());
                }
            }
            break;
        }
    }
}

// void HarmonyRelation::findPath(HarmonyObject *start)
// {
//     // PF("%p", this);
//     HarmonyObject *o;
//     HarmonyItem *i;

//     assert(this);
//     if (!global_name.empty())
//         return;
//     o = this;
//     // PF("%p %p", this, sweep_parent);

//     i = NULL;
//     // PF("%p", owner);
//     // PF("FOUND RELATION");
//     i = owner->sweep_parent;
// //    assert(i);
//     for (; i; i = i->parent->sweep_parent) {
//         // PF("  %p %p <%s><%s> %p %p", o, i, i->object->global_name.c_str(), i->label.c_str(), i->object, i->parent->sweep_parent);
//         if (i->label.empty()) {
//             o->global_name_sweep_mark = _current_sweep_mark;
//             break;
//         }
//         if (!i->parent->global_name.empty()) {
//             break;
//         }
//         o = i->parent;
//     }
// }

string HarmonyObject::getPath(HarmonyObject *start)
{
    list<HarmonyItem *> path;
    list<HarmonyItem *> start_path;
    HarmonyItem *local_root = NULL;
    string spath;

    // PF("%p:%p -> %p:%p", start, start->sweep_parent, this, sweep_parent);
    HarmonyItem *i;

    assert(this);

    if (!sweep_parent)
        return ".";
    for (i = sweep_parent; i; i = i->parent->sweep_parent) {
        path.push_front(i);
    }

    for (i = start->sweep_parent; i; i = i->parent->sweep_parent) {
        start_path.push_front(i);
    }

    // for (auto pit: path) {
    //     PF("pit %p [%s]", pit, pit->label.c_str());
    // }
    // for (auto spit: start_path) {
    //     PF("spit %p [%s]", spit, spit->label.c_str());
    // }
    auto pit = path.begin();
    auto spit = start_path.begin();

    // assert(pit != path.end());
    // assert(spit != start_path.end());

    for (; pit != path.end() && spit != start_path.end(); pit++) {
        // PF("found %p %p", (*spit)->object, this);
        if ((*spit)->object == this) {
            local_root = *pit;
            break;
        }
        if ((*pit)->object == (*spit)->object) {
            auto it = spit;
            it++;
            if (!(*pit)->label.empty()) {
                for (; it != start_path.end(); it++) {
                    if ((*pit)->label == (*it)->label)
                        goto out;
                }
            }
            // PF("same");
            local_root = *pit;
            spit++;
            continue;
        }
        break;
    }
    // for (auto pit2 = pit; pit2 != path.end(); pit2++) {
    //     PF("pit2 %p [%s]", *pit2, (*pit2)->label.c_str());
    // }
    // for (auto spit2 = spit; spit2 != start_path.end(); spit2++) {
    //     PF("spit2 %p [%s]", *spit2, (*spit2)->label.c_str());
    // }
out:
    // PF("local_root %p", local_root);
    // for (i = sweep_parent; i && i != local_root; i = i->parent->sweep_parent) {
    //     PF("%p %p %p [%s]", sweep_parent, i, local_root, i->label.c_str());
    // if ((pit == path.begin() && pit == path.end()) || !local_root)
    // PF("lr: %p %p", local_root, local_root->object);

    // if (local_root) {
    //     if (local_root->object->temporary_label_sweep_mark == HarmonyObject::_old_sweep_mark)
    //         spath = local_root->object->getKey();
    //     else
    //         spath = local_root->label;
    // }
restart:
    unsigned path_size = 1;
    for (auto pit2 = pit; pit2 != path.end(); pit2++) {
        for (auto spit2 = spit; spit2 != start_path.end(); spit2++) {
            for (unsigned i = 0; i < path_size; i++) {
                string l, p;
                auto pit3 = pit2;
                auto spit3 = spit2;

                if ((*pit3)->object->temporary_label_sweep_mark == HarmonyObject::_old_sweep_mark)
                    l = (*pit3)->object->getKey();
                else
                    l = (*pit3)->label;

                if ((*spit3)->object->temporary_label_sweep_mark == HarmonyObject::_old_sweep_mark)
                    p = (*spit3)->object->getKey();
                else
                    p = (*spit3)->label;

                // printf("[%s]<->[%s]\n", l.c_str(), p.c_str());
                if (!l.empty() && !p.empty() && l == p && (*pit3)->object != (*spit3)->object) {
                    // printf("Found\n");
                    pit--;
                    spit--;
                    if (pit == path.begin()) {
                        local_root = NULL;
                        goto out0;
                    }
                    auto ppit = pit;
                    ppit--;
                    if (ppit == path.begin())
                        local_root = NULL;
                    else
                        local_root = *ppit;
                    // printf("Restart\n");
                    goto restart;
                }
                pit3++;
                spit3++;
            }
        }
    }
out0:
    // PF("local_root %p", local_root);
    for (; pit != path.end(); pit++) {
        string l;

        if ((*pit)->object->temporary_label_sweep_mark == HarmonyObject::_old_sweep_mark)
            l = (*pit)->object->getKey();
        else
            l = (*pit)->label;
        // PF("%d [%s]", spath.empty(), l.c_str());
        if (spath.empty() && local_root)
            spath = l;
        else
            spath = spath + string(".") + l;
    }
    return spath;
}

void HarmonyObject::copy(HarmonyObject *source)
{
    switch (type) {
        case Type::ELEMENT:
            element_type.removeReference();
            break;
        case Type::PROXY:
            link(NULL);
            break;
        default:
            break;
    }
    type = source->type;
    switch (type) {
        case Type::ELEMENT:
            element_value = source->element_value;
            element_type.setReference(source->element_type.object);
            break;
        case Type::TYPE:
            type_lower = source->type_lower;
            type_higher = source->type_higher;
            break;
        case Type::PROXY:
            link(source->proxy.object);
            break;
        default:
            break;
    }
}

HarmonyObject * HarmonyObject::clone() {
    HarmonyObject *object = new HarmonyObject(type);
    switch (type) {
        case Type::ELEMENT:
            object->element_value = element_value;
            object->element_type.setReference(element_type.object);
            break;
        case Type::TYPE:
            object->type_lower = type_lower;
            object->type_higher = type_higher;
            break;
        case Type::PATTERN:
            object->source.setReference(source.object);
            object->destination.setReference(destination.object);
            object->relation.setReference(relation.object);
            object->pattern_owner.setReference(pattern_owner.object);
            break;
        default:
            break;
    }
    return object;
}

// string HarmonyRelation::getPath(HarmonyObject *start)
// {
//     // HarmonyObject *o;
//     // HarmonyItem *i;
//     string path;

//     // if (!global_name.empty())
//     //     return global_name;
//     // o = this;

//     // i = NULL;

//     // path = string(".") + label;

//     // for (i = sweep_parent; i; i = i->parent->sweep_parent) {
//     //     if (i->label.empty()) {
//     //         path = o->getGlobalName() + path;
//     //         break;
//     //     }
//     //     path = string(".") + i->label + path;
//     //     if (!i->parent->global_name.empty()) {
//     //         path = i->parent->global_name + path;
//     //         break;
//     //     }
//     //     o = i->parent;
//     // }
//     return path;
// }

HarmonyObject * HarmonyRelation::clone() {
    auto r = new HarmonyRelation;
    r->relation.setReference(relation.object);
    r->source.setReference(source.object);
    r->destination.setReference(destination.object);
    return r;
}

void HarmonyObject::link(HarmonyObject *object)
{
    assert(isProxy());
    if (proxy.object) { // unlink
        // PF("UNLINK %p -> %p", this, reference.object);
        proxy.removeReference();
        proxy.parent = NULL;
    }
    if (object) {
        // PF("LINK %p -> %p", this, object);
        proxy.parent = this;
        proxy.setReference(object, true);
        object->updateDistance(object);
    }
    // PF("DONE %p", this);
}

bool HarmonyObject::compare(HarmonyObject *object)
{
    if (type != object->type)
        return false;
    if (isElement() && element_type.object == object->element_type.object && element_value == object->element_value)
        return true;
    return false;
}

HarmonyObject * HarmonyObject::getObject()
{
    if (isProxy())
        return proxy.object;
    return this;
}

HarmonyDB::HarmonyDB()
{
}

HarmonyDB::~HarmonyDB()
{
    getRoot()->clearRelations();
    getRoot()->clear();
    setRoot(NULL);
}

#define PRINT_CONFIG(fmt, ...) pbs.print_config(fmt, ##__VA_ARGS__)
//fprintf(config_file, fmt, ##__VA_ARGS__)
#define PRINT_CONFIG_COLORING(fmt, ...)
//fprintf(config_file, fmt, ##__VA_ARGS__)

static FILE * createConfigFile(const string &filepath)
{
    size_t e = 0;

    for (;;) {
        auto n = filepath.find("/", e);
        if (n == std::string::npos)
            break;
        e = n + 1;
        auto token = filepath.substr(0, n);
        // PF("[%s]\n", token.c_str());
        mkdir(token.c_str(), 0755);
    }
    // PF("[%s]\n", filepath.c_str());
    auto f = fopen(filepath.c_str(), "w");
    assert(f);
    return f;
}

void HarmonyDB::dumpBase(HarmonyObject *object, string const &filepath)
{
    FILE *config_file;

    if (filepath.empty())
        config_file = stdout;
    else {
        config_file = createConfigFile(filepath + "/root.hdb");
    }
    // PF(" csm %x", HarmonyObject::_current_sweep_mark);
    sweep(root.object);
    findTemporaryLabels(root.object);
    // PF("dumpBase");
    START_SWEEP
    dumpBase(object ? object: root.object, 0, false, true, string(), filepath, config_file);
    FINISH_SWEEP
    // PF("dumped");
    if (!filepath.empty())
        fclose(config_file);
}

static string sprint(const char *fmt, ...)
{
    static char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return string(buf);
}

struct print_base_state {
    FILE *config_file;
    string header_to_print;

    void print_config(const char *fmt, ...) {
        if (!header_to_print.empty()) {
            fprintf(config_file, "%s", header_to_print.c_str());
            header_to_print.clear();
        }
        va_list ap;
        va_start(ap, fmt);
        vfprintf(config_file, fmt, ap);
        va_end(ap);
    }
    void indent(int level) {
        while (level > 0) {
            print_config("    ");
            level--;
        }
    }
    void indent2(int level) {
        while (level > 0) {
            header_to_print += "    ";
            level--;
        }
    }
    void dumpHints(const map<string, string> &hints) {
        if (!hints.empty()) {
            for (auto h: hints) {
                if (h.first != HINT_BACKEND && h.first != HINT_FILEPATH)
                    print_config("#\"%s\":\"%s\"", h.first.c_str(), h.second.c_str());
            }
        }
    }
};


void HarmonyDB::dumpBase(HarmonyObject *object, int indent_level, bool dont_indent, bool primary, string const &label, string const &filepath, FILE *config_file)
{
    struct print_base_state pbs;
    pbs.config_file = config_file;

    bool add_space = false;
    bool add_newline = false;
    bool add_comma = false;

    object->sweep_mark = object->_current_sweep_mark;

    if (!dont_indent)
        pbs.indent2(indent_level);
    if (!label.empty()) {
        pbs.header_to_print += sprint("%s: ", label.c_str());
        // PRINT_CONFIG("%s: ", label.c_str());
    }
    PRINT_CONFIG_COLORING("\e[32m{%p:%c:%d:%d:  R:%p:%p}\e[0m ", object, primary ? 'P': 'p',
        object->root_distance, object->reference.structural_references,
        object->context, object->parent_receiver);
    // pbs.header_to_print += sprint("\e[32m{%p:%c:%d:%d:  R:%p:%p}\e[0m ", object, object->has_primary ? (primary ? 'P': 'p'): '?',
        // object->root_distance, object->reference.structural_references, object->context, object->parent_receiver);
    // PRINT_CONFIG_COLORING("\e[32m{%p:%c:%d:%d:  R:%p:%p}\e[0m ", object, object->has_primary ? (primary ? 'P': 'p'): '?',
    //     object->root_distance, object->reference.structural_references, object->context, object->parent_receiver);

    if (object->isProxy()) {
        if (object->proxy.object) {
            auto o = object->proxy.object;
            PRINT_CONFIG("$ ");
            if ((o->has_primary && !object->proxy.primary) || (!o->has_primary && (o->root_distance <= object->root_distance // don't move closer to root
               || o->sweep_mark == o->_current_sweep_mark))) {
                PRINT_CONFIG_COLORING("\e[93m{%p:%c:%d:%d}\e[0m ", o,  o->has_primary ? (object->proxy.primary ? 'P': 'p'): '-',
                    o->root_distance, o->reference.structural_references);
                PRINT_CONFIG("%s", o->getPath(object).c_str());
            } else {
                dumpBase(o, indent_level + 1, true, object->proxy.primary, string(), filepath, config_file);
            }
            // PRINT_CONFIG("$aa");//%s", o->reference.object->getPath().c_str());
        } else
            PRINT_CONFIG("$");
    } else if (object->isElement()) {
        PRINT_CONFIG("%s[%ld]", object->element_type.object->getPath(object).c_str(), object->element_value);
        add_space = true;
    } else if (object->isType()) {
        PRINT_CONFIG("<%ld, %ld>", object->type_lower, object->type_higher);
        add_space = true;
    } else if (object->isCode() && object->type != HarmonyObject::Type::PATTERN) {
        const char codes[] = "_ETP?*=+-!<>^~";
        PRINT_CONFIG("%c", codes[object->type]);
        add_space = true;
    } else if (object->isEmpty() && object->relations.next == &object->relations && object->type != HarmonyObject::Type::PATTERN) {
        PRINT_CONFIG("_");
    }

    if (!object->isEmpty() || object->relations.next != &object->relations) {
        HarmonyItem *item;

        if (add_space)
            pbs.header_to_print = " ";
        pbs.header_to_print += ("(\n");

        for (item = object->items.next; item != &object->items; item = item->next) {
            string name;

            if ((item->object->has_primary && !item->primary) ||
                (!item->object->has_primary && (item->object->root_distance <= object->root_distance // don't move closer to root
               || item->object->sweep_mark == item->object->_current_sweep_mark))) {
                if (add_comma) {
                    add_comma = false;
                    PRINT_CONFIG(",");
                }
                if (add_newline) {
                    add_newline = false;
                    PRINT_CONFIG("\n");
                }
                pbs.indent(indent_level + 1);
                if (!item->label.empty()) {
                    PRINT_CONFIG("%s: ", item->label.c_str());
                }
                PRINT_CONFIG_COLORING("\e[32m{%p:%c:%d:%d:  R:%p:%p}\e[0m ", item->object, item->object->has_primary ? (item->primary ? 'P': 'p'): '-',
                    item->object->root_distance, item->object->reference.structural_references,
                    item->object->context, item->object->parent_receiver);
                PRINT_CONFIG("%s", item->object->getPath(object).c_str());
                add_comma = true;
                add_newline = true;
            } else {
                if (item->object->temporary_label_sweep_mark == HarmonyObject::_old_sweep_mark) {
                    name = string(".") + item->object->getKey();
                } else if (!item->label.empty()) {
                    name = item->label;
                }

                auto hint_backend = item->object->getHint(HINT_BACKEND);
                auto hint_filepath = item->object->getHint(HINT_FILEPATH);

                if (config_file != stdout && hint_backend == "file") {
                    FILE *new_config_file = config_file;
                    PF("SETTING TARGET FILE to %s\n", hint_filepath.c_str());
                    new_config_file = createConfigFile(filepath + hint_filepath);
                    dumpBase(item->object, 0, false, item->primary, name, filepath, new_config_file);
                    fclose(new_config_file);
                } else {
                    if (add_comma) {
                        add_comma = false;
                        PRINT_CONFIG(",");
                    }
                    if (add_newline) {
                        add_newline = false;
                        PRINT_CONFIG("\n");
                    }
                    // pbs.indent(indent_level + 1);
                    PRINT_CONFIG("");
                    dumpBase(item->object, indent_level + 1, false, item->primary, name, filepath, config_file);
                    add_comma = true;
                    add_newline = true;
                }
            }

            // if (item->next != &object->items || !object->relations.isEmpty())
            //     PRINT_CONFIG(",\n");
            // else
            //     PRINT_CONFIG("\n");
        }
        add_comma = false;
        if (add_newline) {
            add_newline = false;
            PRINT_CONFIG("\n");
        }
        for (auto item = object->relations.next; item != &object->relations; item = item->next) {
            string name;
            HarmonyRelation *r = static_cast<HarmonyRelation *>(item->object);

            if (r->temporary_label_sweep_mark == HarmonyObject::_old_sweep_mark) {
                name = string(".") + r->getKey();
            } else if (!item->label.empty()) {
                name = r->label;
            }

            pbs.indent(indent_level + 1);
            if (!name.empty()) {
                PRINT_CONFIG("%s: ", name.c_str());
            }
            PRINT_CONFIG_COLORING("\e[32m{%p}\e[0m ", r);
            PRINT_CONFIG("[\n");
            pbs.indent(indent_level + 2);

            PRINT_CONFIG_COLORING("\e[32m{%p:%d:%d}\e[0m ", r->relation.object, r->relation.object->root_distance, r->relation.object->reference.structural_references);
            PRINT_CONFIG("%s,\n", r->relation.object->getPath(item->object).c_str());

            pbs.indent(indent_level + 2);
            PRINT_CONFIG_COLORING("\e[32m{%p:%d:%d}\e[0m ", r->source.object, r->source.object->root_distance, r->source.object->reference.structural_references);
            PRINT_CONFIG("%s,\n", r->source.object->getPath(item->object).c_str());

            pbs.indent(indent_level + 2);
            PRINT_CONFIG_COLORING("\e[32m{%p:%d:%d}\e[0m ", r->destination.object, r->destination.object->root_distance, r->destination.object->reference.structural_references);
            PRINT_CONFIG("%s\n", r->destination.object->getPath(item->object).c_str());

            pbs.indent(indent_level + 1);
            PRINT_CONFIG("]");

            pbs.dumpHints(r->hints);

            if (item != &object->relations)
                    // add_comma = true;
                    // add_newline = true;
                PRINT_CONFIG(",\n");
            else
                PRINT_CONFIG("\n");
        }
        pbs.indent(indent_level);
        PRINT_CONFIG(")");
    } else if (object->type == HarmonyObject::Type::PATTERN) {
        PRINT_CONFIG("[\n");
        pbs.indent(indent_level + 1);

        // PRINT_CONFIG(" {%p:%d:%d} ", object->relation.object, object->relation.object->root_distance, object->relation.object->reference.structural_references);
        PRINT_CONFIG("%s,\n", object->relation.object->getPath(object).c_str());

        pbs.indent(indent_level + 1);
        // PRINT_CONFIG(" {%p:%d:%d} ", object->source.object, object->source.object->root_distance, object->source.object->reference.structural_references);
        PRINT_CONFIG("%s,\n", object->source.object->getPath(object).c_str());

        pbs.indent(indent_level + 1);
        // PRINT_CONFIG(" {%p:%d:%d} ", object->destination.object, object->destination.object->root_distance, object->destination.object->reference.structural_references);
        PRINT_CONFIG("%s,\n", object->destination.object->getPath(object).c_str());

        pbs.indent(indent_level + 1);
        // PRINT_CONFIG(" {%p:%d:%d} ", object->pattern_owner.object, object->pattern_owner.object->root_distance, object->pattern_owner.object->reference.structural_references);
        PRINT_CONFIG("%s\n", object->pattern_owner.object->getPath(object).c_str());

        pbs.indent(indent_level);
        PRINT_CONFIG("]");
    }

    pbs.dumpHints(object->hints);

    if (indent_level == 0)
        PRINT_CONFIG("\n");
}

void HarmonyDB::clear()
{
    PF();
    // if (root.object)
    //     deleteNode(root.object);
    // assert(root.object == NULL);
}

void HarmonyDB::deleteNode(HarmonyObject *object)
{
    // PF("%p", object);
    // for (auto it : object->relations) {
    //     delete it;
    // }
    // for (auto i = object->first(); i; i->nextItem(object)) {
    //     if (i->reference.object->parent == object)
    //         deleteNode(i->reference.object);
    // }
    clear();
}

HarmonyObject * HarmonyDB::createContext(HarmonyObject *source, string name, HarmonyObject *return_object, HarmonyObject *arg)
{
    HarmonyObject *contexts, *ctx;
    HarmonyObjectPath path;

    path.push_back("context");
    contexts = getObjectByPath(path);
    if (!contexts) {
        contexts = new HarmonyObject;
        getRoot()->add(contexts, "context", true);
        contexts->hints[HINT_BACKEND] = HINT_BACKEND_FILE;
        contexts->hints[HINT_FILEPATH] = "/context.hdb";
    }

    ctx = new HarmonyObject;
    contexts->add(ctx, name, true);
    if (source) {
        HarmonyObject *no;
        {
            START_SWEEP
            // PF("clone %d", HarmonyObject::_current_sweep_mark);
            no = cloneObject(source, ctx, "root");
            FINISH_SWEEP
            // ooo = HarmonyObject::_old_sweep_mark;
        }
// dumpBase();
        {
            START_SWEEP
            // HarmonyObject::_old_sweep_mark = ooo;
            // PF("fillCloned %d", HarmonyObject::_current_sweep_mark);
            fillClonedObject(no, ctx);
            FINISH_SWEEP
        }

        auto named = no->first();
        assert(named);
        auto unnamed = named->nextItem(no);
        assert(unnamed);
        auto body = unnamed->nextItem(no);
        if (!body) { // no unnamed arguments
            body = unnamed;
            unnamed = NULL;
        }

        copyArgument(arg, named->object, unnamed ? unnamed->object: NULL, return_object);

        auto ip = new HarmonyObject(HarmonyObject::Type::PROXY);
        ctx->add(ip, "ip", true);
        auto ip_stack = new HarmonyObject;
        ctx->add(ip_stack, "ip_stack", true);
        ip->link(body->object);
    } else { // link root
        auto p = new HarmonyObject(HarmonyObject::Type::PROXY);
        ctx->add(p, "root", true);
        p->link(getRoot());
    }
    // dumpBase();
    return ctx;
}

HarmonyObject * HarmonyDB::cloneObject(HarmonyObject *source, HarmonyObject *parent, string label, bool primary)
{
    HarmonyObject *object;

    object = source->clone();
    // PF("%p -> %p", source, object);
    source->sweep_mark = HarmonyObject::_current_sweep_mark;
    source->sweep_object = object;
    if (parent) {
        if (parent->isProxy())
            parent->link(object);
        else
            parent->add(object, label, primary);
    }

    if (source->isProxy()) {
        if (source->proxy.object) {//&& source->proxy.object->reference.structural_references == 1) {
            cloneObject(source->proxy.object, object);
        }
    }

    auto r = source->relations.next;
    while (r != &source->relations) {
        HarmonyRelation *nr = static_cast<HarmonyRelation *>(r->object->clone());
        object->addRelation(nr);
        r->object->sweep_object = nr;
        // PF("%p -> %p", r->object, nr);
        r = r->next;
    }

    auto o = source->first();
    unsigned count = 0;
    for (; o != NULL; o = o->nextItem(source)) {
        if (count == 0 && source->isSend() && !o->object->isProxy()) { // don't clone the receiver
            object->add(o->object, o->label);
            o->object->sweep_mark = 0;
        } else {
            if (o->object->sweep_mark != HarmonyObject::_current_sweep_mark) {
                cloneObject(o->object, object, o->label, o->primary);
            } else {
                object->add(o->object->sweep_object, o->label, o->primary);
            }
        }
        count++;
    }
    return object;
}

void HarmonyDB::fillClonedObject(HarmonyObject *object, HarmonyObject *context, HarmonyObject *parent_receiver)
{
    // PF("%p:%d ctx:%p  parent_receiver:%p    %d:%d", object, object->type, context, parent_receiver, HarmonyObject::_old_sweep_mark, HarmonyObject::_current_sweep_mark);
    object->sweep_mark = HarmonyObject::_current_sweep_mark;

    if (object->type == HarmonyObject::Type::RECEIVE && context) {
        object->context = context;
    }
    if (object->isProxy()) {
        if (object->proxy.object && object->proxy.object->sweep_mark == HarmonyObject::_old_sweep_mark) {
            PF("RELINK %p: %p -> %p  %d:%d", object, object->proxy.object, object->proxy.object->sweep_object, object->sweep_mark, object->proxy.object->sweep_mark);
            // assert(0);
            // object->link(object->proxy.object->sweep_object);
        } else if (object->proxy.object) {
            fillClonedObject(object->proxy.object, context, NULL);
        }
    } else if (object->type == HarmonyObject::Type::PATTERN) {
        // PF("old:%d  current:%d", HarmonyObject::_old_sweep_mark, HarmonyObject::_current_sweep_mark);
        // PF("%p:%d %p %p", object, object->type, context, parent_receiver);
        if (object->relation.object->sweep_mark == HarmonyObject::_old_sweep_mark) {
            auto no = object->relation.object->sweep_object;
            object->relation.removeReference();
            object->relation.setReference(no);
        }
        // PF("%p %p  %d", object->source.object, object->source.object->sweep_object, object->source.object->sweep_mark);
        if (object->source.object->sweep_mark == HarmonyObject::_old_sweep_mark) {
            auto no = object->source.object->sweep_object;
            object->source.removeReference();
            object->source.setReference(no);
        }
        // PF("%p %p  %d", object->destination.object, object->destination.object->sweep_object, object->destination.object->sweep_mark);
        if (object->destination.object->sweep_mark == HarmonyObject::_old_sweep_mark) {
            auto no = object->destination.object->sweep_object;
            object->destination.removeReference();
            object->destination.setReference(no);
        }
        // PF("%p %p  %d", object->pattern_owner.object, object->pattern_owner.object->sweep_object, object->pattern_owner.object->sweep_mark);
        if (object->pattern_owner.object->sweep_mark == HarmonyObject::_old_sweep_mark) {
            auto no = object->pattern_owner.object->sweep_object;
            object->pattern_owner.removeReference();
            object->pattern_owner.setReference(no);
        }
    }
    auto ri = object->relations.next;
    while (ri != &object->relations) {
        auto r = static_cast<HarmonyRelation *>(ri->object);
        if (r->relation.object->sweep_mark == HarmonyObject::_old_sweep_mark) {
            auto no = r->relation.object->sweep_object;
            r->relation.removeReference();
            r->relation.setReference(no);
        }
        if (r->source.object->sweep_mark == HarmonyObject::_old_sweep_mark) {
            auto no = r->source.object->sweep_object;
            r->source.removeReference();
            r->source.setReference(no);
        }
        if (r->destination.object->sweep_mark == HarmonyObject::_old_sweep_mark) {
            auto no = r->destination.object->sweep_object;
            r->destination.removeReference();
            r->destination.setReference(no);
        }
        ri = ri->next;
    }

    auto o = object->first();
    unsigned count = 0;

    for (; o != NULL; o = o->nextItem(object)) {
        if (o->object->sweep_mark != HarmonyObject::_current_sweep_mark && (count != 0 || !object->isSend() || o->object->isProxy())) {
            fillClonedObject(o->object, context, NULL);
        } else  if (o->object->isNul()) {
            o->object->loop = true;
        }
        if (object->type == HarmonyObject::Type::RECEIVE) {
            o->object->parent_receiver = object;
        }
        count++;
    }
}

HarmonyObject * HarmonyDB::cloneArgument(HarmonyObject *source, HarmonyObject *parent)
{
    HarmonyObject *object;

    if (parent) {
        object = parent;
    } else {
        if (source->getObject()->isType())
            object = source->getObject();
        else
            object = source->getObject()->clone();
    }

    // PF("%p -> %p", source, object);
    source->sweep_object = object;
    source->sweep_mark = HarmonyObject::_current_sweep_mark;

    auto r = source->relations.next;
    while (r != &source->relations) {
        HarmonyRelation *nr = static_cast<HarmonyRelation *>(r->object->clone());
        object->addRelation(nr);
        PF("%p -> %p", r->object, nr);
        r->object->sweep_object = nr;
        r = r->next;
    }

    auto o = source->first();
    for (; o != NULL; o = o->nextItem(source)) {
        if (o->object->sweep_mark != HarmonyObject::_current_sweep_mark) {
            cloneObject(o->object, object, o->label);
        } else {
            object->add(o->object->sweep_object, o->label);
        }
    }
    return object;
}


void HarmonyDB::copyArgument(HarmonyObject *source, HarmonyObject *named, HarmonyObject *unnamed, HarmonyObject *return_object)
{
    // PF("start");
    // PF("src:%p  pattern:%p  rcvr:%p", source, pattern, retun_object);
    auto i = source->first();

    for (; i != NULL; i = i->nextItem(source)) {
        HarmonyItem *arg;

        if (!i->label.empty() && (arg = named->findItem(i->label))) {
            assert(arg->object->isProxy());
            // PF("[%s]", i->label.c_str());
            if (i->label != "return") {
                arg->object->link(cloneArgument(i->object->getObject()));
            } else {
                assert(!return_object);
                // if (return_object) {
                    // PF("[%s]", i->label.c_str());
                    // arg->object->link(return_object);
                // } else {
                    arg->object->link(i->object->getObject());
                // }
            }
        } else if (unnamed) {
            // PF("[%s]", i->label.c_str());
            unnamed->add(cloneArgument(i->object->getObject()), i->label);
        }
    }
    if (return_object) {
        auto r = named->findItem("return");
        assert(r && r->object->isProxy());
        r->object->link(return_object);
    }
    // PF("filling");
    START_SWEEP
    fillClonedObject(named, NULL);
    if (unnamed)
        fillClonedObject(unnamed, NULL);
    FINISH_SWEEP
    // PF("done");
}

bool HarmonyDB::clearArguments(HarmonyObject *receiver, unsigned level)
{
    unsigned count = 0;
    bool more = false;

    auto named = receiver->first();
    auto unnamed = named->nextItem(receiver);

// PF("%p %p lvl:%d", named, unnamed, level);
    auto o = named->object->first();

    for (; o != NULL; o = o->nextItem(named->object)) {
        // PF("%p %d %d", o->object, o->object->isProxy(), level);
        if (o->object->isProxy())
            o->object->link(NULL);
        else {
            if (!clearArguments(o->object, level + 1) && level == 0)
                count++;
            more = true;
        }
    }
    if (unnamed && unnamed->object) {
        unnamed->object->clear();
    }
    if (level == 0) {
        receiver->receiver_armed = count ? count: 1;
        // PF("Arming %d", receiver->receiver_armed);
        receiver->receiver_got = 0;
    }
    return more;
}

unsigned HarmonyObject::getItems(unsigned size, HarmonyObject *items[])
{
    unsigned i;
    auto *item = first();

    for (i = 0; i < size && item; i++) {
        items[i] = item->object;
        item = item->nextItem(this);
    }
    return i;
}
