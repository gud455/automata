#!/usr/bin/env python3
"""Portable end-to-end tests; usage: python tests/test_cli.py BUILD_DIRECTORY."""

import argparse
from collections import deque
from pathlib import Path
import subprocess
import sys


class Suite:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.checks = 0

    def check(self, condition, message):
        self.checks += 1
        if not condition:
            raise AssertionError(message)

    def run(self, name, data, args=(), succeeds=True):
        executable = self.directory / name
        if not executable.is_file():
            executable = self.directory / (name + ".exe")
        self.check(executable.is_file(), "missing executable: " + str(executable))
        if isinstance(data, str):
            data = data.encode("ascii")
        result = subprocess.run(
            [str(executable), *args], input=data, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=30, check=False,
        )
        label = name + " " + " ".join(args)
        self.check((result.returncode == 0) == succeeds,
                   "unexpected exit from " + label + ": " +
                   repr(result.returncode) + "; stderr=" + repr(result.stderr) +
                   "; input=" + repr(data[:500]))
        if succeeds:
            self.check(not result.stderr, "unexpected diagnostics from " + label)
        else:
            self.check(result.stderr.startswith(b"ERROR:"),
                       "missing error diagnostic from " + label)
            self.check(not result.stdout, "partial output on failure from " + label)
        return result.stdout

    def parse_dfa(self, serialized):
        tokens = serialized.split()
        self.check(bool(tokens) and tokens[0] == b"DFA", "invalid serialized DFA header")
        n, k, start, final_count = map(int, tokens[1:5])
        self.check(len(tokens) == 5 + k + final_count + n * k,
                   "invalid serialized DFA token count")
        alphabet = [int(token, 16) for token in tokens[5:5 + k]]
        finals = {int(token) for token in tokens[5 + k:5 + k + final_count]}
        transitions = list(map(int, tokens[5 + k + final_count:]))
        table = [transitions[state * k:(state + 1) * k] for state in range(n)]
        return n, start, alphabet, finals, table

    def equivalent(self, first, second, context):
        left, right = self.parse_dfa(first), self.parse_dfa(second)
        symbols = sorted(set(left[2]) | set(right[2]))
        left_columns = {byte: index for index, byte in enumerate(left[2])}
        right_columns = {byte: index for index, byte in enumerate(right[2])}

        def step(dfa, columns, state, byte):
            if state < 0 or byte not in columns:
                return -1
            return dfa[4][state][columns[byte]]

        first_pair = (left[1], right[1])
        visited = {first_pair}
        pending = deque([(first_pair, b"")])
        while pending:
            (a, b), word = pending.popleft()
            self.check((a in left[3]) == (b in right[3]),
                       context + ": counterexample " + repr(word))
            for byte in symbols:
                pair = (step(left, left_columns, a, byte),
                        step(right, right_columns, b, byte))
                if pair not in visited:
                    visited.add(pair)
                    pending.append((pair, word + bytes([byte])))

    def minimize_nfa(self, nfa):
        return self.run("dfa_minimize", self.run("nfa_to_dfa", nfa))

    def round_trip(self, pattern):
        nfa = self.run("regex_to_nfa", pattern)
        dfa = self.run("nfa_to_dfa", nfa)
        minimal = self.run("dfa_minimize", dfa)
        direct = self.run("regex_to_min_dfa", pattern)
        self.check(minimal == direct, "composed/direct serialization mismatch: " + pattern)
        regenerated = self.run("automaton_to_regex", minimal)
        recovered = self.run("regex_to_min_dfa", regenerated)
        self.equivalent(minimal, recovered, "regex round trip " + pattern)
        lifted = self.run("dfa_to_nfa", dfa)
        self.equivalent(minimal, self.minimize_nfa(lifted), "DFA -> NFA round trip " + pattern)
        direct_regex = self.run("automaton_to_regex", nfa)
        self.equivalent(minimal, self.run("regex_to_min_dfa", direct_regex),
                        "direct NFA -> regex " + pattern)
        uncached = self.run("nfa_to_dfa", nfa, ["--cache-mib", "0"])
        self.check(uncached == dfa, "cache-disabled serialization mismatch")

    def positive_cases(self):
        for pattern in ["(a|b)*abb", "#", "@", "#*", "#a", "(@|a)*", "a?b+",
                        r"\|\x00\xFF", r"\x20", r"a|\@"]:
            self.round_trip(pattern)

        empty_alphabet = [
            (b"NFA\n1 0 0 0 0\n", False),
            (b"NFA\n3 0 0 1 3\n2\n0 1 eps\n1 2 eps\n2 1 eps\n", True),
        ]
        for nfa, accepts_empty in empty_alphabet:
            minimal = self.minimize_nfa(nfa)
            n, start, alphabet, finals, _ = self.parse_dfa(minimal)
            self.check(n == 1 and not alphabet and (start in finals) == accepts_empty,
                       "incorrect empty-alphabet language")
            regex = self.run("automaton_to_regex", nfa)
            self.equivalent(minimal, self.run("regex_to_min_dfa", regex),
                            "empty alphabet -> regex")

        partial = b"DFA\n4 2 1 2\nFF 00\n2 3\n0 0\n2 -1\n-1 2\n3 3\n"
        minimal = self.run("dfa_minimize", partial)
        self.equivalent(partial, minimal, "partial DFA with byte alphabet")
        self.equivalent(partial, self.minimize_nfa(self.run("dfa_to_nfa", partial)),
                        "partial DFA -> NFA")
        regex = self.run("automaton_to_regex", partial)
        self.equivalent(partial, self.run("regex_to_min_dfa", regex),
                        "partial DFA -> regex")

    def malformed_inputs(self):
        malformed_nfas = [
            "", "DFA\n1 0 0 0\n", "nfa\n1 0 0 0 0\n", "NFA\n",
            "NFA\n0 0 0 0 0", "NFA\n-1 0 0 0 0", "NFA\n1000001 0 0 0 0",
            "NFA\n1x 0 0 0 0", "NFA\n999999999999999999999999 0 0 0 0",
            "NFA\n1 0 1 0 0", "NFA\n1 257 0 0 0", "NFA\n1 0 0 2 0",
            "NFA\n1 0 0 0 -1", "NFA\n1 0 0 0 256000001",
            "NFA\n1 2 0 0 0\n61 61", "NFA\n1 1 0 0 0\nG0",
            "NFA\n1 1 0 0 0\n6", "NFA\n1 1 0 0 0\n000",
            "NFA\n1 0 0 1 0\n1", "NFA\n2 0 0 2 0\n0 0",
            "NFA\n1 0 0 0 1\n0 1 eps", "NFA\n1 0 0 0 1\n-1 0 eps",
            "NFA\n1 0 0 0 1\n0 0", "NFA\n1 1 0 0 1\n61\n0 0 62",
            "NFA\n1 0 0 0 1\n0 0 EPS", "NFA\n1 0 0 0 0\ntrailing",
        ]
        for data in malformed_nfas:
            self.run("nfa_to_dfa", data, succeeds=False)

        malformed_dfas = [
            "", "NFA\n1 0 0 0 0\n", "DFA\n0 0 0 0", "DFA\n1 0 -1 0",
            "DFA\n1 1 0 0\n61", "DFA\n1 1 0 0\n61\n-2",
            "DFA\n1 1 0 0\n61\n1", "DFA\n2 0 0 2\n0 0",
            "DFA\n1 2 0 0\n61 61\n0 0", "DFA\n1 0 0 0\ntrailing",
        ]
        for data in malformed_dfas:
            self.run("dfa_minimize", data, succeeds=False)
            self.run("dfa_to_nfa", data, succeeds=False)
        self.run("automaton_to_regex", "INVALID\n1 0 0 0", succeeds=False)
        self.run("automaton_to_regex", "NFA\n1 0 0 0 0\ntrailing", succeeds=False)
        self.run("automaton_to_regex", "DFA\n1 1 0 0\n61\n5", succeeds=False)
        for regex in ["", " ", "a|", "|a", "()", "(a", "a)", ".a", "a..b", "*a",
                      "\\", r"\x0", r"\xGG"]:
            self.run("regex_to_nfa", regex, succeeds=False)
            self.run("regex_to_min_dfa", regex, succeeds=False)

    def argument_cases(self):
        nfa = b"NFA\n1 0 0 0 0\n"
        dfa = b"DFA\n1 0 0 0\n"
        for name, data in [("regex_to_nfa", "a"), ("nfa_to_dfa", nfa),
                           ("dfa_minimize", dfa), ("automaton_to_regex", nfa),
                           ("regex_to_min_dfa", "a"), ("dfa_to_nfa", dfa)]:
            self.run(name, data, ["--unknown", "1"], succeeds=False)
        for args in [["--max-states"], ["--max-states", "-1"], ["--max-states", "1x"],
                     ["--max-states", "0"], ["--max-states", ""],
                     ["--max-states", "2", "--max-states", "3"],
                     ["--max-states", "999999999999999999999999999999"],
                     ["--cache-mib", "18446744073709551615"], ["--bitset-mib", "0"]]:
            self.run("nfa_to_dfa", nfa, args, succeeds=False)
        self.run("regex_to_nfa", "ab", ["--max-states", "1"], succeeds=False)
        self.run("regex_to_nfa", "@", ["--max-states", "0"], succeeds=False)
        self.run("regex_to_min_dfa", "ab", ["--max-nfa-states", "1"], succeeds=False)
        self.run("regex_to_min_dfa", "ab", ["--max-states", "1"], succeeds=False)
        self.run("regex_to_min_dfa", "@", ["--cache-mib", "0"])
        ab = self.run("regex_to_nfa", "ab")
        self.run("automaton_to_regex", ab, ["--max-output", "1"], succeeds=False)
        self.run("automaton_to_regex", ab, ["--max-nodes", "0"], succeeds=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory", help="directory containing the six executables")
    args = parser.parse_args()
    suite = Suite(args.build_directory)
    try:
        suite.positive_cases()
        suite.malformed_inputs()
        suite.argument_cases()
    except (AssertionError, OSError, subprocess.TimeoutExpired, ValueError) as error:
        print("FAIL after {} checks: {}".format(suite.checks, error), file=sys.stderr)
        return 1
    print("PASS: {} CLI checks; pipelines, round trips, byte alphabets, strict errors and limits."
          .format(suite.checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
