#pragma once

#include "core.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fa {
namespace regex_detail {

inline bool space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
inline int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline std::string escaped_byte(unsigned char c) {
    if (c <= 32 || c >= 127) {
        constexpr char h[] = "0123456789ABCDEF";
        return std::string("\\x") + h[c >> 4] + h[c & 15];
    }
    const std::string reserved = "\\.|*+?()@#";
    if (reserved.find(static_cast<char>(c)) != std::string::npos)
        return std::string("\\") + static_cast<char>(c);
    return std::string(1, static_cast<char>(c));
}

} // namespace regex_detail

// Byte regular expressions: implicit concatenation or '.', '|', '*', '+', '?',
// parentheses, @ (epsilon), # (empty language), \xHH and escaped literal bytes.
// ASCII whitespace is ignored unless escaped. Brackets and anchors are literals;
// character classes, wildcard dots and bounded repetitions are not implemented.
inline NFA regex_to_nfa(const std::string& expression, std::size_t max_states = 1000000) {
    struct Fragment { int first, last; };
    NFA result;
    std::array<int, 256> alphabet_index;
    alphabet_index.fill(-1);
    std::vector<Fragment> values;
    std::vector<char> operators;
    bool need_operand = true;
    bool saw_token = false;

    auto state = [&]() -> int {
        if (result.adj.size() >= max_states ||
            result.adj.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::runtime_error("regex_to_nfa: state limit exceeded");
        int id = static_cast<int>(result.adj.size());
        result.adj.emplace_back();
        result.accept.push_back(0);
        return id;
    };
    auto edge = [&](int from, int to, int symbol) {
        result.adj[from].push_back(Edge{to, symbol});
    };
    auto reduce = [&]() {
        if (operators.empty() || operators.back() == '(' || values.size() < 2)
            throw std::invalid_argument("regex_to_nfa: missing operand");
        char op = operators.back(); operators.pop_back();
        Fragment b = values.back(); values.pop_back();
        Fragment a = values.back(); values.pop_back();
        if (op == '.') {
            edge(a.last, b.first, -1);
            values.push_back({a.first, b.last});
        } else {
            int first = state(), last = state();
            edge(first, a.first, -1); edge(first, b.first, -1);
            edge(a.last, last, -1); edge(b.last, last, -1);
            values.push_back({first, last});
        }
    };
    auto binary = [&](char op) {
        const int precedence = op == '.' ? 2 : 1;
        while (!operators.empty() && operators.back() != '(' &&
               (operators.back() == '.' ? 2 : 1) >= precedence) reduce();
        operators.push_back(op);
        need_operand = true;
    };

    for (std::size_t p = 0; p < expression.size(); ++p) {
        unsigned char c = static_cast<unsigned char>(expression[p]);
        if (regex_detail::space(c)) continue;
        saw_token = true;
        bool literal = false;
        if (c == '\\') {
            if (++p == expression.size())
                throw std::invalid_argument("regex_to_nfa: trailing backslash");
            c = static_cast<unsigned char>(expression[p]);
            if (c == 'x') {
                if (expression.size() - p < 3 ||
                    regex_detail::hex(static_cast<unsigned char>(expression[p + 1])) < 0 ||
                    regex_detail::hex(static_cast<unsigned char>(expression[p + 2])) < 0)
                    throw std::invalid_argument("regex_to_nfa: expected two hex digits after \\x");
                c = static_cast<unsigned char>(
                    regex_detail::hex(static_cast<unsigned char>(expression[p + 1])) * 16 +
                    regex_detail::hex(static_cast<unsigned char>(expression[p + 2])));
                p += 2;
            }
            literal = true;
        }
        if (!literal && c == '(') {
            if (!need_operand) binary('.');
            operators.push_back('(');
            need_operand = true;
        } else if (!literal && c == ')') {
            if (need_operand) throw std::invalid_argument("regex_to_nfa: empty group or missing operand");
            while (!operators.empty() && operators.back() != '(') reduce();
            if (operators.empty()) throw std::invalid_argument("regex_to_nfa: unmatched closing parenthesis");
            operators.pop_back();
            need_operand = false;
        } else if (!literal && (c == '.' || c == '|')) {
            if (need_operand) throw std::invalid_argument("regex_to_nfa: missing left operand");
            binary(static_cast<char>(c));
        } else if (!literal && (c == '*' || c == '+' || c == '?')) {
            if (need_operand || values.empty())
                throw std::invalid_argument("regex_to_nfa: postfix operator has no operand");
            Fragment a = values.back(); values.pop_back();
            int first = state(), last = state();
            edge(first, a.first, -1); edge(a.last, last, -1);
            if (c != '+') edge(first, last, -1);
            if (c != '?') edge(a.last, a.first, -1);
            values.push_back({first, last});
        } else {
            if (!need_operand) binary('.');
            int first = state(), last = state();
            if (!literal && c == '@') edge(first, last, -1);
            else if (literal || c != '#') {
                int& index = alphabet_index[c];
                if (index < 0) {
                    index = static_cast<int>(result.alphabet.size());
                    result.alphabet.push_back(c);
                }
                edge(first, last, index);
            }
            values.push_back({first, last});
            need_operand = false;
        }
    }
    if (!saw_token) throw std::invalid_argument("regex_to_nfa: empty expression; use @ for epsilon");
    if (need_operand) throw std::invalid_argument("regex_to_nfa: missing right operand");
    while (!operators.empty()) {
        if (operators.back() == '(')
            throw std::invalid_argument("regex_to_nfa: unmatched opening parenthesis");
        reduce();
    }
    if (values.size() != 1) throw std::invalid_argument("regex_to_nfa: malformed expression");
    result.start = values.back().first;
    result.accept[values.back().last] = 1;
    return result;
}

namespace regex_detail {

// Interning shares repeated subexpressions without copying their expanded text.
// Lengths saturate at max_output + 1; only the final expression is serialized.
class Expressions {
    enum Kind : unsigned char { Empty, Epsilon, Literal, Union, Concat, Star };
    struct Node { Kind kind; int a, b; std::size_t length; };
    struct Key {
        Kind kind; int a, b;
        bool operator==(const Key& other) const {
            return kind == other.kind && a == other.a && b == other.b;
        }
    };
    struct Hash {
        std::size_t operator()(const Key& key) const {
            std::size_t h = static_cast<std::size_t>(key.kind);
            h ^= std::hash<int>{}(key.a) + std::size_t(0x9e3779b9U) + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(key.b) + std::size_t(0x9e3779b9U) + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::vector<Node> nodes_;
    std::unordered_map<Key, int, Hash> intern_;
    std::size_t max_nodes_, max_output_, cap_;

    std::size_t sum(std::size_t a, std::size_t b) const {
        return a >= cap_ || b >= cap_ - a ? cap_ : a + b;
    }
    int precedence(int id) const {
        Kind k = nodes_[id].kind;
        return k == Union ? 1 : k == Concat ? 2 : k == Star ? 3 : 4;
    }
    std::size_t operand_length(int id, int parent_precedence) const {
        return sum(nodes_[id].length, precedence(id) < parent_precedence ? 2 : 0);
    }
    int make(Kind kind, int a, int b, std::size_t length) {
        Key key{kind, a, b};
        auto found = intern_.find(key);
        if (found != intern_.end()) return found->second;
        if (nodes_.size() >= max_nodes_ ||
            nodes_.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::runtime_error("nfa_to_regex: expression DAG node limit exceeded");
        int id = static_cast<int>(nodes_.size());
        nodes_.push_back({kind, a, b, length});
        intern_.emplace(key, id);
        return id;
    }
public:
    Expressions(std::size_t max_nodes, std::size_t max_output)
        : max_nodes_(max_nodes), max_output_(max_output),
          cap_(max_output == std::numeric_limits<std::size_t>::max() ? max_output : max_output + 1) {
        make(Empty, -1, -1, 1);   // ID 0
        make(Epsilon, -1, -1, 1); // ID 1
    }
    int literal(unsigned char c) {
        return make(Literal, c, -1, escaped_byte(c).size());
    }
    int unite(int a, int b) {
        if (a == 0) return b;
        if (b == 0 || a == b) return a;
        if (a > b) std::swap(a, b);
        return make(Union, a, b, sum(sum(nodes_[a].length, nodes_[b].length), 1));
    }
    int concat(int a, int b) {
        if (a == 0 || b == 0) return 0;
        if (a == 1) return b;
        if (b == 1) return a;
        return make(Concat, a, b, sum(operand_length(a, 2), operand_length(b, 2)));
    }
    int star(int a) {
        if (a == 0 || a == 1) return 1;
        if (nodes_[a].kind == Star) return a;
        return make(Star, a, -1, sum(operand_length(a, 3), 1));
    }
    std::string serialize(int root) const {
        if (nodes_[root].length > max_output_ ||
            nodes_[root].length > std::string().max_size())
            throw std::runtime_error("nfa_to_regex: expanded expression output limit exceeded");
        struct Task { int id; int parent_precedence; char token; };
        std::vector<Task> stack{{root, 0, 0}};
        std::string output;
        output.reserve(nodes_[root].length);
        auto emit = [&](const std::string& text) {
            if (text.size() > max_output_ - output.size())
                throw std::runtime_error("nfa_to_regex: expanded expression output limit exceeded");
            output += text;
        };
        while (!stack.empty()) {
            Task task = stack.back(); stack.pop_back();
            if (task.id < 0) { emit(std::string(1, task.token)); continue; }
            const Node& node = nodes_[task.id];
            bool parentheses = precedence(task.id) < task.parent_precedence;
            if (parentheses) stack.push_back({-1, 0, ')'});
            switch (node.kind) {
            case Empty: stack.push_back({-1, 0, '#'}); break;
            case Epsilon: stack.push_back({-1, 0, '@'}); break;
            case Literal: emit(escaped_byte(static_cast<unsigned char>(node.a))); break;
            case Union:
                stack.push_back({node.b, 1, 0});
                stack.push_back({-1, 0, '|'});
                stack.push_back({node.a, 1, 0});
                break;
            case Concat:
                stack.push_back({node.b, 2, 0});
                stack.push_back({node.a, 2, 0});
                break;
            case Star:
                stack.push_back({-1, 0, '*'});
                stack.push_back({node.a, 3, 0});
                break;
            }
            if (parentheses) stack.push_back({-1, 0, '('});
        }
        return output;
    }
};

} // namespace regex_detail

// Sparse GNFA elimination. max_nodes bounds both interned expression nodes and
// simultaneously stored GNFA edges (separately); max_output bounds output bytes.
// Resource-limit exceptions never return a partial/truncated regular expression.
inline std::string nfa_to_regex(const NFA& input, std::size_t max_nodes = 1000000,
                                std::size_t max_output = 1000000) {
    validate(input);
    regex_detail::Expressions expressions(max_nodes, max_output);
    const int n = static_cast<int>(input.adj.size());
    std::vector<std::vector<int>> reverse(n);
    for (int u = 0; u < n; ++u)
        for (const Edge& edge : input.adj[u]) reverse[edge.to].push_back(u);
    std::vector<unsigned char> reachable(n, 0), productive(n, 0);
    std::vector<int> queue;
    reachable[input.start] = 1;
    queue.push_back(input.start);
    for (std::size_t p = 0; p < queue.size(); ++p)
        for (const Edge& edge : input.adj[queue[p]])
            if (!reachable[edge.to]) { reachable[edge.to] = 1; queue.push_back(edge.to); }
    queue.clear();
    for (int u = 0; u < n; ++u)
        if (input.accept[u]) { productive[u] = 1; queue.push_back(u); }
    for (std::size_t p = 0; p < queue.size(); ++p)
        for (int u : reverse[queue[p]])
            if (!productive[u]) { productive[u] = 1; queue.push_back(u); }
    if (!productive[input.start]) return expressions.serialize(0);

    std::vector<int> remap(n, -1);
    int count = 0;
    for (int u = 0; u < n; ++u)
        if (reachable[u] && productive[u]) {
            if (count >= std::numeric_limits<int>::max() - 2)
                throw std::runtime_error("nfa_to_regex: too many states");
            remap[u] = count++;
        }
    const int start = count, finish = count + 1;
    std::vector<std::unordered_map<int, int>> outgoing(count + 2);
    std::vector<std::unordered_set<int>> incoming(count + 2);
    std::vector<unsigned char> alive(count, 1), dirty(count, 0);
    std::vector<int> touched;
    std::size_t edge_count = 0;
    auto mark = [&](int u) {
        if (u < count && alive[u] && !dirty[u]) { dirty[u] = 1; touched.push_back(u); }
    };
    auto add_edge = [&](int u, int v, int expression) {
        if (expression == 0) return;
        auto found = outgoing[u].find(v);
        if (found != outgoing[u].end()) {
            found->second = expressions.unite(found->second, expression);
        } else {
            if (edge_count >= max_nodes)
                throw std::runtime_error("nfa_to_regex: sparse GNFA edge limit exceeded");
            outgoing[u].emplace(v, expression);
            incoming[v].insert(u);
            ++edge_count;
            mark(u); mark(v);
        }
    };
    for (int u = 0; u < n; ++u) if (remap[u] >= 0) {
        for (const Edge& edge : input.adj[u]) if (remap[edge.to] >= 0) {
            int label = edge.symbol < 0 ? 1 : expressions.literal(
                static_cast<unsigned char>(input.alphabet[edge.symbol]));
            add_edge(remap[u], remap[edge.to], label);
        }
        if (input.accept[u]) add_edge(remap[u], finish, 1);
    }
    add_edge(start, remap[input.start], 1);

    // Minimize the number of predecessor/successor pairs created by elimination.
    // Lazy versioning and periodic rebuilds bound stale priority-queue entries.
    using Entry = std::tuple<std::uint64_t, std::uint64_t, int, std::uint64_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
    std::vector<std::uint64_t> version(count, 0);
    auto push_score = [&](int u) {
        std::uint64_t self = outgoing[u].count(u);
        std::uint64_t a = incoming[u].size() - self;
        std::uint64_t b = outgoing[u].size() - self;
        heap.emplace(a * b, a + b, u, ++version[u]);
    };
    for (int u = 0; u < count; ++u) push_score(u);
    for (int u : touched) dirty[u] = 0;
    touched.clear();
    for (int remaining = count; remaining > 0; --remaining) {
        while (!heap.empty() &&
               (!alive[std::get<2>(heap.top())] ||
                std::get<3>(heap.top()) != version[std::get<2>(heap.top())])) heap.pop();
        if (heap.empty()) throw std::logic_error("nfa_to_regex: invalid elimination queue");
        int k = std::get<2>(heap.top()); heap.pop();
        alive[k] = 0;
        std::vector<std::pair<int, int>> pred, succ;
        for (int u : incoming[k]) if (u != k) pred.emplace_back(u, outgoing[u].at(k));
        for (const auto& edge : outgoing[k]) if (edge.first != k) succ.push_back(edge);
        std::sort(pred.begin(), pred.end());
        std::sort(succ.begin(), succ.end());
        auto loop = outgoing[k].find(k);
        int repeat = expressions.star(loop == outgoing[k].end() ? 0 : loop->second);
        // All labels are snapshots: inserting new edges cannot invalidate these.
        for (const auto& a : pred) {
            int prefix = expressions.concat(a.second, repeat);
            for (const auto& b : succ)
                add_edge(a.first, b.first, expressions.concat(prefix, b.second));
        }
        for (int u : incoming[k]) if (u != k) {
            outgoing[u].erase(k); --edge_count; mark(u);
        }
        for (const auto& edge : outgoing[k]) {
            incoming[edge.first].erase(k); --edge_count; mark(edge.first);
        }
        incoming[k].clear(); outgoing[k].clear();
        for (int u : touched) { dirty[u] = 0; if (alive[u]) push_score(u); }
        touched.clear();
        if (heap.size() > std::size_t(remaining) * 8 + 1024) {
            decltype(heap) fresh;
            heap.swap(fresh);
            for (int u = 0; u < count; ++u) if (alive[u]) push_score(u);
        }
    }
    auto answer = outgoing[start].find(finish);
    return expressions.serialize(answer == outgoing[start].end() ? 0 : answer->second);
}

} // namespace fa
