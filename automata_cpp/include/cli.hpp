#pragma once
#include "io.hpp"
#include <functional>
#include <iterator>
#include <map>
#include <set>
namespace fa {
class Arguments {
    std::map<std::string,size_t> values;
public:
    Arguments(int argc,char** argv,std::initializer_list<std::string> allowed) {
        std::set<std::string> names(allowed);
        for(int i=1;i<argc;i+=2) {
            std::string key=argv[i];require(names.count(key)>0,"unknown option: "+key);
            require(i+1<argc,"missing value for "+key);require(!values.count(key),"duplicate option: "+key);
            std::string value=argv[i+1];require(!value.empty(),"empty option value");
            size_t x=0;for(char c:value) {
                require(c>='0' && c<='9',"option must be a nonnegative decimal integer: "+key);
                require(x<=(std::numeric_limits<size_t>::max()-static_cast<unsigned>(c-'0'))/10,"option integer overflow");
                x=x*10+static_cast<unsigned>(c-'0');
            }
            values.emplace(key,x);
        }
    }
    size_t get(const std::string& key,size_t fallback) const {
        auto it=values.find(key);return it==values.end()?fallback:it->second;
    }
    DeterminizeOptions determinize_options() const {
        DeterminizeOptions o;o.max_states=get("--max-states",o.max_states);
        auto mib=[&](const std::string& key,size_t def) {
            size_t x=get(key,def>>20);require(x<=std::numeric_limits<size_t>::max()/(1<<20),"MiB overflow");return x*(1<<20);
        };
        o.max_bitset_bytes=mib("--bitset-mib",o.max_bitset_bytes);
        o.max_cache_bytes=mib("--cache-mib",o.max_cache_bytes);return o;
    }
};
inline std::string read_regex(std::istream& in) {
    // Bounded streaming read rather than an unbounded istreambuf_iterator allocation.
    constexpr size_t limit=1000000;std::string s;char ch;
    while(in.get(ch)) {require(s.size()<limit,"regex input exceeds 1000000 bytes");s+=ch;}
    require(!in.bad(),"input read failed");return s;
}
template<class F> int cli_main(F run) {
    std::ios::sync_with_stdio(false);std::cin.tie(nullptr);
    try {run();require(bool(std::cout),"output write failed");return 0;}
    catch(const std::exception& e) {std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}
} // namespace fa
