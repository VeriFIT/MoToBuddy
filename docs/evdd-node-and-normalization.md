---
title: "MoToBuDDy EVDD - node layouts and normalization"
subtitle: "Design brief (from the supervisor canvas)"
geometry: margin=2.2cm
fontsize: 11pt
mainfont: DejaVu Sans
monofont: DejaVu Sans Mono
colorlinks: true
header-includes:
  - \usepackage{longtable}
  - \usepackage{booktabs}
  - \usepackage{array}
  - \usepackage{xcolor}
  - \usepackage{needspace}
---

Design brief for extending MoToBuDDy with **multiplicative** edge-valued decision diagrams, without changing the existing 20-byte BDD/MTBDD node. Separate EVDD kernel, same executable as MTBDD, for later comparison of MEDUSA-style simulation.

| | |
|---|---|
| Current BDD/MTBDD node | **20 B** (verified with gcc on this tree) |
| Recommended EVDD node | **28 B** |
| Kernels | `bdd_*` and `evdd_*` in one process |
| Normalization | Pluggable, frozen per type |

**Hard constraint.** Existing BDD/MTBDD nodes stay 20 bytes. EVDD gets its own node table and globals. When EVDD is unused, MTBDD behaviour and layout are unchanged.

# Nomenclature

| Term | Meaning |
|---|---|
| **Weight-normalize** (`evdd_value_normalize`) | Local rewrite of one node's two outgoing weights: $(w_0,w_1)\to(w_0',w_1',\mathrm{factor})$. Does not allocate nodes. |
| **Diagram normalize** (`evdd_normalize`) | Bottom-up DAG walk: at each node, weight-normalize then unique-table insert. Makes a raw-built diagram canonical. |
| **Unique lookup** | Hash/insert on key `(level, low, high, w_low, w_high)` as stored on the node. |
| **Skip-reduction** | ROBDD-style "both cofactor edges equal → drop this node" and return that edge. Used in `evdd_makenode` / `evdd_makenode_raw`. |

Public root is always an edge `EVDD { node, w }`, not a bare node id. Incoming root weight is held only on that handle; the node table does not store incoming edges.

# What an EVDD node represents

A diagram is an incoming edge to a node. Each internal node has two outgoing edges. Path value is the **product** of weights from the root to the distinguished terminal 1. This is multiplicative, not additive.

Incoming $w$ times node $N$, with outgoing $w_0$ to $A$ and $w_1$ to $B$:

$$
f(0)=w\cdot w_0\cdot A,\qquad f(1)=w\cdot w_1\cdot B.
$$

# Candidate node representations

All three can implement EVDD semantics. They differ in size, cache behaviour, and whether every normalization strategy fits without a later compact form.

### A - two weights in the node (recommended)

- low child, high child
- low ValueID, high ValueID
- refcount, level, mark, hash, next
- **28 bytes**, one memory load

### B - separate edge table

- node stores two EdgeIDs (20 B)
- each edge: child + ValueID (8 B)
- **36 B** per node plus a second array
- extra loads on every cofactor

### C - implicit unit edge

- low child, high child
- only one stored ValueID (**24 B**)
- other outgoing weight assumed 1
- **cannot** represent L2, e.g. $(0.6, 0.8)$

| Approach | Bytes / node | Cofactor access | L2 / neither edge is 1 | Role |
|---|---|---|---|---|
| **A. Two ValueIDs in EvddNode** | 28 | Single load of the node | Yes - both weights stored | Initial general representation |
| B. Edge table (EdgeID → child + weight) | 20 + 16 = 36 | Node, then two edge rows | Yes, at higher cost | Reject for v1 (indirection, more RAM) |
| C. One ValueID, other edge implicit 1 | 24 | Single load | **No** | Later packing, only for unit-edge strategies |

Sizes assume 32-bit child ids and ValueIDs, same packing as the current 20-byte `BddNode` (union + 10/21/1-bit header + hash + next). Edge values live in a shared intern table, not in the node. Source: `sizeof(BddNode) = 20` on this tree (gcc); `sizeof(EvddNode) = 28`.

# Why not grow the existing node

Putting EVDD fields on `BddNode` would enlarge every BDD and MTBDD node even when EVDD is unused. BuDDy is plain C with process-global state, so the clean split is a second kernel: `evddnodes`, `evdd_init`, `evdd_makenode`, beside `bdd_init`. Both can run in one process for MTBDD-vs-EVDD oracles.

# Weight-normalization (pluggable, frozen per type)

Weight-normalization rewrites the two outgoing weights and pushes a factor onto the incoming edge. The kernel stores whatever pair the strategy returns (eager path) or the raw pair (`makenode_raw`).

| Strategy | Rule (binary node) | Guarantees an edge of 1? | Needs from the value domain | Notes |
|---|---|---|---|---|
| First nonzero / low-edge | If $w_0 \neq 0$: divide both by $w_0$ (low becomes 1). Else divide by $w_1$. Push the divisor up. | Yes | `is_zero`, mul, div | Simple; Q-Sylvan reports it as numerically fragile for floats |
| Max-magnitude | Divide by the nonzero edge of largest $\|w\|$. Deterministic tie-break (prefer low on ties). Keep the edge's sign. | Yes | `is_zero`, $\|w\|$ compare, mul, div | Q-Sylvan default for complex floats; good for quantum amplitudes |
| L2 | Divide $(w_0,w_1)$ by $\sqrt{\|w_0\|^2+\|w_1\|^2}$. Example: $(3,4)\to(0.6,0.8)$. | **No** | add, abs², sqrt, div | Why v1 must store two weights. Not defined for every exact/symbolic type |
| User callback | Returns normalized $(w_0,w_1)$ and the incoming factor. Kernel does unique-table and refs. | Only if the user chooses that | Whatever the callback uses | Required for exact-algebraic or MEDUSA-style symbolic weights |

**Kernel vs strategy.** The callback does not allocate nodes. It only maps $(w_0,w_1)$ to $(w_0',w_1',\mathrm{factor})$. Eager `evdd_makenode` then hashes `(level, low, high, w0', w1')` and returns the edge `(factor, node)`.

**Skip-reduction.** If both outgoing edges are equal (same child node and same weight ValueID), makenode returns that edge instead of allocating a node. Same idea as BuDDy `checkSameChildren`, extended to weighted edges. Runs in both eager and raw paths (after weight-normalize for eager).

**Value domain is independent.** Built-ins (double, int, later complex double) register mul/div themselves. CUSTOM registers hash, equal, free, mul, and whatever the chosen strategy needs. If a type cannot implement L2 (no sqrt), that strategy is rejected at set-normalization, not in the middle of makenode.

# Types and normalization (locked policy)

Follow MTBDD CUSTOM: several type ids share one unique table. The flag lives on the interned value row, not on `EvddNode` and not as a second `SETDOMAIN` union. Mixing storage layouts (a global `double[]` together with CUSTOM pointers) is not allowed. Built-in double or complex, if present, are registered type ids with stock callbacks, so they use the same mechanism as CUSTOM.

| Situation | Allowed? |
|---|---|
| Two CUSTOM type ids in one run (e.g. exact and symbolic) | **Yes** - intern row carries `type_id`; ValueIDs do not alias |
| Same `type_id`, two normalization strategies in one unique table | **No** - canonicity would mix two representatives of the same algebra |
| Different `type_id`s, each with its own frozen normalization | **Yes** - max-magnitude on doubles, first-nonzero on exact, same `evddnodes` |
| Apply / mul / makenode mixing two `type_id`s on one node | **No** - hard error (stricter than MTBDD apply, which only prints) |

Weight-normalization strategy is set per `type_id` at registration and frozen until `evdd_done`. Translate walks the source, interns converted payloads under the destination `type_id`, then makenode so the destination strategy applies. ValueID 0 is bare absorb only; identity is a typed intern (`is_one` / `evdd_value_unit`), not a reserved id.

**Second manager.** Needed only if the same algebra must exist under two norms at once (double + L2 and double + max-magnitude). Out of scope: one type, one strategy.

# Makenode API (eager vs raw)

Both paths share unique-table lookup/insert and skip-reduction. They differ only by whether weight-normalization runs before the key is formed.

| API | Weight-normalize? | Unique lookup? | Skip-reduce? | Canonical? |
|---|---|---|---|---|
| `evdd_makenode(level, low, high)` | **Yes** (local `evdd_value_normalize`) | Yes on normalized key | Yes | Yes if children are already canonical |
| `evdd_makenode_raw(level, low, high)` | **No** | Yes on raw key | Yes | No (identical raw subgraphs still share) |
| `evdd_normalize(root)` | Yes at every node (bottom-up) | Yes on normalized keys | Yes | **Yes** - same function as eager makenode throughout |

Natural uses of raw + diagram normalize: bulk MTBDD→EVDD, then one `evdd_normalize`; building a circuit fragment without normalizing every intermediate, then normalize once.

Do not apply two raw (non-diagram-normalized) diagrams unless apply also diagram-normalizes, or you call `evdd_normalize` first. Same type still has one frozen weight-normalization strategy; `evdd_normalize` does not switch norms.

# Garbage collection

**Nodes.** `evdd_gbc` marks from nodes with `refcou > 0` (external roots, `evdd_addref`) and from an internal reference stack, then sweeps the rest onto the free list. Operations (`evdd_apply`, `evdd_normalize`, `evdd_makenode`) push the nodes they still need onto that stack instead of adding references, as BuDDy does with `bddrefstack`. Stack entries never saturate, so a node that many memo entries share is still collected once the operation returns. A collection can run inside any operation that creates nodes; results an operation returns are unreferenced, as in BuDDy, so a caller that keeps one across another operation must `evdd_addref` it.

# Planned MTBDD ↔ EVDD transformation

Both kernels stay live in one process (existing 20-byte `bddnodes` plus `evddnodes`). Conversion is an explicit walk, not pointer punning. Same CUSTOM payload `type_id` on both sides when the algebra matches. Used as the correctness oracle: apply a gate on each, convert, compare the represented vector.

| Direction | Idea | Caveats |
|---|---|---|
| MTBDD → EVDD | Replace each valued leaf with an edge `(leaf_value, T1)`. Internal edges start as identity. Bottom-up makenode (or raw then `evdd_normalize`) applies that type's weight-normalization, so equal-up-to-scalar subvectors can merge. | Shape may shrink vs the MTBDD. Boolean 0 maps to `(T1, ZERO)`; boolean 1 to `(T1, unit)`. |
| EVDD → MTBDD | Multiply weights along each path; write the product as an MTBDD terminal. Incoming root weight scales every leaf. Skipped EVDD levels expand to both cofactors with the same subgraph if the MTBDD side needs an explicit node. | Node count can grow (no edge sharing of scalars). Mixed EVDD type ids are an error unless a value mapper is supplied. |

API sketch: `mtbdd_to_evdd(BDD, type_id)` and `evdd_to_mtbdd(EVDD, type_id)`, cached like apply. Round-trip need not preserve node identity, only the function (path products equal leaf values).

# Recommended starting point

Layout A: two interned ValueIDs per node, 28 B, one EVDD unique table. Intern rows are `(type_id, payload)`. CUSTOM-style multi-type; one frozen weight-normalization per `type_id`. Hard error on mixed-type ops. Skip-reduction on equal cofactor edges. Eager `evdd_makenode` or raw + `evdd_normalize`. Compact form C later, only for unit-edge strategies. Existing 20-byte BDD/MTBDD nodes unchanged. MTBDD ↔ EVDD conversion is a planned oracle, not v1 kernel work: uniquing and gates first, then translate, then gate-by-gate compare.
