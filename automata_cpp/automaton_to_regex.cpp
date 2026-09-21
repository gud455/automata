#include "include/cli.hpp"
#include "include/regex.hpp"
int main(int argc,char** argv) {return fa::cli_main([&] {
    fa::Arguments args(argc,argv,{"--max-nodes","--max-output"});
    auto s=fa::nfa_to_regex(fa::read_automaton_as_nfa(std::cin),
        args.get("--max-nodes",1000000),args.get("--max-output",1000000));
    std::cout<<s<<'\n';
});}
