#include "include/cli.hpp"
#include "include/regex.hpp"
int main(int argc,char** argv) {return fa::cli_main([&] {
    fa::Arguments args(argc,argv,{"--max-states"});
    auto a=fa::regex_to_nfa(fa::read_regex(std::cin),args.get("--max-states",1000000));
    fa::write_nfa(std::cout,a);
});}
