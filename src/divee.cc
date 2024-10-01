#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <vector>

#include "harmonydb.h"
#include "execution_engine.h"


list<HarmonyItem *> current_path;

HarmonyDB *db;
HarmonyObject *shell_context = NULL;
HarmonyObject *shell_receiver;

ExecutionEngine *engine;


vector<string> parse_command_line(const char *line)
{
    int i, token, start;
    vector<string> fields;

    fields.clear();
    token = 0;
    for (i = 0; line[i]; i++) {
        if (isblank(line[i])) {
            if (token) {
                token = 0;
                fields.push_back(string(line + start, i - start));
            }
        } else {
            if (!token) {
                token = 1;
                start = i;
            }
        }
    }
    if (token) {
        fields.push_back(string(line + start, i - start));
    }
    return fields;
}

string getCurrentPath()
{
    string path;

    if (current_path.empty()) {
        return string(".");
    }
    for (auto it: current_path) {
        if (it->label.empty())
            path += string(".") + it->object->getKey();
        else
            path += string(".") + it->label;
    }
    return path;
}

void shell_ls()
{
    HarmonyObject *parent = db->getRoot();
    HarmonyItem *item;

    if (!current_path.empty())
        parent = current_path.back()->object;
    for (item = parent->first(); item; item = item->nextItem(parent)) {
        auto o = item->object;
        printf("%s -> %s: (rt:%d  sm:%08x  refcnt:%d  srefcnt:%d) %c\n", o->getKey().c_str(), item->label.c_str(), item->object->root_distance,
            item->object->sweep_mark, item->object->reference.countReferences(), item->object->reference.structural_references, item->object->isEmpty() ? ' ': '*');
    }

    auto ri = parent->relations.next;
    while (ri != &parent->relations) {
        auto r = static_cast<HarmonyRelation *>(ri->object);
        printf("o:%p  r:%p  s:%p  d:%p\n", r->owner, r->relation.object, r->source.object, r->destination.object);
        ri = ri->next;
    }
}

void shell_cd(const vector<string> &fields)
{
    HarmonyObject *parent = db->getRoot();
    HarmonyItem *item;

    if (fields.size() == 1) { // go to root
        current_path.clear();
        return;
    }
    if (fields[1] == "..") { // go up
        current_path.pop_back();
        return;
    }
    if (!current_path.empty())
        parent = current_path.back()->object;
    for (item = parent->first(); item; item = item->nextItem(parent)) {
        if (item->label == fields[1] || item->object->getKey() == fields[1]) {
            // if (item->object->isEmpty() && item->object) {
            //     PF("Object is empty!");
            //     return;
            // } else {
                current_path.push_back(item);
                return;
            // }
        }
    }
    PF("Object not found!");
}

void shell_clone(const vector<string> &fields)
{
    HarmonyObject *parent = db->getRoot();
    HarmonyItem *item;

    if (fields.size() == 1) {
        db->createContext(NULL);
        return;
    }
    if (!current_path.empty())
        parent = current_path.back()->object;
    for (item = parent->first(); item; item = item->nextItem(parent)) {
        if (item->label == fields[1] || item->object->getKey() == fields[1]) {
            db->createContext(item->object);//, "shell");
            return;
        }
    }
    PF("Object not found!");
}

void shell_rm(const vector<string> &fields)
{
    HarmonyObject *parent = db->getRoot();
    HarmonyItem *item;

    if (fields.size() == 1) {
        return;
    }
    if (!current_path.empty())
        parent = current_path.back()->object;
    for (item = parent->first(); item; item = item->nextItem(parent)) {
        if (item->label == fields[1] || item->object->getKey() == fields[1]) {
            parent->remove(item);
            return;
        }
    }
    PF("Object not found!");
}

void shell_send(const vector<string> &fields)
{
    HarmonyObject *parent = db->getRoot();
    HarmonyObject *receiver = NULL, *arg = NULL;
    HarmonyItem *item;

    if (fields.size() < 3)
        return;

    if (!current_path.empty())
        parent = current_path.back()->object;
    for (item = parent->first(); item; item = item->nextItem(parent)) {
        if (item->label == fields[1] || item->object->getKey() == fields[1]) {
            receiver = item->object;
        }
        if (item->label == fields[2] || item->object->getKey() == fields[2]) {
            arg = item->object;
        } else {
            HarmonyObjectPath path;
            path.parse(fields[2]);
            arg = db->getObjectByPath(path);
        }
    }
    // sscanf(fields[1].c_str(), "%p", &receiver);
    // sscanf(fields[2].c_str(), "%p", &arg);
    if (receiver && arg) {
        // HarmonyObject *arguments;

        printf("Sending %p to %p...\n", arg, receiver);
        // auto item = shell_context->findItem("arguments");
        // if (!item) {
        //     arguments = new HarmonyObject;
        //     shell_context->add(arguments, "arguments");
        // } else {
        //     arguments = item->object;
        //     arguments->clear();
        // }
        // arguments->add(shell_receiver, "receiver");
        // arguments->add(arg, "argument");
        db->clearArguments(shell_receiver);
        engine->sendMessage(receiver, arg, shell_receiver);
    }
}

static void shell_dump(const vector<string> &fields)
{
    string path;

    if (fields.size() > 1) {
        struct stat buf;
        path = fields[1];
        PF("dump path %s\n", path.c_str());
        auto r = lstat(path.c_str(), &buf);
        PF("%d\n", r);
        db->dumpBase(db->root.object, path);
    } else {
        db->dumpBase();
    }
}

void shell(void)
{
//    Configure readline to auto-complete paths when the tab key is hit.
    // rl_bind_key('\t', rl_complete);
    auto context = db->getRoot()->findItem("context");
    if (context) {
        auto shell_context_item = context->object->findItem("shell");
        if (shell_context_item)
            shell_context = shell_context_item->object;
    }
    if (!shell_context)
        shell_context = db->createContext(NULL, "shell");
    auto shell_receiver_item = shell_context->findItem("receiver");
    if (shell_receiver_item) {
        shell_receiver = shell_receiver_item->object;
    } else {
        shell_receiver = new HarmonyObject(HarmonyObject::Type::RECEIVE);
        shell_context->add(shell_receiver, "receiver", true);
        shell_receiver->add(new HarmonyObject, "named", true);
        shell_receiver->add(new HarmonyObject, "unnamed", true);
    }
    for (;;) {
        char prompt[257];

        snprintf(prompt, sizeof(prompt), "%s> ", getCurrentPath().c_str());
        // Display prompt and read input
        char* input = readline(prompt);

        // Check for EOF.
        if (!input)
            break;

        // Add input to readline history.
        add_history(input);

        // Do stuff...
        vector<string> fields;

        fields = parse_command_line(input);
        if (fields.size() > 0) {
            if (fields[0] == "ls") {
                shell_ls();
            } else if (fields[0] == "dump") {
                shell_dump(fields);
            } else if (fields[0] == "cd") {
                shell_cd(fields);
            } else if (fields[0] == "clone") {
                shell_clone(fields);
            } else if (fields[0] == "rm") {
                shell_rm(fields);
            } else if (fields[0] == "send") {
                shell_send(fields);
            }
        }
        // Free buffer that was allocated by readline
        free(input);
    }
    printf("\nBye!\n");
}

void initHarmony(const char *filepath) {
    db = buildBase(filepath);
    engine = new ExecutionEngine(db);
}

int main(int argc, char *argv[])
{
    const char *filepath = NULL;
    printf("Divee 1\n");

    if (argc > 1)
        filepath = argv[1];
    initHarmony(filepath);
    PF("BASE = %p", db);
    shell();
    delete db;
    PF("Bye!");
    assertf(HarmonyObject::_object_count == 0, "object_count=%d!", HarmonyObject::_object_count);
    return 0;
}
