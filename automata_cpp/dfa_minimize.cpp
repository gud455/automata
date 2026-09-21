#include "include/cli.hpp"
#include "include/minimize.hpp"
int main(int argc,char** argv) {return fa::cli_main([&] {
    fa::Arguments args(argc,argv,{});
    auto d=fa::minimize_dfa(fa::read_dfa(std::cin));fa::write_dfa(std::cout,d);
});}
