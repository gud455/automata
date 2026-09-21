#pragma once
#include "core.hpp"
#include <iomanip>
#include <sstream>

namespace fa {
// Input limits also prevent corrupt input from triggering unbounded allocations.
constexpr long long INPUT_STATES=1000000, INPUT_EDGES=256000000, INPUT_CELLS=256000000;
inline long long read_integer(std::istream& in, const char* field) {
    std::string s; require(bool(in>>s),std::string("missing ")+field);
    require(s.size()<=20,"integer token too long");
    size_t used=0; long long x=0;
    try { x=std::stoll(s,&used); } catch(...) { throw std::runtime_error(std::string("invalid ")+field); }
    require(used==s.size(),std::string("invalid integer: ")+field);return x;
}
inline int bounded_integer(std::istream& in,long long lo,long long hi,const char* field) {
    long long x=read_integer(in,field);require(x>=lo && x<=hi,std::string("out of range: ")+field);
    return static_cast<int>(x);
}
inline int hex_digit(char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
inline int byte_token(const std::string& s) {
    require(s.size()==2 && hex_digit(s[0])>=0 && hex_digit(s[1])>=0,"byte must use two hex digits, e.g. 61");
    return hex_digit(s[0])*16+hex_digit(s[1]);
}
inline std::string byte_text(int c) {
    constexpr char digits[]="0123456789ABCDEF";
    std::string s(2,'0');s[0]=digits[c>>4];s[1]=digits[c&15];return s;
}
inline void read_alphabet(std::istream& in,int k,std::vector<int>& alphabet) {
    for(int i=0;i<k;++i) {std::string s;require(bool(in>>s),"missing alphabet byte");alphabet.push_back(byte_token(s));}
    validate_alphabet(alphabet);
}
inline void read_finals(std::istream& in,int n,int f,std::vector<uint8_t>& accept) {
    accept.assign(n,0);
    for(int i=0;i<f;++i) {int u=bounded_integer(in,0,n-1,"final state");require(!accept[u],"duplicate final state");accept[u]=1;}
}
inline void require_end(std::istream& in) {
    std::string extra;require(!(in>>extra),"unexpected trailing input");
    require(!in.bad(),"input read failed");
}
inline NFA read_nfa_body(std::istream& in) {
    int n=bounded_integer(in,1,INPUT_STATES,"NFA n");
    int k=bounded_integer(in,0,256,"alphabet size");
    NFA a;a.start=bounded_integer(in,0,n-1,"start");
    int f=bounded_integer(in,0,n,"final count"),m=bounded_integer(in,0,INPUT_EDGES,"edge count");
    read_alphabet(in,k,a.alphabet);read_finals(in,n,f,a.accept);a.adj.resize(n);
    std::array<int,256> index;index.fill(-1);for(int i=0;i<k;++i)index[a.alphabet[i]]=i;
    for(int i=0;i<m;++i) {
        int u=bounded_integer(in,0,n-1,"edge source"),v=bounded_integer(in,0,n-1,"edge target");
        std::string s;require(bool(in>>s),"missing edge label");int symbol=-1;
        if(s!="eps") {symbol=index[byte_token(s)];require(symbol>=0,"edge byte absent from alphabet");}
        a.adj[u].push_back({v,symbol});
    }
    validate(a);return a;
}
inline DFA read_dfa_body(std::istream& in) {
    int n=bounded_integer(in,1,INPUT_STATES,"DFA n"),k=bounded_integer(in,0,256,"alphabet size");
    require(static_cast<long long>(n)*k<=INPUT_CELLS,"DFA input transition table too large");
    DFA a;a.start=bounded_integer(in,0,n-1,"start");int f=bounded_integer(in,0,n,"final count");
    read_alphabet(in,k,a.alphabet);read_finals(in,n,f,a.accept);
    a.trans.assign(n,std::vector<int>(k));
    for(auto& row:a.trans)for(int& v:row)v=bounded_integer(in,-1,n-1,"DFA target");
    validate(a);return a;
}
inline std::string read_kind(std::istream& in) {
    std::string s;require(bool(in>>s),"missing NFA/DFA header");return s;
}
inline NFA read_nfa(std::istream& in) {
    require(read_kind(in)=="NFA","expected NFA header");NFA a=read_nfa_body(in);require_end(in);return a;
}
inline DFA read_dfa(std::istream& in) {
    require(read_kind(in)=="DFA","expected DFA header");DFA a=read_dfa_body(in);require_end(in);return a;
}
inline NFA read_automaton_as_nfa(std::istream& in) {
    std::string kind=read_kind(in);NFA a;
    if(kind=="NFA")a=read_nfa_body(in);
    else {require(kind=="DFA","expected NFA or DFA header");a=as_nfa(read_dfa_body(in));}
    require_end(in);return a;
}
inline void write_alphabet_finals(std::ostream& out,const std::vector<int>& alphabet,const std::vector<uint8_t>& accept) {
    for(size_t i=0;i<alphabet.size();++i) out<<(i?" ":"")<<byte_text(alphabet[i]);
    out<<'\n';
    bool first=true;for(size_t i=0;i<accept.size();++i)if(accept[i]) {out<<(first?"":" ")<<i;first=false;}out<<'\n';
}
inline void write_nfa(std::ostream& out,const NFA& a) {
    validate(a);size_t m=0;for(auto& row:a.adj)m+=row.size();
    out<<"NFA\n"<<a.adj.size()<<' '<<a.alphabet.size()<<' '<<a.start<<' '
       <<std::count(a.accept.begin(),a.accept.end(),uint8_t(1))<<' '<<m<<'\n';
    write_alphabet_finals(out,a.alphabet,a.accept);
    for(size_t u=0;u<a.adj.size();++u)for(auto e:a.adj[u])
        out<<u<<' '<<e.to<<' '<<(e.symbol<0 ? "eps" : byte_text(a.alphabet[e.symbol]))<<'\n';
}
inline void write_dfa(std::ostream& out,const DFA& a) {
    validate(a);out<<"DFA\n"<<a.trans.size()<<' '<<a.alphabet.size()<<' '<<a.start<<' '
       <<std::count(a.accept.begin(),a.accept.end(),uint8_t(1))<<'\n';
    write_alphabet_finals(out,a.alphabet,a.accept);
    for(auto& row:a.trans) {for(size_t c=0;c<row.size();++c)out<<(c?" ":"")<<row[c];out<<'\n';}
}
} // namespace fa
