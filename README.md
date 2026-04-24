# MoToBuDDy

A fork of the [BuDDy BDD library](https://github.com/SSoelvsten/buddy) extended with full Multi-Terminal BDD (MTBDD) support, a JSON-based gate serialization format (MOSF), and quantum gate simulation examples.

---

## Features

- Full BDD library inherited from BuDDy 2.4.0
- MTBDD support with custom terminal types (arbitrary pointer-sized leaves)
- High-level C++ combinator API for composing MTBDD tree transformations
- MOSF: a JSON serialization format for gate operations, with a streaming parser
- Quantum gate examples: CX, CCX (Toffoli), and Hadamard over alphabet MTBDDs

---

## Building

Requirements: CMake ≥ 3.12, a C++17-capable compiler, tcsh (for running tests), internet access on first configure (downloads nlohmann/json automatically).

```bash
mkdir build && cd build
cmake ..
make
```

The build system fetches [nlohmann/json](https://github.com/nlohmann/json) automatically on first configure. No manual header installation is required.

---

## MTBDD Extension

The MTBDD extension adds multi-terminal BDDs on top of the BuDDy kernel. Unlike standard BDDs whose leaves are always 0 or 1, MTBDDs carry arbitrary typed values at their leaves.

### Terminal Types

Custom terminal types are registered at runtime:

```c
unsigned leaf_id = mtbdd_new_terminal_type();
mtbdd_register_compare_function(leaf_id, my_cmp);
mtbdd_register_hash_function(leaf_id, my_hash);
mtbdd_register_to_str_function(leaf_id, my_tostr);
```

Each terminal is created with:

```c
BDD node = mtbdd_maketerminal((void*)my_value, leaf_id);
```

### Apply Operations

Binary operations over terminal values are applied with:

```c
BDD result = mtbdd_apply(left, right, op_function);
```

Where `op_function` has signature `void*(void*, void*)` and returns a newly allocated value.

### Combinator API (`mtbddop.h`)

A composable C++ API for building tree transformations without explicit recursion:

| Combinator | Description |
|---|---|
| `mtbdd_with_traverse_to(level, action)` | Descend to `level`, apply `action` |
| `mtbdd_with_lockstep_to(level, action)` | Descend two BDDs in lockstep to `level`, apply binary `action` |
| `mtbdd_make_swap()` | Swap LOW and HIGH children of a node |
| `mtbdd_make_swap(paramL, paramR)` | Parameterised swap exchanging values between two nodes |
| `mtbdd_get_side(Side::L / Side::R)` | Extract one child of a node |

`Branch` controls traversal direction (`L`, `R`, `LR`, `RL`, `ITSELF`) and which subtree receives the action.

#### Example -- CX gate (control above target)

```cpp
auto cx = mtbdd_with_traverse_to(
    c,
    mtbdd_with_traverse_to(t, mtbdd_make_swap()),
    Branch::LR, Branch::R   // follow HIGH at control level only
);
BDD result = cx(tree);
```

---

## MOSF -- MTBDD Operation Serialization Format

MOSF (version 1.0) is a JSON format that serializes gate operations built from the combinator API. A MOSF file is parsed into a streaming sequence of `NodeOp` values ready to apply to an MTBDD.

MOSF file format and examples can be found in doc/mosf.mosf

### Parser API

```cpp
mosf::ExtensionRegistry reg;
reg.binary_ops["plus"] = [](const json&) -> BinaryNodeOp { ... };

mosf::MOSFFile circuit = mosf::load("gate.mosf", reg);

while (circuit.has_next()) {
    mosf::Event ev = circuit.next_unrolled();
    for (size_t i = 0; i < ev.op_repeat; ++i)
        result = ev.op(result);
}
```

`next()` emits `GROUP_BEGIN` / `GROUP_END` / `OP` events preserving group structure. `next_unrolled()` suppresses group events and folds all repeat counts into each `OP`'s `op_repeat`. Call `reset()` before switching between the two.

### Example -- CX gate MOSF file

```json
{
  "mosf_version": "1.0",
  "tree": "tree_id",
  "vars": { "c": 1, "t": 2 },
  "defs": {
    "high_swap": {
      "put_up": { "type": "get_side", "side": "R" },
      "put_in": {
        "type": "makenode",
        "low":  { "type": "get_side", "side": "L" },
        "high": { "type": "received" }
      }
    }
  },
  "ops": [
    {
      "type":   "traverse_to",
      "target": { "var": "t" },
      "action": {
        "type":   "lockstep_to",
        "target": { "var": "c" },
        "action": {
          "type":   "swap",
          "paramL": { "$ref": "high_swap" },
          "paramR": { "$ref": "high_swap" }
        }
      }
    }
  ]
}
```

---

## Quantum Gates Example (`examples/quantum_gates`)

Demonstrates CX, CCX (Toffoli), and MOSF-driven gate simulation over an alphabet MTBDD. The MTBDD encodes basis states as leaves A–Z over a perfect binary tree, with level 0 as the most significant bit (root).

### Usage

```
quantum_gates <num_letters> cx  <c> <t>
quantum_gates <num_letters> ccx <c1> <c2> <t>
quantum_gates <num_letters> <gate.mosf>
```

| Argument | Description |
|---|---|
| `num_letters` | Number of alphabet leaves (1–26); padded to next power of 2 |
| `cx` | Single-control CX gate: control `c`, target `t` |
| `ccx` | Double-control CCX gate: controls `c1`, `c2` (auto-sorted), target `t` |
| `<gate.mosf>` | Path to a MOSF file describing an arbitrary gate |

Level indices start at 0 (root). A control is active when the HIGH branch is taken at that level.

### Running

After `make`:

```bash
# CX gate: 8 letters, control=0, target=1
./build/examples/quantum_gates/quantum_gates 8 cx 0 1

# CCX gate: 8 letters, controls=0,1, target=2
./build/examples/quantum_gates/quantum_gates 8 ccx 0 1 2

# MOSF-driven gate
./build/examples/quantum_gates/quantum_gates 8 file.mosf
```

Output is a Graphviz DOT representation of the resulting MTBDD, suitable for piping into `dot -Tpng`.

```bash
./build/examples/quantum_gates/quantum_gates 8 cx 0 1 | dot -Tpng -o result.png
```

---

## Project Structure

```
src/
  bdd.h / kernel.c      -- BuDDy BDD core
  mtbdd.h / mtbdd.c     -- MTBDD terminal layer
  mtbddop.h / mtbddop.cxx -- Combinator API
  mosf_parser.h         -- MOSF JSON parser
doc/
  mosf.mosf             -- MOSF format reference and examples
examples/
  quantum_gates/        -- CX, CCX, MOSF quantum gate demo
```

---

## License

Inherited from BuDDy. See [LICENSE.md](LICENSE.md).
