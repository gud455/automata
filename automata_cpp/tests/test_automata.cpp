// C++17 differential tests. All reference algorithms below are deliberately
// simple and independent of the production bitset/minimization algorithms.
#include "../include/core.hpp"
#include "../include/regex.hpp"
#include "../include/minimize.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
std::size_t checks = 0;

void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

template <class F>
void must_throw(F&& function, const std::string& message) {
    bool thrown = false;
    try { function(); } catch (const std::exception&) { thrown = true; }
    check(thrown, message);
}

std::string bytes(const std::string& input) {
    static const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : input) {
        result += "\\x";
        result += hex[c >> 4];
        result += hex[c & 15];
    }
    return result.empty() ? "<epsilon>" : result;
}

// Byte values and alphabet indices are intentionally kept separate.
int alphabet_index(const std::vector<int>& alphabet, int byte) {
    auto it = std::find(alphabet.begin(), alphabet.end(), byte);
    return it == alphabet.end() ? -1 : int(it - alphabet.begin());
}

std::vector<int> closure(const fa::NFA& nfa, const std::vector<int>& seed) {
    std::vector<unsigned char> seen(nfa.adj.size(), 0);
    std::queue<int> pending;
    for (int state : seed) if (!seen[state]) {
        seen[state] = 1;
        pending.push(state);
    }
    while (!pending.empty()) {
        int state = pending.front();
        pending.pop();
        for (const auto& edge : nfa.adj[state]) {
            if (edge.symbol == -1 && !seen[edge.to]) {
                seen[edge.to] = 1;
                pending.push(edge.to);
            }
        }
    }
    std::vector<int> answer;
    for (int i = 0; i < int(seen.size()); ++i) if (seen[i]) answer.push_back(i);
    return answer;
}

std::vector<int> move(const fa::NFA& nfa, const std::vector<int>& states,
                      int symbol) {
    std::vector<int> next;
    for (int state : states) for (const auto& edge : nfa.adj[state]) {
        if (edge.symbol == symbol) next.push_back(edge.to);
    }
    return closure(nfa, next);
}

bool naive_accepts(const fa::NFA& nfa, const std::string& word) {
    auto states = closure(nfa, {nfa.start});
    for (unsigned char byte : word) {
        int symbol = alphabet_index(nfa.alphabet, byte);
        if (symbol < 0) return false;
        states = move(nfa, states, symbol);
    }
    for (int state : states) if (nfa.accept[state]) return true;
    return false;
}

bool dfa_accepts(const fa::DFA& dfa, const std::string& word) {
    int state = dfa.start;
    for (unsigned char byte : word) {
        int symbol = alphabet_index(dfa.alphabet, byte);
        if (symbol < 0 || state < 0) return false;
        state = dfa.trans[state][symbol];
    }
    return state >= 0 && dfa.accept[state];
}

// Ordinary sorted vector subsets + std::map, without bitsets or closure caching.
fa::DFA reference_determinize(const fa::NFA& nfa) {
    fa::DFA dfa;
    dfa.alphabet = nfa.alphabet;
    dfa.start = 0;
    std::map<std::vector<int>, int> ids;
    std::vector<std::vector<int>> subsets;
    auto first = closure(nfa, {nfa.start});
    ids.emplace(first, 0);
    subsets.push_back(first);
    for (std::size_t current = 0; current < subsets.size(); ++current) {
        // Copy: inserting successors can reallocate subsets.
        auto subset = subsets[current];
        std::vector<int> row;
        bool final = false;
        for (int state : subset) final = final || nfa.accept[state];
        dfa.accept.push_back(final);
        for (int symbol = 0; symbol < int(nfa.alphabet.size()); ++symbol) {
            auto next = move(nfa, subset, symbol);
            auto [it, inserted] = ids.emplace(next, int(subsets.size()));
            if (inserted) subsets.push_back(std::move(next));
            row.push_back(it->second);
        }
        dfa.trans.push_back(std::move(row));
    }
    return dfa;
}

int next_state(const fa::DFA& dfa, int state, int byte) {
    if (state < 0) return -1;
    int symbol = alphabet_index(dfa.alphabet, byte);
    return symbol < 0 ? -1 : dfa.trans[state][symbol];
}

bool final_state(const fa::DFA& dfa, int state) {
    return state >= 0 && dfa.accept[state];
}

// Exact equivalence, including implicit rejecting sinks and differing alphabets.
// A shortest counterexample is attached to failures.
void equivalent(const fa::DFA& left, const fa::DFA& right,
                const std::string& context) {
    std::set<int> symbols(left.alphabet.begin(), left.alphabet.end());
    symbols.insert(right.alphabet.begin(), right.alphabet.end());
    using Pair = std::pair<int, int>;
    std::map<Pair, std::string> visited;
    std::queue<Pair> pending;
    Pair initial{left.start, right.start};
    visited.emplace(initial, "");
    pending.push(initial);
    while (!pending.empty()) {
        Pair current = pending.front();
        pending.pop();
        const auto word = visited.at(current);
        check(final_state(left, current.first) == final_state(right, current.second),
              context + ": counterexample " + bytes(word));
        for (int byte : symbols) {
            Pair next{next_state(left, current.first, byte),
                      next_state(right, current.second, byte)};
            if (visited.emplace(next, word + char(byte)).second) pending.push(next);
        }
    }
}

// Independent reachable completion, also retaining the empty-word start state.
fa::DFA reference_complete(const fa::DFA& input) {
    fa::DFA result;
    result.alphabet = input.alphabet;
    result.start = 0;
    std::map<int, int> ids{{input.start, 0}};
    std::vector<int> states{input.start};
    for (std::size_t i = 0; i < states.size(); ++i) {
        int original = states[i];
        result.accept.push_back(final_state(input, original));
        std::vector<int> row;
        for (int byte : result.alphabet) {
            int next = next_state(input, original, byte);
            auto [it, inserted] = ids.emplace(next, int(states.size()));
            if (inserted) states.push_back(next);
            row.push_back(it->second);
        }
        result.trans.push_back(std::move(row));
    }
    return result;
}

// Quadratic table filling, distinct from partition-refinement minimization.
int reference_minimal_count(const fa::DFA& input) {
    auto dfa = reference_complete(input);
    int n = int(dfa.trans.size());
    std::vector<std::vector<unsigned char>> different(n, std::vector<unsigned char>(n));
    for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b)
        different[a][b] = bool(dfa.accept[a]) != bool(dfa.accept[b]);
    bool changed = true;
    while (changed) {
        changed = false;
        for (int a = 0; a < n; ++a) for (int b = a + 1; b < n; ++b) {
            if (different[a][b]) continue;
            for (int symbol = 0; symbol < int(dfa.alphabet.size()); ++symbol) {
                if (different[dfa.trans[a][symbol]][dfa.trans[b][symbol]]) {
                    different[a][b] = different[b][a] = 1;
                    changed = true;
                    break;
                }
            }
        }
    }
    int classes = 0;
    for (int state = 0; state < n; ++state) {
        bool represented = false;
        for (int prior = 0; prior < state; ++prior)
            represented = represented || !different[state][prior];
        if (!represented) ++classes;
    }
    return classes;
}

void check_total_reachable(const fa::DFA& dfa, const std::string& context) {
    fa::validate(dfa);
    std::vector<unsigned char> seen(dfa.trans.size());
    std::queue<int> pending;
    pending.push(dfa.start);
    seen[dfa.start] = 1;
    while (!pending.empty()) {
        int current = pending.front(); pending.pop();
        for (int next : dfa.trans[current]) {
            check(next >= 0 && next < int(dfa.trans.size()), context + ": incomplete DFA");
            if (!seen[next]) { seen[next] = 1; pending.push(next); }
        }
    }
    check(std::all_of(seen.begin(), seen.end(), [](unsigned char x) { return x != 0; }),
          context + ": unreachable output state");
}

void check_minimize(const fa::DFA& input, const std::string& context) {
    auto result = fa::minimize_dfa(input);
    check_total_reachable(result, context);
    equivalent(input, result, context);
    check(int(result.trans.size()) == reference_minimal_count(input),
          context + ": state count is not minimal");
    auto second = fa::minimize_dfa(result);
    check(second.trans.size() == result.trans.size(), context + ": non-idempotent state count");
    equivalent(result, second, context + ": second minimization");
}

void exhaustive_words(const std::vector<int>& alphabet, int depth,
                      const std::function<void(const std::string&)>& visit) {
    std::function<void(std::string&, int)> dfs = [&](std::string& word, int remaining) {
        visit(word);
        if (remaining == 0) return;
        for (int symbol : alphabet) {
            word.push_back(char(symbol));
            dfs(word, remaining - 1);
            word.pop_back();
        }
    };
    std::string word;
    dfs(word, depth);
}

void check_nfa(const fa::NFA& nfa, const std::string& context, int depth = 4) {
    fa::validate(nfa);
    auto dfa = fa::determinize(nfa);
    fa::validate(dfa);
    auto reference = reference_determinize(nfa);
    equivalent(reference, dfa, context + ": subset construction");
    fa::DeterminizeOptions uncached;
    uncached.max_cache_bytes = 0;
    equivalent(reference, fa::determinize(nfa, uncached), context + ": disabled move cache");
    exhaustive_words(nfa.alphabet, depth, [&](const std::string& word) {
        check(naive_accepts(nfa, word) == dfa_accepts(dfa, word),
              context + ": simulation mismatch on " + bytes(word));
    });
    auto completed = fa::complete_reachable(dfa);
    check_total_reachable(completed, context + ": completion");
    equivalent(dfa, completed, context + ": completion");
    equivalent(dfa, reference_determinize(fa::as_nfa(dfa)), context + ": DFA to NFA");
    check_minimize(dfa, context + ": minimization");
}

void check_regex_roundtrip(const std::string& pattern, const std::string& context) {
    auto nfa = fa::regex_to_nfa(pattern);
    fa::validate(nfa);
    auto expected = reference_determinize(nfa);
    auto dfa = fa::determinize(nfa);
    equivalent(expected, dfa, context + ": regex determinization");
    auto minimal = fa::minimize_dfa(dfa);
    equivalent(expected, minimal, context + ": regex minimization");
    const auto regenerated = fa::nfa_to_regex(fa::as_nfa(minimal));
    check(!regenerated.empty(), context + ": empty regex serialization");
    auto recovered = fa::regex_to_nfa(regenerated);
    equivalent(expected, reference_determinize(recovered),
               context + ": regex round trip; output=" + regenerated);
}

void fixed_regex_tests() {
    struct Case { std::string regex; std::vector<std::string> yes, no; };
    const std::vector<Case> cases = {
        {"@", {""}, {"a", "@"}},
        {"#", {}, {"", "a", "#"}},
        {"a", {"a"}, {"", "aa", "b"}},
        {"(a|b)*abb", {"abb", "aabb", "babb", "abababb"}, {"", "ab", "abba"}},
        {"a+b?", {"a", "aa", "ab", "aaaab"}, {"", "b", "abb", "ba"}},
        {" a . ( b | @ ) ", {"a", "ab"}, {"", "b", "aba"}},
        {"(a?)*", {"", "a", "aaaa"}, {"b", "ab"}},
        {"#*", {""}, {"a"}},
        {"#a|b#|@", {""}, {"a", "b", "ab"}},
        {"a|bc", {"a", "bc"}, {"ac", "b", "abc"}},
        {R"(\|\*\+\?\(\)\.\@\#\\)", {"|*+?().@#\\"}, {"", "|*+?().@#"}},
        {R"(\x00\x20\xFF)", {std::string("\0 \xFF", 3)}, {"", " ", std::string("\0", 1)}},
        {R"((\x00|\xFF)*)", {"", std::string("\0\xFF\0", 3)}, {"a", " "}}
    };
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto& test = cases[i];
        auto nfa = fa::regex_to_nfa(test.regex);
        for (const auto& word : test.yes)
            check(naive_accepts(nfa, word), "regex should accept " + bytes(word) + ": " + test.regex);
        for (const auto& word : test.no)
            check(!naive_accepts(nfa, word), "regex should reject " + bytes(word) + ": " + test.regex);
        check_nfa(nfa, "fixed regex " + std::to_string(i), 3);
        check_regex_roundtrip(test.regex, "fixed regex " + std::to_string(i));
    }
    const std::vector<std::string> malformed = {
        "", "   ", "a|", "|a", "(a", "a)", "()", "*a", ".a", "a.",
        "a..b", "a||b", "\\", "\\x", "\\x0", "\\xGG", "a(|b)"
    };
    for (const auto& pattern : malformed)
        must_throw([&] { (void)fa::regex_to_nfa(pattern); }, "accepted malformed regex: " + pattern);
}

void fixed_automata_tests() {
    for (bool accepting : {false, true}) {
        fa::NFA nfa;
        nfa.adj.resize(3);
        nfa.accept = {0, std::uint8_t(accepting), 1};
        nfa.adj[0].push_back({1, -1});
        nfa.adj[1].push_back({0, -1});
        check_nfa(nfa, accepting ? "empty alphabet accepting" : "empty alphabet rejecting");
        auto regex = fa::nfa_to_regex(nfa);
        equivalent(reference_determinize(nfa), reference_determinize(fa::regex_to_nfa(regex)),
                   "empty alphabet round trip");
    }
    fa::NFA wide;
    wide.alphabet = {'b', 'a'};  // Unsorted alphabet catches byte/index confusion.
    wide.adj.resize(257);
    wide.accept.assign(257, 0);
    wide.accept[256] = 1;
    for (int i = 0; i < 128; ++i) wide.adj[i].push_back({i + 1, -1});
    wide.adj[128].push_back({0, -1});
    wide.adj[127].push_back({129, 1});
    wide.adj[127].push_back({129, 1});  // Duplicate edges are semantically harmless.
    for (int i = 129; i < 255; ++i) wide.adj[i].push_back({i + 1, -1});
    wide.adj[255].push_back({256, 0});
    wide.adj[256].push_back({256, 0});
    check_nfa(wide, "257 states, epsilon SCC, duplicate edges", 5);
    check(naive_accepts(wide, "abbbb"), "wide reference sanity");

    fa::DFA partial;
    partial.alphabet = {0, 255};
    partial.start = 1;
    partial.trans = {{0, 0}, {2, -1}, {-1, 2}, {3, 3}};
    partial.accept = {1, 0, 1, 1};
    check_minimize(partial, "partial byte DFA with nonzero start/unreachable finals");
    auto completed = fa::complete_reachable(partial);
    equivalent(partial, completed, "partial completion");
    check_total_reachable(completed, "partial completion");
    equivalent(partial, reference_determinize(fa::as_nfa(partial)), "partial DFA to NFA");

    for (bool accepting : {false, true}) {
        fa::DFA uniform;
        uniform.alphabet = {'a', 'b'};
        uniform.trans = {{1, 2}, {2, 0}, {0, 1}};
        uniform.accept.assign(3, accepting);
        check_minimize(uniform, accepting ? "all final" : "no final");
        check(fa::minimize_dfa(uniform).trans.size() == 1, "uniform total DFA should merge to one state");
    }

    // A long distinguishability chain exercises many refinement rounds.
    fa::DFA chain;
    chain.alphabet = {'a', 'b'};
    chain.trans.assign(140, std::vector<int>(2, -1));
    chain.accept.assign(140, 0);
    chain.accept[139] = 1;
    for (int i = 0; i < 139; ++i) chain.trans[i][0] = i + 1;
    check_minimize(chain, "140-state distinguishability chain");

    // This language remembers the last 13 letters, so all 2^13 subsets are
    // reachable and pairwise distinguishable. It forces hash-map growth while
    // stored subset references remain in use. Avoid quadratic table filling here.
    fa::NFA powerset;
    powerset.alphabet = {'a', 'b'};
    powerset.adj.resize(14);
    powerset.accept.assign(14, 0);
    powerset.accept[13] = 1;
    powerset.adj[0] = {{0, 0}, {0, 1}, {1, 0}};
    for (int state = 1; state < 13; ++state)
        powerset.adj[state] = {{state + 1, 0}, {state + 1, 1}};
    auto powerset_dfa = fa::determinize(powerset);
    check(powerset_dfa.trans.size() == 8192, "13th-symbol powerset state count");
    equivalent(reference_determinize(powerset), powerset_dfa, "8192-state powerset");
    auto powerset_min = fa::minimize_dfa(powerset_dfa);
    check(powerset_min.trans.size() == 8192, "13th-symbol minimal state count");
    check_total_reachable(powerset_min, "8192-state powerset minimization");
    equivalent(powerset_dfa, powerset_min, "8192-state powerset minimization");
}

std::string random_regex(std::mt19937& rng, int depth) {
    if (depth == 0) {
        const char* atoms[] = {"a", "b", "@", "#"};
        return atoms[rng() % 4];
    }
    unsigned choice = rng() % 7;
    if (choice < 2) return random_regex(rng, 0);
    auto a = random_regex(rng, depth - 1);
    if (choice == 2) return "(" + a + "|" + random_regex(rng, depth - 1) + ")";
    if (choice == 3) return "(" + a + ").(" + random_regex(rng, depth - 1) + ")";
    return "(" + a + ")" + (choice == 4 ? "*" : choice == 5 ? "+" : "?");
}

void randomized_tests() {
    std::mt19937 rng(0x5EED2026u);
    const std::vector<int> symbols = {255, 'a', 0};
    for (int iteration = 0; iteration < 120; ++iteration) {
        int n = 1 + int(rng() % 7), sigma = int(rng() % 4);
        fa::NFA nfa;
        nfa.start = int(rng() % n);
        nfa.alphabet.assign(symbols.begin(), symbols.begin() + sigma);
        nfa.adj.resize(n);
        nfa.accept.resize(n);
        for (int from = 0; from < n; ++from) {
            nfa.accept[from] = rng() % 3 == 0;
            for (int to = 0; to < n; ++to) {
                if (rng() % 7 == 0) nfa.adj[from].push_back({to, -1});
                for (int symbol = 0; symbol < sigma; ++symbol)
                    if (rng() % 9 == 0) nfa.adj[from].push_back({to, symbol});
            }
        }
        auto context = "random NFA " + std::to_string(iteration);
        check_nfa(nfa, context);
        if (iteration < 35) {
            auto regex = fa::nfa_to_regex(nfa);
            equivalent(reference_determinize(nfa), reference_determinize(fa::regex_to_nfa(regex)),
                       context + ": direct NFA to regex");
        }
    }
    for (int iteration = 0; iteration < 220; ++iteration) {
        int n = 1 + int(rng() % 10), sigma = int(rng() % 4);
        fa::DFA dfa;
        dfa.start = int(rng() % n);
        dfa.alphabet.assign(symbols.begin(), symbols.begin() + sigma);
        dfa.trans.assign(n, std::vector<int>(sigma));
        dfa.accept.resize(n);
        for (int state = 0; state < n; ++state) {
            dfa.accept[state] = rng() % 2;
            for (int symbol = 0; symbol < sigma; ++symbol)
                dfa.trans[state][symbol] = rng() % 4 == 0 ? -1 : int(rng() % n);
        }
        check_minimize(dfa, "random partial DFA " + std::to_string(iteration));
    }
    for (int iteration = 0; iteration < 90; ++iteration)
        check_regex_roundtrip(random_regex(rng, 3), "random regex " + std::to_string(iteration));
}

void validation_and_limits() {
    fa::NFA nfa;
    nfa.adj.resize(1);
    nfa.accept = {0};
    fa::validate(nfa);
    auto bad_nfa = nfa;
    bad_nfa.start = -1;
    must_throw([&] { fa::validate(bad_nfa); }, "negative NFA start accepted");
    bad_nfa = nfa; bad_nfa.accept.clear();
    must_throw([&] { fa::validate(bad_nfa); }, "NFA acceptance size mismatch accepted");
    bad_nfa = nfa; bad_nfa.adj[0].push_back({1, -1});
    must_throw([&] { fa::validate(bad_nfa); }, "invalid NFA edge target accepted");
    bad_nfa = nfa; bad_nfa.adj[0].push_back({0, 0});
    must_throw([&] { fa::validate(bad_nfa); }, "invalid NFA symbol accepted");
    bad_nfa = nfa; bad_nfa.alphabet = {'a', 'a'};
    must_throw([&] { fa::validate(bad_nfa); }, "duplicate NFA alphabet accepted");
    bad_nfa = nfa; bad_nfa.alphabet = {256};
    must_throw([&] { fa::validate(bad_nfa); }, "non-byte NFA alphabet accepted");

    fa::DFA dfa;
    dfa.alphabet = {'a'};
    dfa.trans = {{-1}};
    dfa.accept = {0};
    fa::validate(dfa);
    auto bad_dfa = dfa; bad_dfa.start = 1;
    must_throw([&] { fa::validate(bad_dfa); }, "invalid DFA start accepted");
    bad_dfa = dfa; bad_dfa.trans[0].clear();
    must_throw([&] { fa::validate(bad_dfa); }, "invalid DFA row width accepted");
    bad_dfa = dfa; bad_dfa.trans[0][0] = -2;
    must_throw([&] { fa::validate(bad_dfa); }, "invalid negative DFA edge accepted");
    bad_dfa = dfa; bad_dfa.trans[0][0] = 1;
    must_throw([&] { fa::validate(bad_dfa); }, "invalid DFA edge target accepted");

    auto ab = fa::regex_to_nfa("ab");
    fa::DeterminizeOptions options;
    options.max_states = 1;
    must_throw([&] { (void)fa::determinize(ab, options); }, "determinization state limit ignored");
    must_throw([&] { (void)fa::regex_to_nfa("ab", 1); }, "Thompson state limit ignored");
    must_throw([&] { (void)fa::nfa_to_regex(ab, 1000000, 1); }, "regex output limit ignored");
}
}  // namespace

int main() {
    try {
        validation_and_limits();
        fixed_regex_tests();
        fixed_automata_tests();
        randomized_tests();
        std::cout << "PASS: " << checks
                  << " checks; deterministic seed 0x5EED2026; exact equivalence + "
                     "independent simulation/table filling.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
