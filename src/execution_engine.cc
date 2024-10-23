#include "execution_engine.h"

ExecutionEngine::ExecutionEngine(HarmonyDB *db) : db(db)
{
    HarmonyObject *relation;
    HarmonyItem *relationi;
    HarmonyItem *relationi_next, *relationi_prev, *relationi_me, *relationi_type;
    HarmonyItem *relationi_first, *relationi_last, *relationi_proxy, *relationi_label;

// find intrinsic relations
    relationi = db->getRoot()->findItem("relation");
    if (!relationi) {
        relation = new HarmonyObject;
        db->getRoot()->add(relation, "relation");
    } else
        relation = relationi->object;

    relationi_next = relation->findItem("next");
    if (!relationi_next) {
        relation_next = new HarmonyObject;
        relation->add(relation_next, "next");
    } else
        relation_next = relationi_next->object;

    relationi_prev = relation->findItem("prev");
    if (!relationi_prev) {
        relation_prev = new HarmonyObject;
        relation->add(relation_prev, "prev");
    } else
        relation_prev = relationi_prev->object;

    relationi_me = relation->findItem("me");
    if (!relationi_me) {
        relation_me = new HarmonyObject;
        relation->add(relation_me, "me");
    } else
        relation_me = relationi_me->object;

    relationi_type = relation->findItem("type");
    if (!relationi_type) {
        relation_type = new HarmonyObject;
        relation->add(relation_type, "type");
    } else
        relation_type = relationi_type->object;

    relationi_first = relation->findItem("first");
    if (!relationi_first) {
        relation_first = new HarmonyObject;
        relation->add(relation_first, "first");
    } else
        relation_first = relationi_first->object;

    relationi_last = relation->findItem("last");
    if (!relationi_last) {
        relation_last = new HarmonyObject;
        relation->add(relation_last, "last");
    } else
        relation_last = relationi_last->object;

    relationi_proxy = relation->findItem("proxy");
    if (!relationi_proxy) {
        relation_proxy = new HarmonyObject;
        relation->add(relation_proxy, "proxy");
    } else
        relation_proxy = relationi_proxy->object;

    relationi_label = relation->findItem("label");
    if (!relationi_label) {
        relation_label = new HarmonyObject;
        relation->add(relation_label, "label");
    } else
        relation_label = relationi_label->object;

}

void ExecutionEngine::sendMessage(HarmonyObject *recipient, HarmonyObject *arg, HarmonyObject *return_object)
{
    assertf(recipient->isCode(), "Recipient is not HarmonyCode!");
    assertf(recipient->type == HarmonyObject::Type::LAUNCH || recipient->type == HarmonyObject::Type::RECEIVE, "Receiver is not launcher nor receiver!");
    if (recipient->type == HarmonyObject::Type::LAUNCH) { // launcher, new context & thread
        PT("Launcher %p found", recipient);
        auto ctx = db->createContext(recipient, string(), return_object, arg);
        // db->dumpBase();
        run_queue.push(ctx);
        run();
    } else if (recipient->type == HarmonyObject::Type::RECEIVE) { // launcher, new context & thread
        PT("Receiver %p found", recipient);
    } else {
        assert(0);
    }
}

bool ExecutionEngine::pushFrame(HarmonyObject *frame)
{
    if (!frame->isEmpty()) {
        // PT("new stack %p %d", frame, frame->loop);
        if (frame->loop) {
            auto si = ip_stack->first();
            for ( ; si; si = si->nextItem(ip_stack)) {
                if (si->object == frame) {
                    // PT("found");
                    si = si->nextItem(ip_stack);
                    while (si) {
                        // PT("removing %p", si->object);
                        si = ip_stack->remove(si);
                    }
                    return true;
                }
            }
        }
        ip_stack->add(frame);
        ip->link(frame->first()->object);
        return true;
    }
    return false;
}

void ExecutionEngine::run()
{
    HarmonyObject *contexts;

    contexts = db->getRoot()->findItem("context")->object;
again:
    PT("rq:%ld  wq:%ld", run_queue.size(), wait_queue.size());
    if (run_queue.empty())
        return;
    ctx = run_queue.front();
    ip = ctx->findItem("ip")->object;
    assert(ip->isProxy());
    ip_stack = ctx->findItem("ip_stack")->object;

    PT("ctx:%p  ip:%p  ip_stack:%p", ctx, ip, ip_stack);
    for (;;) {
        current_ip = ip->getObject();
        PT(" current_ip:%p type:%d loop:%d", current_ip, current_ip->type, current_ip->loop);
        if (current_ip->isCode()) {
            const char codes[] = "_ETP?*=+-!<>^~";
            PT("Code: %c", codes[current_ip->type]);
            if (current_ip->type == HarmonyObject::Type::MATCH) {
                HarmonyObject *pattern, *unknowns, *negatives, *cont;

                auto pattern_item = current_ip->first();
                pattern = pattern_item->object;

                auto unknowns_item = pattern_item->nextItem(current_ip);
                unknowns = unknowns_item->object;

                auto negatives_item = unknowns_item->nextItem(current_ip);
                negatives = negatives_item->object;

                auto cont_item = negatives_item->nextItem(current_ip);
                cont = NULL;
                if (cont_item) {
                    cont = cont_item->object;
                }
                auto b = execute_match(pattern, unknowns, negatives);
                PT("b:%d  cont:%p", b, cont);
                if (!cont && !b) {
                    assertf(0, "MATCH NOT MET!");
                }
                if (cont && b) {
                    if (pushFrame(ip->getObject()) && pushFrame(cont->getObject())) {
                        continue;
                    }
                    break;
                }
            } else if (current_ip->type == HarmonyObject::Type::CREATE) {
                HarmonyObject *dest, *new_object;

                assert(!current_ip->isEmpty());
                dest = current_ip->first()->object;
                assert(dest->isProxy());
                new_object = new HarmonyObject;
                dest->link(new_object);
            } else if (current_ip->type == HarmonyObject::Type::ASSIGN) {
                HarmonyObject *dest, *src;

                assert(!current_ip->isEmpty());

                auto dest_item = current_ip->first();
                dest = dest_item->object;
                assert(dest->isProxy());

                auto src_item = dest_item->nextItem(current_ip);
                assert(src_item);
                src = src_item->object->getObject();
                // PT("%p:%ld -> %p:%ld", src, src->element_value, dest->getObject(), dest->getObject()->element_value);
                dest->getObject()->copy(src);
                // PT("%p:%ld -> %p:%ld", src, src->element_value, dest->getObject(), dest->getObject()->element_value);
            } else if (current_ip->type == HarmonyObject::Type::ADD) {
                HarmonyObject *set, *object;

                assert(!current_ip->isEmpty());

                auto set_item = current_ip->first();
                set = set_item->object->getObject();

                auto object_item = set_item->nextItem(current_ip);
                object = object_item->object->getObject();
                set->add(object);
            } else if (current_ip->type == HarmonyObject::Type::REMOVE) {
                HarmonyObject *set, *object;

                assert(!current_ip->isEmpty());

                auto set_item = current_ip->first();
                set = set_item->object->getObject();

                auto object_item = set_item->nextItem(current_ip);
                object = object_item->object->getObject();
                auto item = set->findItem(object);
                if (item)
                    set->remove(item);
            } else if (current_ip->type == HarmonyObject::Type::RECEIVE) {
                PT("%p ra:%d rg:%d", current_ip, current_ip->receiver_armed, current_ip->receiver_got);
                if (!current_ip->receiver_armed) {
                    run_queue.pop();
                    wait_queue.push_back(ctx);
                    db->clearArguments(current_ip);
                    goto again;
                } else if (current_ip->receiver_armed != current_ip->receiver_got) {
                    run_queue.pop();
                    wait_queue.push_back(ctx);
                    goto again;
                }
                current_ip->receiver_armed = 0;
                current_ip->receiver_got = 0;
            } else if (current_ip->type == HarmonyObject::Type::SEND) {
                HarmonyItem *receiver_item, *argument_item;
                receiver_item = current_ip->first();
                if (!receiver_item) { // stop
                    run_queue.pop();
                    PT("DELETE context");
                    auto i = contexts->findItem(ctx);
                    assert(i);
                    // contexts->remove(i);
                    goto again;
                }
                argument_item = receiver_item->nextItem(current_ip);

                HarmonyObject *receiver, *argument;
                receiver = receiver_item->object->getObject();
                argument = argument_item->object->getObject();
                PT("Sending %p to %p...", argument, receiver);

                if (receiver->type == HarmonyObject::LAUNCH) {
                    auto launcher = receiver;

                    auto return_object = argument->findItem("return")->object->getObject();

                    // 0 - launcher args
                    // 1 - launcher body
                    PT("launcher %p", launcher);
                    if (argument && return_object) {
                        auto ctx = db->createContext(launcher, string(), NULL/*return_object*/, argument);
                        run_queue.push(ctx);
                    }
                } else if (receiver->type == HarmonyObject::RECEIVE) {
                    HarmonyObject *rctx = NULL;
                    rctx = receiver->context;
                    PT("receiver:%p  rctx:%p", receiver, rctx);
                    assert(argument);

                    auto named = receiver->first();
                    assert(named);
                    auto unnamed = named->nextItem(receiver);

                    db->copyArgument(argument, named->object, unnamed ? unnamed->object: NULL);

                    if (rctx) {
                        for (auto it: wait_queue) {
                            if (it == rctx) {
                                wait_queue.remove(rctx);
                                run_queue.push(rctx);
                                PT("wq -> rq");
                                break;
                            }
                        }
                    }
                    receiver->receiver_got = 1;
                } else if (receiver->parent_receiver) {
                    HarmonyObject *rctx = NULL;
                    rctx = receiver->parent_receiver->context;
                    // PT("receiver:%p  rctx:%p  ra:%d rg:%d", receiver, rctx, receiver->parent_receiver->receiver_armed, receiver->parent_receiver->receiver_got);
                    assert(receiver->parent_receiver->receiver_armed > 0);
                    if (argument && receiver->parent_receiver->receiver_armed > 0) {
                        assert(receiver->parent_receiver->receiver_got < receiver->parent_receiver->receiver_armed);
                        db->copyArgument(argument, receiver);
                        receiver->parent_receiver->receiver_got++;
                    }
                    // PT("receiver:%p  rctx:%p  ra:%d rg:%d", receiver, rctx, receiver->parent_receiver->receiver_armed, receiver->parent_receiver->receiver_got);
                    if (rctx && receiver->parent_receiver->receiver_armed == receiver->parent_receiver->receiver_got) {
                        for (auto it: wait_queue) {
                            if (it == rctx) {
                                wait_queue.remove(rctx);
                                run_queue.push(rctx);
                                PT("wq -> rq");
                                break;
                            }
                        }
                    }
                } else {
                    db->dumpBase();
                    assert(0);
                }
            } else if (current_ip->type == HarmonyObject::Type::LINK) {
                HarmonyObject *proxy;

                assert(!current_ip->isEmpty());

                auto proxy_item = current_ip->first();
                proxy = proxy_item->object;

                if (proxy->isProxy()) {
                    HarmonyObject *object = NULL;
                    auto object_item = proxy_item->nextItem(current_ip);
                    if (object_item) {
                        object = object_item->object->getObject();
                    }
                    proxy->link(object);
                }
            } else if (current_ip->type == HarmonyObject::Type::RELATE) {
                PT("NOP");
            } else if (current_ip->isElement()) {
                if (current_ip->element_value == -1)
                    db->dumpBase();
            }
        } else if (current_ip->isProxy()) { // skip

        } else { // HarmonyObject, step into
            PT("Step into %p", ip->getObject());
            if (pushFrame(ip->getObject()))
                continue;
            break;
        }
        // next instruction
        PT("NEXT");
        auto current_ip = ip->getObject();
        for (;;) {
            auto *ip_frame = ip_stack->last();
            if (ip_frame) {
                auto next_ip = ip_frame->object->next(current_ip);
                PT("next_ip %p", next_ip);
                if (next_ip) {
                    PT("next_ip->object %p", next_ip->object);
                    if (next_ip->object->loop) {
                        bool found = false;
                        PT("loop");
                        auto si = ip_stack->first();
                        for ( ; si; si = si->nextItem(ip_stack)) {
                            PT("%p %p", si->object, next_ip->object);
                            if (si->object == next_ip->object) {
                                PT("found");
                                found = true;
                                si = si->nextItem(ip_stack);
                                while (si) {
                                    PT("removing %p", si->object);
                                    si = ip_stack->remove(si);
                                }
                                break;
                            }
                        }
                        if (found)
                            ip->link(next_ip->object->first()->object);
                        else
                            ip->link(next_ip->object);
                    } else
                        ip->link(next_ip->object);
                    break;
                } else { // step out
                    current_ip = ip_frame->object;
                    ip_stack->remove(ip_stack->findItem(ip_frame->object));
                }
            } else { // no more instructions, done
                run_queue.pop();
                PT("DELETE context");
                auto i = contexts->findItem(ctx);
                assert(i);
                // contexts->remove(i);
                goto again;
            }
        }
    }
    PT("Done");
}

bool ExecutionEngine::execute_match(HarmonyObject *pattern, HarmonyObject *unknowns, HarmonyObject *negatives)
{
    PT("MATCH %p:%d  %p:%d  %p:%d", pattern, pattern->isEmpty(), unknowns, unknowns->isEmpty(), negatives, negatives->isEmpty());

    // db->dumpBase();

    auto u = unknowns->first();
    for (; u != NULL; u = u->nextItem(unknowns)) {
        PT("Setting %p unknown", u->object);
        u->object->unknown = true;
    }

    auto n = negatives->first();
    for (; n != NULL; n = n->nextItem(negatives)) {
        PT("Setting %p negative", n->object);
        n->object->negative = true;
    }

    auto pi = pattern->first();
    for (; pi != NULL; pi = pi->nextItem(pattern)) {
        auto p = pi->object->getObject();
        if (p->isPattern()) {
            auto owner = p->pattern_owner.object->getObject();

            if (p->relation.object == relation_type) {
                auto o = p->source.object->getObject();
                PT("Relation TYPE %p", p->source.object);
                assert(o == p->pattern_owner.object->getObject());
                if (o->isElement()) {
                    if (p->destination.object->negative)
                        return false;
                    if (p->destination.object->unknown && p->destination.object->isProxy()) {
                        p->destination.object->link(o->element_type.object);
                    }
                } else {
                    return false;
                }
            } else if (p->relation.object == relation_prev) {
                auto o = p->source.object->getObject();
                auto t = p->pattern_owner.object->getObject();
                PT("Relation PREV %p %p %p", p->source.object, o, t);
                if (o->isElement() && t->isType()) {
                    assert(o->element_type.object == t);
                    if (o->element_value == t->type_lower)
                        return false;
                    if (p->destination.object->negative)
                        return false;
                    if (p->destination.object->unknown && p->destination.object->isProxy()) {
                        auto no = new HarmonyObject(HarmonyObject::ELEMENT);
                        no->element_type.setReference(t);
                        no->element_value = o->element_value - 1;
                        PT("linking %p to %p %ld", p->destination.object, no, no->element_value);
                        p->destination.object->link(no);
                    }
                } else {
                    auto ni = t->prev(o);
                    if (!ni)
                        return false;
                    if (p->destination.object->unknown && p->destination.object->isProxy()) {
                        p->destination.object->link(ni->object->getObject());
                    }
                }
            } else if (p->relation.object == relation_next) {
                auto o = p->source.object->getObject();
                auto t = p->pattern_owner.object->getObject();
                PT("Relation NEXT %p %p %p", p->source.object, o, t);
                if (o->isElement() && t->isType()) {
                    assert(o->element_type.object == t);
                    if (o->element_value == t->type_higher)
                        return false;
                    if (p->destination.object->negative)
                        return false;
                    if (p->destination.object->unknown && p->destination.object->isProxy()) {
                        auto no = new HarmonyObject(HarmonyObject::ELEMENT);
                        no->element_type.setReference(t);
                        no->element_value = o->element_value + 1;
                        p->destination.object->link(no);
                    }
                } else {
                    auto ni = t->next(o);
                    if (!ni)
                        return false;
                    if (p->destination.object->unknown && p->destination.object->isProxy()) {
                        p->destination.object->link(ni->object->getObject());
                    }
                }
            } else if (p->relation.object == relation_first) {
                auto o = p->source.object->getObject();
                auto t = p->pattern_owner.object->getObject();
                PT("Relation FIRST %p %p %p", p->source.object, o, t);
                if (o->isElement() && t->isType()) {
                    assert(o->element_type.object == t);
                    if (o->element_value == t->type_lower)
                        return false;
                    if (p->destination.object->negative)
                        return false;
                    if (p->destination.object->unknown && p->destination.object->isProxy()) {
                        auto no = new HarmonyObject(HarmonyObject::ELEMENT);
                        no->element_type.setReference(t);
                        no->element_value = t->type_lower;
                        PT("linking %p to %p %ld", p->destination.object, no, no->element_value);
                        p->destination.object->link(no);
                    }
                } else if (o == t) {
                    auto fi = o->first();
                    if (p->destination.object->negative) {
                        return fi == NULL;
                    }
                    if (p->destination.object->unknown && p->destination.object->isProxy()) {
                        p->destination.object->link(fi->object->getObject());
                    }
                } else {
                    return false;
                }
            } else if (p->relation.object == relation_last) {
                auto o = p->source.object->getObject();
                auto t = p->pattern_owner.object->getObject();
                PT("Relation LAST %p %p %p", p->source.object, o, t);
                if (o->isElement() && t->isType()) {
                    assert(o->element_type.object == t);
                    if (o->element_value == t->type_higher)
                        return false;
                    if (p->destination.object->negative)
                        return false;
                    if (p->destination.object->unknown && p->destination.object->isProxy()) {
                        auto no = new HarmonyObject(HarmonyObject::ELEMENT);
                        no->element_type.setReference(t);
                        no->element_value = t->type_higher;
                        PT("linking %p to %p %ld", p->destination.object, no, no->element_value);
                        p->destination.object->link(no);
                    }
                } else if (o == t) {
                    auto fi = o->last();
                    if (p->destination.object->negative) {
                        return fi == NULL;
                    }
                    if (p->destination.object->unknown && p->destination.object->isProxy()) {
                        p->destination.object->link(fi->object->getObject());
                    }
                } else {
                    return false;
                }
            } else if (p->relation.object == relation_me) {
                auto o = p->source.object->getObject();
                auto t = p->pattern_owner.object->getObject();
                auto d = p->destination.object->getObject();
                PT("Relation ME %p %p %p %p", p->source.object, o, t, d);
                // PT("%d %d %p %p %d ")
                if (o->isElement() && d->isElement()
                    && o->element_type.object->getObject() == d->element_type.object->getObject() && o->element_value == d->element_value)
                    return true;
                if (o->isType() && d->isType() && o == d)
                    return true;
                return false;
            } else {
                auto ri = owner->relations.next;
                while (ri != &owner->relations) {
                    auto r = static_cast<HarmonyRelation *>(ri->object);
                    PT("%p: %p %p %p %p", r, r->relation.object, p->relation.object->getObject(), r->source.object, p->source.object->getObject());
                    if (r->relation.object == p->relation.object->getObject() && r->source.object->compare(p->source.object->getObject())) {
                        // PT("FOUND %p %d:%d", p->destination.object, p->destination.object->unknown, p->destination.object->negative);
                        if (p->destination.object->negative)
                            return false;
                        if (p->destination.object->unknown && p->destination.object->isProxy()) {
                            // PT("linking %p", r->destination.object);
                            p->destination.object->link(r->destination.object);
                            break;
                        }
                    }
                    ri = ri->next;
                }
                if (ri == &owner->relations)
                    return false;
            }
        }
    }

    return true;
}
