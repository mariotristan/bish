#include <cassert>
#include <set>
#include <string>
#include <queue>
#include <iostream>
#include "CodeGen_Bash.h"
#include "Parser.h"
#include "IRVisitor.h"

const std::string BISH_VERSION = "0.1";
const std::string BISH_URL = "https://github.com/tdenniston/bish";
const std::string STDLIB_PATH = "src/StdLib.bish";

class CallGraphBuilder : public Bish::IRVisitor {
public:
    virtual void visit(Bish::FunctionCall *call) {
        Bish::Block *b = dynamic_cast<Bish::Block *>(call->parent());
        assert(b);
        Bish::Function *f = dynamic_cast<Bish::Function *>(b->parent());
        assert(f);
        calls_map[f].push_back(call->function);
        callers_map[call->function].push_back(f);
    }

    // Return a list of direct calls from f.
    std::vector<Bish::Function *> calls(Bish::Function *f) {
        return calls_map[f];
    }

    // Return a list of all calls (recursively) from root.
    std::vector<Bish::Function *> transitive_calls(Bish::Function *root) {
        std::vector<Bish::Function *> result;
        std::set<Bish::Function *> visited;
        std::queue<Bish::Function *> worklist;
        worklist.push(root);
        visited.insert(root);
        while (!worklist.empty()) {
            Bish::Function *f = worklist.front();
            worklist.pop();
            std::vector<Bish::Function *> direct = calls(f);
            for (std::vector<Bish::Function *>::iterator I = direct.begin(), E = direct.end(); I != E; ++I) {
                if (visited.find(*I) == visited.end()) {
                    visited.insert(*I);
                    worklist.push(*I);
                    result.push_back(*I);
                }
            }
        }
        return result;
    }

    std::vector<Bish::Function *> callers(Bish::Function *f) {
        return callers_map[f];
    }

private:
    std::map<Bish::Function *, std::vector<Bish::Function *> > calls_map;
    std::map<Bish::Function *, std::vector<Bish::Function *> > callers_map;
};

class FindFunctionCalls : public Bish::IRVisitor {
public:
    FindFunctionCalls(const std::set<std::string> &n) {
        to_find.insert(n.begin(), n.end());
    }

    std::set<std::string> names() { return names_; }

    virtual void visit(Bish::FunctionCall *call) {
        for (std::vector<Bish::IRNode *>::const_iterator I = call->args.begin(),
                 E = call->args.end(); I != E; ++I) {
            (*I)->accept(this);
        }
        if (to_find.count(call->function->name)) {
            names_.insert(call->function->name);
            Bish::Block *b = dynamic_cast<Bish::Block *>(call->parent());
            assert(b);
            Bish::Function *f = dynamic_cast<Bish::Function *>(b->parent());
            assert(f);
            names_.insert(f->name);
            to_find.insert(f->name);
        }
    }
private:
    std::set<std::string> to_find;
    std::set<std::string> names_;
};

void link_stdlib(Bish::Module *m) {
    Bish::Parser p;
    Bish::Module *stdlib = p.parse(STDLIB_PATH);
    std::set<std::string> stdlib_functions;
    for (std::vector<Bish::Function *>::iterator I = stdlib->functions.begin(),
             E = stdlib->functions.end(); I != E; ++I) {
        Bish::Function *f = *I;
        if (f->name.compare("main") == 0) continue;
        stdlib_functions.insert(f->name);
    }
    FindFunctionCalls find(stdlib_functions);
    m->accept(&find);
    CallGraphBuilder cg;
    stdlib->accept(&cg);
    
    std::set<std::string> to_link = find.names();
    for (std::set<std::string>::iterator I = to_link.begin(), E = to_link.end(); I != E; ++I) {
        Bish::Function *f = stdlib->get_function(*I);
        assert(f);
        if (f->name.compare("main") == 0) continue;
        m->add_function(f);
        std::vector<Bish::Function *> calls = cg.transitive_calls(f);
        for (std::vector<Bish::Function *>::iterator II = calls.begin(), EE = calls.end(); II != EE; ++II) {
            m->add_function(stdlib->get_function((*II)->name));
        }
    }
}

void compile_to_bash(std::ostream &os, Bish::Module *m) {
    link_stdlib(m);
    os << "#!/bin/bash\n";
    os << "# Autogenerated script, compiled from the Bish language.\n";
    os << "# Bish version " << BISH_VERSION << "\n";
    os << "# Please see " << BISH_URL << " for more information about Bish.\n\n";
    Bish::CodeGen_Bash compile(os);
    m->accept(&compile);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "USAGE: " << argv[0] << " <INPUT>\n";
        std::cerr << "  Compiles Bish file <INPUT> to bash.\n";
        return 1;
    }

    std::string path(argv[1]);
    Bish::Parser p;
    Bish::Module *m = p.parse(path);

    compile_to_bash(std::cout, m);

    return 0;
}
