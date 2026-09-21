#pragma once

#include "core.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace fa {

// Hopcroft minimization. Missing transitions mean rejection, so first add a
// sink when necessary and remove unreachable states. The result is total.
// Time: O(n + k*n*log(n+1)); space: O(k*n+n), including normalization.
inline DFA minimize_dfa(const DFA& input) {
    const DFA dfa = complete_reachable(input);
    const int n = static_cast<int>(dfa.trans.size());
    const int k = static_cast<int>(dfa.alphabet.size());

    int final_count = 0;
    for (const auto value : dfa.accept) final_count += (value != 0);
    if (final_count == 0 || final_count == n) {
        DFA result;
        result.start = 0;
        result.alphabet = dfa.alphabet;
        result.trans.assign(1, std::vector<int>(k, 0));
        result.accept.assign(1, static_cast<std::uint8_t>(final_count != 0));
        return result;
    }

    // CSR inverse transitions. For symbol c, predecessors of q are in
    // predecessor[c][offset[c][q] .. offset[c][q+1]).
    std::vector<std::vector<int>> offset(
        k, std::vector<int>(static_cast<std::size_t>(n) + 1, 0));
    std::vector<std::vector<int>> predecessor(k, std::vector<int>(n));
    for (int c = 0; c < k; ++c) {
        for (int q = 0; q < n; ++q) ++offset[c][dfa.trans[q][c] + 1];
        for (int q = 0; q < n; ++q) offset[c][q + 1] += offset[c][q];
        std::vector<int> cursor = offset[c];
        for (int q = 0; q < n; ++q) {
            predecessor[c][cursor[dfa.trans[q][c]]++] = q;
        }
    }

    struct Block {
        int begin;
        int end;
        std::vector<std::uint8_t> pending;
    };
    std::vector<Block> blocks;
    blocks.reserve(n);  // At most n nonempty blocks; no references can move.
    blocks.push_back({0, n - final_count, std::vector<std::uint8_t>(k, 0)});
    blocks.push_back({n - final_count, n, std::vector<std::uint8_t>(k, 0)});

    // Every block owns an interval of one global permutation. Swapping a
    // marked state to the interval's right end relocates it in O(1), without
    // scanning the other half or retaining oversized per-block allocations.
    std::vector<int> order(n), position(n), belong(n);
    int reject_cursor = 0;
    int accept_cursor = n - final_count;
    for (int q = 0; q < n; ++q) {
        const bool accepting = dfa.accept[q] != 0;
        const int p = accepting ? accept_cursor++ : reject_cursor++;
        order[p] = q;
        position[q] = p;
        belong[q] = accepting ? 1 : 0;
    }

    std::deque<std::pair<int, int>> work;
    const auto enqueue = [&](int block, int symbol) {
        if (!blocks[block].pending[symbol]) {
            blocks[block].pending[symbol] = 1;
            work.emplace_back(block, symbol);
        }
    };
    const int initial = (n - final_count <= final_count) ? 0 : 1;
    for (int c = 0; c < k; ++c) enqueue(initial, c);

    // Each state has exactly one successor for the current symbol, hence it
    // occurs at most once in a splitter's inverse image. No visited bitset or
    // O(n) clearing pass is necessary.
    std::vector<int> marked_head(n, -1), marked_count(n, 0), marked_next(n, -1);
    std::vector<int> touched;
    touched.reserve(n);

    while (!work.empty()) {
        const auto [splitter, symbol] = work.front();
        work.pop_front();
        blocks[splitter].pending[symbol] = 0;
        touched.clear();

        // Capture the entire inverse image BEFORE splitting any block. The
        // splitter itself may be divided below; iterating it during mutation
        // would silently miss states and invalidate Hopcroft's invariant.
        for (int p = blocks[splitter].begin; p < blocks[splitter].end; ++p) {
            const int target = order[p];
            for (int e = offset[symbol][target]; e < offset[symbol][target + 1]; ++e) {
                const int state = predecessor[symbol][e];
                const int block = belong[state];
                if (marked_count[block]++ == 0) touched.push_back(block);
                marked_next[state] = marked_head[block];
                marked_head[block] = state;
            }
        }

        for (const int block : touched) {
            const int count = marked_count[block];
            int state = marked_head[block];
            marked_count[block] = 0;
            marked_head[block] = -1;
            const int old_end = blocks[block].end;
            const int old_size = old_end - blocks[block].begin;
            if (count == old_size) continue;

            const int added = static_cast<int>(blocks.size());
            const int boundary = old_end - count;
            blocks.push_back({boundary, old_end, std::vector<std::uint8_t>(k, 0)});
            int write = old_end;
            while (state != -1) {
                const int next = marked_next[state];
                const int from = position[state];
                const int displaced = order[--write];
                std::swap(order[from], order[write]);
                position[state] = write;
                position[displaced] = from;
                belong[state] = added;
                state = next;
            }
            blocks[block].end = boundary;

            const int smaller = (old_size - count <= count) ? block : added;
            for (int c = 0; c < k; ++c) {
                if (blocks[block].pending[c]) {
                    // An old queued splitter is replaced by both pieces:
                    // its existing entry now names the unmarked piece.
                    enqueue(added, c);
                } else {
                    enqueue(smaller, c);
                }
            }
        }
    }

    // Quotient the stable partition and number states by BFS, following the
    // caller's alphabet order. This gives a deterministic, reachable output.
    DFA result;
    result.start = 0;
    result.alphabet = dfa.alphabet;
    std::vector<int> new_id(blocks.size(), -1), queue;
    queue.reserve(blocks.size());
    const int start_block = belong[dfa.start];
    new_id[start_block] = 0;
    queue.push_back(start_block);
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const int representative = order[blocks[queue[i]].begin];
        result.accept.push_back(static_cast<std::uint8_t>(dfa.accept[representative] != 0));
        std::vector<int> row(k);
        for (int c = 0; c < k; ++c) {
            const int target_block = belong[dfa.trans[representative][c]];
            if (new_id[target_block] == -1) {
                new_id[target_block] = static_cast<int>(queue.size());
                queue.push_back(target_block);
            }
            row[c] = new_id[target_block];
        }
        result.trans.push_back(std::move(row));
    }
    return result;
}

}  // namespace fa
