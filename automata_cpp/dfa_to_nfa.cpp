#include "include/cli.hpp"
int main(int argc,char** argv) {return fa::cli_main([&] {
    fa::Arguments args(argc,argv,{});auto a=fa::as_nfa(fa::read_dfa(std::cin));fa::write_nfa(std::cout,a);
});}
