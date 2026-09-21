#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fa {
struct Edge { int to; int symbol; }; // symbol=-1: epsilon; otherwise alphabet index
struct NFA {
    int start = 0;
    std::vector<int> alphabet;
    std::vector<std::vector<Edge>> adj;
    std::vector<uint8_t> accept;
};
struct DFA {
    int start = 0;
    std::vector<int> alphabet;
    std::vector<std::vector<int>> trans; // -1 means implicit rejection
    std::vector<uint8_t> accept;
};
inline void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
inline void validate_alphabet(const std::vector<int>& alphabet) {
    std::array<bool,256> seen{};
    for (int c : alphabet) {
        require(c >= 0 && c < 256, "alphabet byte must be in [0,255]");
        require(!seen[c], "duplicate alphabet byte"); seen[c] = true;
    }
}
inline void validate(const NFA& a) {
    validate_alphabet(a.alphabet);
    require(!a.adj.empty() && a.adj.size() <= static_cast<size_t>(INT32_MAX), "invalid NFA state count");
    int n = static_cast<int>(a.adj.size()), k = static_cast<int>(a.alphabet.size());
    require(a.start >= 0 && a.start < n && a.accept.size() == a.adj.size(), "invalid NFA start/finals");
    for (int u=0; u<n; ++u) {
        require(a.accept[u] <= 1, "accept flags must be 0 or 1");
        for (auto e : a.adj[u]) require(e.to>=0 && e.to<n && e.symbol>=-1 && e.symbol<k, "invalid NFA edge");
    }
}
inline void validate(const DFA& a) {
    validate_alphabet(a.alphabet);
    require(!a.trans.empty() && a.trans.size() <= static_cast<size_t>(INT32_MAX), "invalid DFA state count");
    int n = static_cast<int>(a.trans.size());
    require(a.start>=0 && a.start<n && a.accept.size()==a.trans.size(), "invalid DFA start/finals");
    for (int u=0; u<n; ++u) {
        require(a.accept[u]<=1 && a.trans[u].size()==a.alphabet.size(), "invalid DFA row/final");
        for (int v : a.trans[u]) require(v>=-1 && v<n, "invalid DFA transition");
    }
}

// Reachable BFS, with a lazily created rejecting sink. Alphabet order is preserved.
inline DFA complete_reachable(const DFA& a) {
    validate(a);
    int n=static_cast<int>(a.trans.size()), k=static_cast<int>(a.alphabet.size());
    require(n<INT32_MAX, "too many DFA states to add a sink");
    std::vector<int> id(static_cast<size_t>(n)+1,-1), order{a.start};
    id[a.start]=0;
    DFA b; b.alphabet=a.alphabet;
    for (size_t i=0; i<order.size(); ++i) {
        int u=order[i];
        std::vector<int> row(k);
        for (int c=0;c<k;++c) {
            int v=(u==n || a.trans[u][c]<0) ? n : a.trans[u][c];
            if (id[v]<0) { id[v]=static_cast<int>(order.size()); order.push_back(v); }
            row[c]=id[v];
        }
        b.trans.push_back(std::move(row));
        b.accept.push_back(u==n ? 0 : a.accept[u]);
    }
    return b;
}
inline NFA as_nfa(const DFA& a) {
    validate(a);
    NFA b; b.start=a.start; b.alphabet=a.alphabet; b.accept=a.accept;
    b.adj.resize(a.trans.size());
    for (size_t u=0;u<a.trans.size();++u)
        for (size_t c=0;c<a.alphabet.size();++c)
            if (a.trans[u][c]>=0) b.adj[u].push_back({a.trans[u][c],static_cast<int>(c)});
    return b;
}

// Runtime-sized bitset: no fixed MAXN and no vector<bool> proxy overhead.
struct Bits {
    std::vector<uint64_t> w;
    Bits() = default;
    explicit Bits(size_t words) : w(words,0) {}
    void set(int i) { w[static_cast<size_t>(i)>>6] |= uint64_t(1)<<(i&63); }
    void merge(const Bits& b) { for(size_t i=0;i<w.size();++i) w[i]|=b.w[i]; }
    bool intersects(const Bits& b) const {
        for(size_t i=0;i<w.size();++i) if(w[i]&b.w[i]) return true;
        return false;
    }
    bool operator==(const Bits& b) const { return w==b.w; }
    template<class F> void each(F f) const {
        for(size_t i=0;i<w.size();++i) {
            uint64_t x=w[i];
            while(x) {
                unsigned bit=static_cast<unsigned>(__builtin_ctzll(x)); // x is nonzero
                f(static_cast<int>(i*64+bit)); x&=x-1;
            }
        }
    }
};
struct BitsHash {
    size_t operator()(const Bits& b) const noexcept {
        uint64_t h=0x243f6a8885a308d3ULL;
        for(uint64_t x : b.w) {
            x+=0x9e3779b97f4a7c15ULL; x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
            x=(x^(x>>27))*0x94d049bb133111ebULL; x^=x>>31;
            h^=x+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2);
        }
        return static_cast<size_t>(h);
    }
};
struct DeterminizeOptions {
    size_t max_states=200000;
    size_t max_bitset_bytes=268435456; // epsilon closures + subset keys + temporary words
    size_t max_cache_bytes=67108864;  // optional move/closure cache, separate budget
};

inline DFA determinize(const NFA& a, const DeterminizeOptions& opt={}) {
    validate(a);
    require(opt.max_states>0, "DFA state limit must be positive");
    int n=static_cast<int>(a.adj.size()), k=static_cast<int>(a.alphabet.size());
    // Remove unreachable states before epsilon-SCC preprocessing.
    std::vector<uint8_t> reachable(n,0); std::vector<int> todo{a.start}; reachable[a.start]=1;
    for(size_t i=0;i<todo.size();++i) for(auto e:a.adj[todo[i]])
        if(!reachable[e.to]) { reachable[e.to]=1; todo.push_back(e.to); }
    std::vector<std::vector<int>> eps(n), rev(n);
    for(int u:todo) for(auto e:a.adj[u]) if(e.symbol<0) {
        eps[u].push_back(e.to); rev[e.to].push_back(u);
    }
    // Iterative Kosaraju: long epsilon chains cannot overflow the call stack.
    std::vector<uint8_t> seen(n,0); std::vector<int> finish; finish.reserve(todo.size());
    for(int root:todo) if(!seen[root]) {
        std::vector<std::pair<int,size_t>> st{{root,0}}; seen[root]=1;
        while(!st.empty()) {
            int u=st.back().first; size_t& p=st.back().second;
            if(p==eps[u].size()) { finish.push_back(u); st.pop_back(); }
            else { int v=eps[u][p++]; if(!seen[v]) { seen[v]=1; st.push_back({v,0}); } }
        }
    }
    std::vector<int> component(n,-1); int count=0;
    for(auto it=finish.rbegin();it!=finish.rend();++it) if(component[*it]<0) {
        std::vector<int> st{*it}; component[*it]=count;
        while(!st.empty()) { int u=st.back();st.pop_back();for(int v:rev[u])
            if(component[v]<0) {component[v]=count;st.push_back(v);} }
        ++count;
    }
    size_t words=(static_cast<size_t>(count)+63)/64, bytes=words*sizeof(uint64_t);
    // One accepting mask and two temporary bitsets; map keys are owned only once.
    require(static_cast<size_t>(count)+3 <= opt.max_bitset_bytes/bytes,
            "epsilon-closure bitset budget exceeded; increase --bitset-mib");
    size_t fixed=static_cast<size_t>(count)+3;
    std::vector<std::vector<int>> dag(count); std::vector<int> indeg(count,0);
    for(int u:todo) for(int v:eps[u]) if(component[u]!=component[v])
        dag[component[u]].push_back(component[v]);
    for(auto& row:dag) {
        std::sort(row.begin(),row.end()); row.erase(std::unique(row.begin(),row.end()),row.end());
        for(int v:row) ++indeg[v];
    }
    std::vector<int> topo; topo.reserve(count);
    for(int c=0;c<count;++c) if(!indeg[c]) topo.push_back(c);
    for(size_t i=0;i<topo.size();++i) for(int v:dag[topo[i]]) if(--indeg[v]==0) topo.push_back(v);
    std::vector<Bits> closure; closure.reserve(count);
    for(int c=0;c<count;++c) { closure.emplace_back(words);closure.back().set(c); }
    for(auto it=topo.rbegin();it!=topo.rend();++it) for(int v:dag[*it]) closure[*it].merge(closure[v]);
    Bits finals(words); for(int u:todo) if(a.accept[u]) finals.set(component[u]);
    // Only nonempty (component,symbol) pairs are stored; cache within an explicit budget.
    struct Move { int symbol; std::vector<int> targets; int cache=-1; };
    std::vector<std::vector<Move>> moves(count);
    std::vector<std::vector<std::pair<int,int>>> raw(count);
    for(int u:todo) for(auto e:a.adj[u]) if(e.symbol>=0)
        raw[component[u]].push_back({e.symbol,component[e.to]});
    std::vector<Bits> cache;
    for(int c=0;c<count;++c) {
        auto& row=raw[c]; std::sort(row.begin(),row.end());
        row.erase(std::unique(row.begin(),row.end()),row.end());
        for(auto p:row) {
            if(moves[c].empty() || moves[c].back().symbol!=p.first) moves[c].push_back({p.first,{},-1});
            moves[c].back().targets.push_back(p.second);
        }
        for(auto& m:moves[c]) if(m.targets.size()>1 && cache.size()<opt.max_cache_bytes/bytes) {
            require(cache.size()<static_cast<size_t>(INT32_MAX), "move cache too large");
            m.cache=static_cast<int>(cache.size());cache.emplace_back(words);
            for(int t:m.targets) cache.back().merge(closure[t]);
        }
        std::vector<std::pair<int,int>>().swap(row);
    }
    DFA d; d.alphabet=a.alphabet;
    std::unordered_map<Bits,int,BitsHash> ids;
    ids.max_load_factor(0.7f); ids.reserve(std::min<size_t>(opt.max_states,4096));
    std::vector<const Bits*> states; // unordered_map element references survive rehash
    auto intern=[&](Bits bits) {
        auto it=ids.find(bits); if(it!=ids.end()) return it->second;
        require(states.size()<opt.max_states && states.size()<static_cast<size_t>(INT32_MAX),
                "DFA state limit exceeded; subset construction may be exponential");
        require(states.size()<opt.max_bitset_bytes/bytes-fixed, "DFA subset bitset budget exceeded");
        int id=static_cast<int>(states.size());
        auto p=ids.emplace(std::move(bits),id);
        states.push_back(&p.first->first);
        d.accept.push_back(p.first->first.intersects(finals) ? 1 : 0);
        d.trans.emplace_back(k,-1); return id;
    };
    d.start=intern(closure[component[a.start]]);
    std::vector<int> active;
    for(size_t i=0;i<states.size();++i) {
        active.clear();states[i]->each([&](int c){active.push_back(c);});
        for(int symbol=0;symbol<k;++symbol) {
            Bits target(words);
            for(int c:active) {
                const auto& row=moves[c];
                auto it=std::lower_bound(row.begin(),row.end(),symbol,
                    [](const Move& m,int s){return m.symbol<s;});
                if(it==row.end() || it->symbol!=symbol) continue;
                if(it->cache>=0) target.merge(cache[it->cache]);
                else for(int t:it->targets) target.merge(closure[t]);
            }
            int v=intern(std::move(target)); // may reallocate d.trans; assign afterward
            d.trans[i][symbol]=v;
        }
    }
    return d; // empty subset becomes a total rejecting sink, when reachable
}
} // namespace fa
