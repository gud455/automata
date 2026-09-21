#include "include/cli.hpp"
#include "include/regex.hpp"
#include "include/minimize.hpp"
int main(int argc,char** argv) {return fa::cli_main([&] {
    fa::Arguments args(argc,argv,{"--max-nfa-states","--max-states","--bitset-mib","--cache-mib"});
    auto a=fa::regex_to_nfa(fa::read_regex(std::cin),args.get("--max-nfa-states",1000000));
    auto d=fa::minimize_dfa(fa::determinize(a,args.determinize_options()));fa::write_dfa(std::cout,d);
});}
