# 实际验证记录

验证日期：2026-09-11。环境：Windows x64，GCC 15.2.0（MinGW-w64）、Clang 19.1.7（llvm-mingw），C++17。

## 已通过的检查

| 检查 | 结果 |
|---|---|
| 6 个共享头文件版本，GCC Release / CMake 构建 | 全部成功 |
| 6 个 standalone 单文件版本，Clang `-std=c++17 -O2 -Wall -Wextra -Wpedantic` | 全部成功，无警告 |
| CTest `automata_properties` | 1/1 通过 |
| 核心算法测试，GCC `-O2` | 49,398 项检查通过 |
| 同一核心测试，GCC `-O0 -g -D_GLIBCXX_DEBUG` | 49,398 项检查通过 |
| 六个 GCC 可执行程序的 CLI 测试 | 1,043 项检查通过 |
| 六个 Clang standalone 可执行程序的 CLI 测试 | 1,043 项检查通过 |

核心测试输出：

```text
PASS: 49398 checks; deterministic seed 0x5EED2026; exact equivalence + independent simulation/table filling.
```

命令行测试输出：

```text
PASS: 1043 CLI checks; pipelines, round trips, byte alphabets, strict errors and limits.
```

测试包含独立实现的 ε-NFA 模拟、朴素子集构造、DFA 乘积图等价性判定、表填充最小状态数检查，以及随机输入和固定边界输入。另有 8,192 个 DFA 子集的构造，验证哈希表扩容及最小化；关闭缓存后仍保持语言一致。

CLI 测试实际调用程序，覆盖正规式→NFA→DFA→最小 DFA 的串接、自动机→正规式的往返、DFA→NFA、字节转义、空语言、空串、空字母表、错误格式、错误选项、状态上限、缓存与输出限制。测试比较字节语义，允许等价结果的状态编号和字母表顺序不同。

## 复现

在项目目录执行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
python tests/test_cli.py build
g++ -std=c++17 -O0 -g -D_GLIBCXX_DEBUG tests/test_automata.cpp -o test_debug
```

运行 `test_debug`；Windows 为 `test_debug.exe`。Windows MinGW 配置 CMake 时加 `-G "MinGW Makefiles"`。

这些检查提供实现正确性的验证证据；算法的不变量、复杂度及有限资源限制见 README。未声称测试覆盖所有可能输入或证明不存在缺陷。
