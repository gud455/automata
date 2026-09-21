#include "include/cli.hpp"
int main(int argc,char** argv) {return fa::cli_main([&] {
    fa::Arguments args(argc,argv,{"--max-states","--bitset-mib","--cache-mib"});
    auto d=fa::determinize(fa::read_nfa(std::cin),args.determinize_options());
    fa::write_dfa(std::cout,d);
});}
