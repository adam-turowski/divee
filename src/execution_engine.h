#ifndef EXECUTION_ENGINE_H
#define EXECUTION_ENGINE_H

#include "harmonydb.h"
#include <queue>

using namespace std;

struct ExecutionEngine
{
    HarmonyDB *db;
    queue<HarmonyObject *> run_queue;
    list<HarmonyObject *> wait_queue;

    HarmonyObject *relation_next, *relation_prev, *relation_me, *relation_type;
    HarmonyObject *relation_first, *relation_last, *relation_proxy, *relation_label;

    HarmonyObject *ctx, *ip_stack, *ip, *current_ip;

    ExecutionEngine(HarmonyDB *db);
    void sendMessage(HarmonyObject *recipient, HarmonyObject *arg, HarmonyObject *return_object = NULL);

    void run();

    bool pushFrame(HarmonyObject *frame);
    bool execute_match(HarmonyObject *pattern, HarmonyObject *unknowns, HarmonyObject *negatives);
};

#endif
