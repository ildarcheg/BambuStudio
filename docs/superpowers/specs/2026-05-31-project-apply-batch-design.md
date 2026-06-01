# `project apply` — Batch Manifest Verb — Design (2026-05-31)

## Goal

Add a single `bambu-cli project apply` verb that consumes a JSON manifest
describing an ordered list of mutations and applies all of them in one
load / mutate / save cycle. The motivating workflow: build a 12-plate
`.3mf` from a template, populating each plate with objects and per-object
or project-level config keys. Today this requires 30+ individual CLI
invocations, each paying the full `load_project` → mutate → `save_project`
(with 3-check invariant guard) round-trip. The dominant cost is the save
side, not the mutation. Collapsing the entire batch to one save makes the
workflow tractable.

Primary use case: scripted pipelines that emit a manifest from a higher-
level tool (build scripts, configurators) and then materialise a `.3mf`
in a single CLI call.

## Architecture

- One new mutating verb `project apply`, registered alongside the
  existing `project init` / `project info` / etc. in the `project`
  subcommand group.
- Applies to an **existing** `.3mf` (positional `in.3mf` argument,
  required, same as every other mutating verb). Does not replace
  `project init` and does not support from-scratch creation in v1 —
  the typical use is to compose against a barebones template project.
- Manifest is **imperative**: an ordered `operations` array of objects,
  each naming a verb and its arguments. 1:1 mapping to existing
  `project_ops` functions; no diff-style declarative semantics.
- Dispatch is **centralized** in a new `commands/project_apply.cpp`,
  using a `std::unordered_map<std::string, OpHandler>` registry
  populated at startup. One handler per supported op. Approach A from
  brainstorming.
- All mutations operate on an in-memory `ProjectState`. **One**
  `load_project` at the start, **one** `save_project` at the end. The
  existing `.bak`-swap atomic save and 3-check invariant guard run once
  against the cumulative state.
- **All-or-nothing semantics**: any failure (manifest parse, schema
  validation, op execution) aborts before `save_project` is called.
  The input file is never partially mutated. No `--continue-on-error`
  flag; rejected as footgun-prone.
- The exception → exit-code mapping currently inlined in
  `mutation_runner.hpp` is **lifted into a shared helper**
  (`exception_dispatch.hpp`) and reused by both the single-op envelope
  and the batch dispatcher. Behaviour-preserving refactor.
- STL paths inside the manifest are resolved relative to the
  **manifest file's directory** (not the CWD), so a manifest + STLs
  folder is a portable self-contained bundle. Absolute paths still
  allowed.

## Scope

In scope:
- `project apply in.3mf --manifest m.json [--output out.3mf]`
- Manifest schema (envelope, op shape, type rules, strictness)
- Full parity with every existing mutating verb:
  - `plate.add` / `plate.remove` / `plate.rename`
  - `plate.center` / `plate.drop-to-bed` / `plate.arrange` / `plate.auto-orient`
  - `object.add` / `object.remove` / `object.set-filament`
  - `object.auto-orient` / `object.split-to-parts` / `object.merge-parts`
  - `config.set` / `config.unset` (with batch shorthand)
- `exception_dispatch.hpp` refactor (lift the dynamic_cast table out
  of `mutation_runner.hpp`)
- `emit_error` signature extension (optional `data` blob)
- Unit, manifest-validation, e2e, and roundtrip test coverage

Out of scope (explicit):
- **From-scratch project creation** from the manifest. Apply-to-existing
  only. Use `project init` to create the base file first.
- **Declarative target-state schema** (`plates: [{name, objects: [...]}]`).
  Rejected during brainstorming: semantics for split/merge/arrange under
  a declarative model are murky; the imperative shape maps cleanly to
  existing `project_ops` functions.
- **`--continue-on-error`** / best-effort mode. Half-applied batches
  produce silent partial-correctness bugs; the 12-plate workflow has
  no use case for it.
- **`--dry-run`** mode (validate manifest + check ops succeed without
  saving). Implementing without a deep-copy of `ProjectState` requires
  per-op validate-isolated branches. Defer to v1.1 if requested.
- **`--check-stls`** pre-flight existence check. The natural error path
  (`load_stl` → `parse_failure` exit 3) already surfaces missing STLs
  correctly; the pre-flight would only improve UX latency. Defer.
- **Templating / variables / includes** in the manifest. Strict JSON
  only. Users wanting substitution pipe through `jq` / `envsubst` /
  their language's JSON lib.
- **Multiple input `.3mf` files** (e.g. merging projects). Single input,
  single output, same as every existing verb.
- **Manifest comments / JSONC / YAML**. Strict JSON only; parser is
  unambiguous and the schema is small enough not to need them.

## CLI shape

```
bambu-cli project apply <in.3mf> --manifest <m.json> [--output <out.3mf>] [--json]
```

Arguments:
- positional `in.3mf` — input project, required.
- `--manifest <path>` — path to the JSON manifest, required.
- `--output <path>` — output path; defaults to in-place (same as every
  mutating verb).
- `--json` — top-level flag already plumbed via `mode_out`; switches
  success/error envelopes to Shape A.

Wiring: a new TU `src/cli/commands/project_apply.cpp` exports
`register_project_apply_subcommand(CLI::App* project_cmd, OutputMode*
mode_out)`, called from `register_project_subcommands` in
`commands/project.cpp`. (One additional `add_subcommand("apply", ...)`
inside the existing `project` group.)

Why a subcommand of `project`, not a top-level verb: the existing
convention is `<noun> <verb>`. `apply` reads naturally as a verb
against a project. Putting it at top level would be the only verb
without a noun prefix.

## Manifest schema

### Top-level envelope

```jsonc
{
  "version": 1,
  "operations": [ ... ]
}
```

- `version` (integer, required) — must be `1`. Any other value →
  `exit 1` (`usage_error`) with `"unsupported manifest version: N"`.
  Forward-compat hook: future schemas bump this.
- `operations` (array, required) — ordered list of op objects. May be
  empty (no-op save, exit 0).
- Any **unknown top-level key** → `exit 1` with
  `"unknown top-level key: '<name>'"`. Strict; catches typos like
  `"operation"` singular.

### Op object shape (uniform across verbs)

```jsonc
{ "op": "<namespace>.<verb>", ...args }
```

- `op` (string, required) — dot-namespaced. Mirrors the CLI subcommand
  path exactly: `"plate.add"`, `"object.split-to-parts"`,
  `"config.unset"`. Hyphens preserved verbatim — no underscore aliasing.
- Remaining fields are the op's arguments. Field names match the CLI
  flag names with `--` stripped: `--name` → `"name"`, `--stl` → `"stl"`,
  etc.
- **Unknown fields on an op** → `exit 1` with
  `"step N (op '<op>'): unknown field: '<name>'"`. Strict; catches
  typos like `"filement": 2`.
- **Missing required fields** → `exit 1` with
  `"step N (op '<op>'): missing required field '<name>'"`.

### Type rules

- **Strings**: JSON string. Empty string is never a valid identifier
  (plate name, object name, key name) — rejected at the `project_ops`
  layer with `exit 1`.
- **Integers** (`filament`, `count`): JSON integer. Floats or strings →
  `exit 1`.
- **Booleans**: JSON `true`/`false`. (None required by any v1 op,
  reserved for future use.)
- **Transforms**: nested object — see [Transform shape](#transform-shape).
- **Config batch maps**: nested object — see
  [Config batch shorthand](#config-batch-shorthand).

### Manifest size cap

Hard cap of **10,000 operations** per manifest. `constexpr` in the
dispatcher. Above this → `exit 1` with
`"manifest exceeds maximum of 10000 operations (got N)"`.

Rationale: above ~10k ops the manifest is almost certainly generated
and the user should split into batches for memory and debuggability.
Safety net only — easy to bump or remove in code if it becomes
restrictive.

### Strict-mode rationale

The unknown-key / unknown-field rejections look harsh but they protect
users who hand-write manifests. Silently ignoring `"filement": 2`
masks a real bug (the print succeeds on extruder 1, not 2). The strict
schema makes "did you spell it right?" a parse error, not a print error.

## Per-op field reference

All 14 mutating verbs. Field names match CLI flag names (sans `--`)
except where JSON ergonomics differ (called out in **bold**).

### Plate ops

| op                  | required          | optional | error codes                       |
|---------------------|-------------------|----------|-----------------------------------|
| `plate.add`         | `name` (string)   | —        | 5 (duplicate)                     |
| `plate.remove`      | `name` (string)   | —        | 6 (not found or non-empty)        |
| `plate.rename`      | `from`, `to` (str)| —        | 1 (empty `to`) / 5 (dup) / 6 (NF) |
| `plate.center`      | `plate` (string)  | —        | 6 (plate not found)               |
| `plate.drop-to-bed` | `plate` (string)  | —        | 6                                 |
| `plate.arrange`     | `plate` (string)  | —        | 1 / 6 / 9 (placement)             |
| `plate.auto-orient` | `plate` (string)  | —        | 6 / 7 (orient engine)             |

### Object ops

| op                       | required                              | optional                                                  | semantics      | error codes        |
|--------------------------|---------------------------------------|-----------------------------------------------------------|----------------|--------------------|
| `object.add`             | `plate` (str), `stl` (str)            | `name` (str), `filament` (int 1..N), `count` (int≥1), **`translate`**, **`rotate`**, **`scale`** (object form) | per-call       | 1 / 9              |
| `object.remove`          | `name` (string)                       | —                                                         | group-by-name  | 6                  |
| `object.set-filament`    | `name` (str), `filament` (int)        | `part` (volume name; omit = object-level)                 | group-by-name  | 1 / 6              |
| `object.auto-orient`     | `name` (string)                       | —                                                         | group-by-name  | 6 / 7              |
| `object.split-to-parts`  | `name` (string)                       | —                                                         | **first-match**| 6 / 7              |
| `object.merge-parts`     | `name` (str), **`parts`** (str array), `into` (str) | `filament` (int)                            | **first-match**| 5 / 6 / 7          |

### Config ops (with batch shorthand)

| op             | required (one of)                                                                          | optional                              | error codes |
|----------------|--------------------------------------------------------------------------------------------|---------------------------------------|-------------|
| `config.set`   | (`key` (str) + `value` (str)) **OR** **`values`** (object: string → string)                | `object` (str; omit = project-level)  | 4 / 6       |
| `config.unset` | `key` (str) **OR** **`keys`** (str array)                                                  | `object` (str; omit = project-level)  | 4 / 6       |

For `config.set` / `config.unset`: **exactly one of** the two forms must
be present. Both forms → `exit 1`. Neither → `exit 1`.

### Semantic footguns (called out per op)

- **Group-by-name** (`object.remove`, `object.set-filament`,
  `object.auto-orient`): operates on *all* `ModelObject`s sharing the
  name. If `object.add` was run with `count: 5`, a later
  `object.remove` on that name removes all 5.
- **First-match** (`object.split-to-parts`, `object.merge-parts`):
  operates on the *first* `ModelObject` with the name; other clones
  untouched. Per the existing rationale in `project_ops.hpp:170-176`
  and `:259-266` — splitting/merging across a clone group is ambiguous.

Both semantics are inherited unchanged from the underlying
`project_ops` functions. The dispatcher calls into them directly.

## Transform shape

For `object.add`. All three sections optional. Presence of any axis in
a section flips the corresponding `ManualTransform::has_translate` /
`has_rotate` / `has_scale` flag.

```jsonc
"translate": { "x": 10, "y": 20 }           // z defaults to 0
"rotate":    { "x": 0, "y": 0, "z": 90 }    // degrees; axes default to 0
"scale":     { "x": 1.5, "y": 1, "z": 1 }   // per-axis; axes default to 1
"scale":     1.5                            // shorthand: uniform on all axes
```

Rules:
- Object form: `x`, `y`, `z` all optional; missing axes use the default
  for that section (0 for translate/rotate, 1 for scale).
- Uniform-scale shorthand: a bare number for `"scale"` is equivalent to
  `{"x": n, "y": n, "z": n}`.
- Unknown axis keys on a transform object → `exit 1` ("unknown axis
  key '<name>' on transform").
- A section present but empty (`"translate": {}`) is treated as not
  present (the corresponding `has_*` flag stays false). No-op.

This is a different surface from the CLI's `--translate "x,y[,z]"`
string but the same `ManualTransform` struct underneath. The CLI keeps
its string form unchanged; the JSON has its own form. Both call into
the same `add_object_to_plate`.

## Config batch shorthand

Motivating case: setting 6 config keys against the project means 6
individual `config.set` ops in single-key form. The `values` form
collapses them into one op.

```jsonc
{
  "op": "config.set",
  "values": {
    "layer_height":       "0.2",
    "first_layer_height": "0.16",
    "sparse_infill_density": "20%"
  }
}
```

Semantics:
- Iterates entries in insertion order (preserved by `nlohmann::json`).
- Each entry is a separate call to `config_set(state, object, key,
  value)` under the hood.
- If any entry fails (bad key, bad value), the *step* fails. The
  earlier successful entries in this step have already mutated
  `state` in memory but `save_project` will not be called (all-or-
  nothing).
- The same `object` field applies to all entries in the map.

`config.unset` mirrors the shape:
```jsonc
{ "op": "config.unset", "keys": ["a", "b", "c"], "object": "MyPart" }
```

Validation:
- `config.set`: `key`+`value` and `values` are mutually exclusive.
  Neither present → `exit 1`. Both present → `exit 1`.
- `config.unset`: `key` and `keys` are mutually exclusive. Neither →
  `exit 1`. Both → `exit 1`.

## Path resolution

For every `stl` field in `object.add` ops:

```
if path is absolute → use as-is (boost::filesystem::path::is_absolute)
else                → resolve as (manifest_dir / path)
```

where `manifest_dir = boost::filesystem::path(manifest_arg).parent_path()`.
A bare-filename `--manifest m.json` yields an empty `manifest_dir`;
`"" / "models/foo.stl"` resolves to `"models/foo.stl"` relative to CWD
via `weakly_canonical` — the natural outcome when the user runs the
command from the manifest's own directory.

After resolution, the path is normalised via
`boost::filesystem::weakly_canonical` so `..` / `.` segments collapse
and symlinks resolve. Error messages then point at the real file.

Path existence is **not** pre-flighted in v1. `add_object_to_plate`
already throws `std::runtime_error` ("STL parse failure") via the
underlying `load_stl`; the existing exception table routes that to
`exit 3` (`parse_failure`). With the step/op envelope additions (see
[Error envelope](#error-envelope)), the message becomes
`"step N (op 'object.add'): STL parse failure: <path>"`.

## Failure & atomicity semantics

### Dispatch flow

```
1. validate --manifest flag, open & read file              [pre-load]
2. parse manifest JSON, validate header (version, ops)     [pre-load]
3. enforce 10k cap                                         [pre-load]
4. load_project(in.3mf) → ProjectState                     [one disk read]
5. for i, step in enumerate(operations, 1):
       handler = registry.lookup(step["op"])
       handler(state, step)                                ← may throw
6. save_project(state, out.3mf)                            [one atomic save]
```

Stages 1–3 are pre-load: if the manifest is malformed, the `.3mf` is
not even read. Stage 5 throws on any failure; the catch block does
**not** call `save_project`. Stage 6 runs only after all ops succeed.

### Two-stage validation

| Stage | What's checked | Rationale |
|-------|----------------|-----------|
| **1. Static (pre-load)** | JSON parses; `version == 1`; `operations` is array; no unknown top-level keys; size ≤ 10000; every step has a string `op` | Cheap, no disk I/O against the `.3mf` |
| **2. Per-op (during dispatch)** | Required fields present, types correct, unknown fields rejected (`require_only`), then underlying `project_ops` semantics | Field shape is op-specific; co-located with each handler |

Per-op field validation throws `std::invalid_argument` → routed by the
existing exception table to `exit 1` (`usage_error`). The step/op
prefix is added by the dispatcher's catch block.

### What atomicity guarantees

- **Manifest invalid** (parse error, schema error, unknown op,
  unknown field, missing required field, wrong type): `exit 1` or
  `exit 3`, no `load_project`, input file untouched.
- **Manifest valid but op N throws** (any reason — `duplicate_name`,
  `unknown_reference`, `bad_config`, `placement_failure`, etc.): the
  matching `exit_code` is returned, `save_project` is **never called**,
  input file untouched on disk.
- **All ops succeed, save_project fails** (invariant violation, disk
  full, permission denied): standard `IoResult` error path. The
  `.bak`-swap preserves the original file even if the write was
  interrupted mid-flight.

What atomicity does **not** guarantee:
- File-level atomicity, not op-level. Once `save_project` starts, the
  output reflects all N ops cumulatively. There is no on-disk
  transaction log.
- Cross-invocation atomicity. Two concurrent `project apply` against
  the same input is undefined (the `.bak`-swap serialises via the
  filesystem rename lock on Windows; behaviour on other platforms is
  out of scope).

## Error envelope

Per-step errors carry two extra fields beyond the standard Shape A
envelope:

- `step` (1-based integer) — index into `operations`.
- `op` (string) — the op name as written in JSON.

### JSON mode

```jsonc
{
  "status": "error",
  "code": "duplicate_name",
  "message": "step 3 (op 'plate.add'): plate 'P1' already exists",
  "step": 3,
  "op": "plate.add"
}
```

The message already contains the step/op prefix; the structured fields
are duplicated for tooling that wants to filter without string parsing.

### Text mode

```
duplicate_name: step 3 (op 'plate.add'): plate 'P1' already exists
```

Single line to stderr, matching the existing text-mode error format.

### Implementation note

This requires `emit_error` to accept an optional `data` blob (default
`nullptr`, omitted from the JSON envelope when null). That matches
`emit_ok`'s existing signature (`json_output.hpp:19-20`) — small,
backwards-compatible signature extension. All existing callers compile
unchanged.

### Pre-load errors (no step/op)

If the manifest itself fails validation (parse error, missing version,
unknown top-level key, 10k cap exceeded), there is no step yet. These
errors emit the standard envelope without `step` / `op`. The message
distinguishes them with a `manifest: ` prefix:

```
usage_error: manifest: unknown top-level key 'operation' (did you mean 'operations'?)
```

## Implementation skeleton

### File map

| File | Status | Purpose | Approx LOC |
|------|--------|---------|------------|
| `src/cli/commands/project_apply.cpp`        | new          | `register_project_apply_subcommand`, manifest parsing, handler registry, dispatch loop | ~300 |
| `src/cli/apply_helpers.hpp` + `.cpp`        | new          | `require_only`, `parse_transform`, `parse_filament`, plus small typed-getters | ~80 |
| `src/cli/exception_dispatch.hpp` + `.cpp`   | new (refactor) | Lifts the `dynamic_cast` exception → (exit, code) table out of `mutation_runner.hpp` | ~60 |
| `src/cli/json_output.hpp` / `.cpp`          | extend       | `emit_error` gains optional `data` blob | ~5 |
| `src/cli/commands/mutation_runner.hpp`      | tweak        | Catch block delegates to `exception_dispatch.hpp` | ~−30 |
| `src/cli/commands/project.cpp`              | tweak        | Call `register_project_apply_subcommand` | ~3 |
| `src/cli/CMakeLists.txt`                    | tweak        | Add new TUs to `bambu_cli_core` | ~4 |

### Handler signature

```cpp
using OpHandler = std::function<void(ProjectState&, const nlohmann::json& step)>;
```

The full step object (including its `"op"` key) is passed; handlers
ignore the `op` key via `require_only`. Returning `void` keeps each
handler tight. Per-step success messages are not emitted; the final
success message reports cumulative count + output path.

### Registry

```cpp
class HandlerRegistry {
public:
    HandlerRegistry();
    void dispatch(ProjectState&, const std::string& op,
                  const nlohmann::json& step) const;
private:
    std::unordered_map<std::string, OpHandler> m_handlers;
};
```

Populated once in the constructor (one entry per op). The dispatcher
holds a function-local `static const HandlerRegistry`. Unknown op →
`std::invalid_argument("unknown op: '" + op + "'")` → routed to
`exit 1` by `exception_dispatch`.

### Sample handler (plate.add)

```cpp
m_handlers["plate.add"] = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "name"});
    if (!step.contains("name") || !step["name"].is_string())
        throw std::invalid_argument("plate.add: missing or non-string 'name'");
    add_plate(s, step["name"].get<std::string>());
};
```

Every handler follows the shape:
1. `require_only(step, {known keys})`
2. Type-check required fields (`is_string`, `is_number_integer`, etc.),
   throwing `std::invalid_argument` on miss.
3. Read optional fields via `step.value("key", default)` or `contains` +
   typed `get`.
4. Call into the matching `project_ops` function.

### `require_only`

```cpp
// apply_helpers.hpp
void require_only(const nlohmann::json& step,
                  std::initializer_list<const char*> known_keys);
```

Iterates `step.items()`; throws `std::invalid_argument` for any key
not in `known_keys`. Single point of truth for the strict-schema rule
(Section 5).

### `parse_transform`

```cpp
// apply_helpers.hpp
ManualTransform parse_transform(const nlohmann::json& step);
```

Reads `translate`, `rotate`, `scale` sections; returns a populated
`ManualTransform` (with the right `has_*` flags). Handles object form,
the uniform-scale numeric shorthand, and missing-axis defaults
(0 for translate/rotate, 1 for scale). Throws `std::invalid_argument`
on:
- A section that is neither object nor (for `scale` only) number.
- An unknown axis key on a transform object.

### Dispatcher loop (pseudocode)

```
parse + validate manifest header                      ; Stage 1
load_project(in_path) → state                         ; one read
for i, step in enumerate(manifest["operations"], 1):
    op = step["op"]                                   ; already validated to be a string
    try:
        registry.dispatch(state, op, step)
    except std::exception as e:
        d = exception_dispatch::dispatch(e)
        data = { "step": i, "op": op }
        emit_error(mode, d.code,
                   "step " + i + " (op '" + op + "'): " + d.message,
                   data)
        std::exit(d.exit_code)
out = output_path.empty() ? in_path : output_path
save_project(state, out)                              ; one write
emit_ok(mode, "ok",
        "applied " + N + " ops -> " + out,
        { "steps_applied": N, "output": out })
```

### `exception_dispatch` refactor

The `dynamic_cast` chain currently in `mutation_runner.hpp:87-123`
moves to a free function:

```cpp
// exception_dispatch.hpp
namespace bambu_cli::exception_dispatch {

struct Dispatched {
    int         exit_code;
    std::string code;          // "duplicate_name", "placement_failure", …
    std::string message;       // e.what(), unchanged
};

Dispatched dispatch(const std::exception& e,
                    const MutationExceptionMap& overrides = {});

} // namespace
```

`mutation_runner.hpp`'s catch block shrinks to:
```cpp
} catch (const std::exception& e) {
    auto d = exception_dispatch::dispatch(e, overrides);
    emit_error(mode, d.code, d.message);
    std::exit(d.exit_code);
}
```

`project_apply.cpp` reuses the same `dispatch` call but wraps the
message with the step/op prefix and adds the data blob.

Behaviour-preserving: the dispatch table is byte-identical to the
existing inline ladder. The full 331-case test suite must remain
green after the refactor.

### Build & link

- No new third-party deps. `nlohmann::json` is already linked via
  `json_output.cpp`.
- No new `libslic3r_gui` link surface.
- CMake changes: two `target_sources(bambu_cli_core PRIVATE ...)`
  entries for the new TUs.

## Testing strategy

Coverage spans the three existing tiers (`tests/cli/{unit,e2e,
roundtrip}`).

### Unit tests — handler-direct

`tests/cli/unit/test_project_apply_handlers.cpp`

Drive individual handlers via the registry, no JSON file involved.
Each test constructs a `ProjectState` (from the reference fixture or
programmatically), looks up the handler, calls it with a synthetic
`json` blob, and asserts on `state`.

- **Per-op happy path** (14 tests, one per op)
- **Per-op required-field rejection** (~28 tests; for each required
  field on each op, omit it, assert `std::invalid_argument`)
- **Per-op unknown-field rejection** (14 tests; junk field per op)
- **Per-op type-mismatch rejection** (~20 tests; strings for ints, etc.)
- **Transform parsing** (8 tests: object form, missing axes, uniform
  scale shorthand, all three sections, none, unknown axis key,
  number-shaped `scale`, empty section)
- **Config batch form** (6 tests: `values` with 3 keys, `keys` array
  with 2, both forms present → reject, neither → reject, single +
  batch mixed → reject, empty `values` map)

Total: ~90 unit tests.

### Manifest validation tests

`tests/cli/unit/test_apply_manifest.cpp`

Drive the manifest parser/validator with strings. No `ProjectState`,
no `load_project`.

- Top-level shape (~8): not JSON, not object, version missing,
  version not integer, version != 1, operations missing, operations
  not array, unknown top-level key
- Per-step shape (3): missing `op`, non-string `op`, unknown `op`
- Size cap (2): exactly 10000 accepted, 10001 rejected

Total: ~13 tests.

### E2E tests

`tests/cli/e2e/test_project_apply.cpp`

Drive the actual `bambu-cli project apply` exe with a real manifest
file and a reference fixture. Catch2 forks the binary; tests capture
stdout/stderr and exit code.

- **Happy path — 12-plate workflow**: manifest that adds 12 plates,
  populates each with one cloned object, sets 6 config keys, runs
  `plate.arrange` on each. Single output `.3mf`; reload and verify
  plate count, object count per plate, config key values.
- **Idempotence**: apply manifest twice (second against first's
  output); second fails at `plate.add` with `duplicate_name` and
  step index 1; output untouched.
- **Path resolution**: manifest in `/tmp/manifest_dir/m.json`, STL
  at `/tmp/manifest_dir/models/foo.stl`, manifest references
  `models/foo.stl`; run from unrelated CWD; assert success.
- **JSON-mode error envelope**: manifest with guaranteed failure at
  step 3; assert stderr JSON has `step: 3, op: <op>` fields plus
  `code` / `message`.
- **All-or-nothing**: 5-op manifest with bad step 4; assert exit
  code from the failure, assert input file mtime/checksum unchanged,
  assert `.bak` not left behind.
- **Empty operations list**: `{"version":1,"operations":[]}` →
  exit 0; output written; contents equal input modulo timestamps.
- **Text-mode error envelope**: same as JSON-mode test but assert
  the single-line stderr format.
- **Manifest not found**: `--manifest /no/such.json` → exit 2,
  `file_not_found`, no `.3mf` touched.
- **Malformed JSON manifest**: invalid JSON → exit 3, `parse_failure`
  with nlohmann line/column.
- **10k cap**: programmatically generated manifest with 10001 ops →
  exit 1, cap message.

Total: ~12 E2E tests.

### Roundtrip tests

`tests/cli/roundtrip/test_apply_roundtrip.cpp`

- **Single save invariant**: instrument `save_project` calls; apply
  a 20-op manifest; assert exactly 1 save.
- **Equivalence**: a manifest of N ops equivalent to N sequential CLI
  verbs produces a `.3mf` whose reloaded `ProjectState` deep-equals
  the sequential result. Asserts plate names + counts, object names
  + extruders per plate, config diffs. Core correctness guarantee:
  batch does not silently change semantics.
- **Empty manifest roundtrip**: load → apply empty → save → reload;
  compare with load → save → reload (skipping apply). Asserts no
  spurious metadata diff from the batch envelope itself.

Total: 3 roundtrip tests.

### Coverage targets

- Every op: ≥1 happy-path unit, ≥1 required-field rejection,
  ≥1 unknown-field rejection.
- Every documented error code (1 / 4 / 5 / 6 / 7 / 9) triggered
  through the batch path by ≥1 E2E.
- The `step` and `op` envelope fields tested in both text and
  JSON modes.

### Estimated total

~118 new test cases. No new fixtures beyond:
- The existing `tests/cli/fixtures/test_reference.3mf`.
- A small STL bundle in `tests/cli/fixtures/manifests/stls/`.
- A handful of `.json` manifests under
  `tests/cli/fixtures/manifests/`.

## Sibling-fork note

OrcaSlicer's parallel `orca-cli` initiative (the read-only reference
at `C:\Users\ildarcheg\Documents\GitHub\OrcaSlicer`) does not yet
have a batch-manifest verb. This work is **Bambu-leading** for now.
The schema and verb name (`project apply`) should be designed so
Orca can adopt it verbatim later: avoid Bambu-specific terminology
in user-visible field names; keep semantics identical to the
underlying `project_ops` functions so a 1:1 port is mechanical.

The exception_dispatch refactor is independently useful for Orca's
own `mutation_runner` and can be ported back without batch support.

## Open questions / deferred to v1.1

- **`--check-stls` pre-flight**: iterate manifest, check every STL
  exists before `load_project`. Improves error latency only.
- **`--dry-run`**: validate manifest + run ops without saving.
  Requires deep-copy of `ProjectState` or per-op validate-isolated
  branches.
- **Stream-mode for very large manifests**: replace nlohmann's
  in-memory parse with SAX-style streaming. Only relevant if the
  10k cap is removed and users genuinely build >100k-op manifests.
- **Per-op success messages in `--verbose`**: today `--verbose` is
  parsed but a no-op (`main.cpp:38-42`). When `--verbose` becomes
  real, the dispatcher should emit a per-step success line.
- **Slice integration**: a hypothetical `slice.plate` op that runs
  the slice for a named plate. Out of scope today (the CLI does not
  slice at all). Would land alongside any future slice support.
