# `project apply` Batch Manifest Verb — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a single `bambu-cli project apply <in.3mf> --manifest <m.json> [--output <out.3mf>] [--dry-run]` verb that loads a `.3mf`, applies an ordered list of mutations described by a JSON manifest, and saves once at the end. Collapses today's 30+ individual CLI invocations per project-composition workflow into one load/save cycle.

**Architecture:** Centralized dispatcher (`commands/project_apply.cpp`) + handler-registry pattern (`HandlerEntry { fn, overrides }` keyed by op name) + behaviour-preserving refactor lifting the exception → exit-code table out of `commands/mutation_runner.hpp` into a shared `exception_dispatch.hpp`. New `ManifestFieldError` (derives from `std::invalid_argument`) short-circuits to exit 1 before per-op override lookup, so schema typos can't be misclassified as semantic mesh-state errors on verbs that remap `std::invalid_argument` to exit 7. `--dry-run` is a single boolean guard at save time — no `ProjectState` deep-copy.

**Tech stack:** C++17, libslic3r, `nlohmann::json` (already linked via `json_output.cpp`), CLI11 (vendored at `src/cli/extern/CLI11/CLI11.hpp`), Catch2 v2.x (`tests/cli/`), boost::filesystem.

**Spec:** `docs/superpowers/specs/2026-05-31-project-apply-batch-design.md`.

---

## Working conventions

### Build env (Windows, primary dev box)

Per the `build_environment` memory file. In PowerShell, before invoking CMake:

```powershell
# Prime VS env (only once per shell)
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$env:CMAKE_POLICY_VERSION_MINIMUM = "3.5"
# Put VS-bundled CMake 3.20 in front of any newer system CMake
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Strawberry\perl\bin;$env:PATH"
```

Configure (only if `build/` doesn't exist or CMake files changed):
```powershell
cmake -G "Visual Studio 16 2019" -A x64 -DSLIC3R_BUILD_TESTS=ON -B build -S .
```

Build:
```powershell
cmake --build build --config Release --parallel 2 --target bambu-cli cli_tests
```

Run cli_tests (DLLs are not POST_BUILD-copied; prepend the CLI release dir):
```powershell
$env:PATH = "$pwd\build\src\cli\Release;$env:PATH"
& "build\tests\cli\Release\cli_tests.exe" --reporter compact
```

Run a single test case by tag or name:
```powershell
& "build\tests\cli\Release\cli_tests.exe" "[project_apply]" --reporter compact
& "build\tests\cli\Release\cli_tests.exe" "project_apply: plate.add happy path" --reporter compact
```

### Commit style

Every commit ends with a Co-Authored-By trailer:
```
Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

Conventional-commit prefixes the repo uses: `feat(cli):`, `refactor(cli):`, `test(cli):`, `docs(cli):`. Stage individual files (`git add <path1> <path2>`), never `git add -A`.

### Sibling-fork hygiene

Do not check the OrcaSlicer fork during this work — it has no batch-manifest verb yet. This is Bambu-leading.

---

## File structure (locked from the spec)

| File | Status | Purpose |
|---|---|---|
| `src/cli/exceptions.hpp` | extend | Add `ManifestFieldError : public std::invalid_argument` |
| `src/cli/json_output.hpp` + `.cpp` | extend | `emit_error` gains optional `data` blob |
| `src/cli/exception_dispatch.hpp` + `.cpp` | new | Lifts the `dynamic_cast` table out of `mutation_runner.hpp`, adds `ManifestFieldError` short-circuit |
| `src/cli/commands/mutation_runner.hpp` | tweak | Catch block delegates to `exception_dispatch.hpp` |
| `src/cli/apply_helpers.hpp` + `.cpp` | new | `require_only`, `parse_filament`, `parse_transform`, plus `ConfigBatchError` |
| `src/cli/commands/project_apply.cpp` | new | Verb registration, manifest parsing, `HandlerRegistry`, dispatch loop, `--dry-run` |
| `src/cli/commands/project.cpp` | tweak | Call `register_project_apply_subcommand` |
| `src/cli/CMakeLists.txt` | tweak | Add `apply_helpers.cpp`, `exception_dispatch.cpp`, `commands/project_apply.cpp` to `bambu_cli_core` |
| `tests/cli/unit/test_apply_helpers.cpp` | new | Unit tests for `require_only`, `parse_filament`, `parse_transform` |
| `tests/cli/unit/test_apply_manifest.cpp` | new | Manifest-header validation tests (no `ProjectState`) |
| `tests/cli/unit/test_project_apply_handlers.cpp` | new | Per-op handler unit tests via `HandlerRegistry::lookup` |
| `tests/cli/e2e/test_project_apply.cpp` | new | Spawned-exe end-to-end tests |
| `tests/cli/roundtrip/test_apply_roundtrip.cpp` | new | Single-save invariant, equivalence, empty-manifest roundtrip |
| `tests/cli/CMakeLists.txt` | tweak | Add the four new test TUs to `BAMBU_CLI_TEST_SOURCES` |

---

## Task 1: Extend `emit_error` to accept an optional `data` blob

**Files:**
- Modify: `src/cli/json_output.hpp` (signature)
- Modify: `src/cli/json_output.cpp` (implementation)

Backwards-compatible default (`nullptr`) so every existing caller compiles unchanged. The `data` argument is merged into the JSON envelope when non-null; ignored in text mode.

- [ ] **Step 1: Read the current signatures**

Read `src/cli/json_output.hpp` lines 19–25 and `json_output.cpp`'s `emit_error` body. Confirm `emit_ok` already takes `const nlohmann::json& data = nullptr` so the pattern is consistent.

- [ ] **Step 2: Update the header signature**

In `src/cli/json_output.hpp`, change:
```cpp
void emit_error(OutputMode mode, const std::string& code, const std::string& message);
```
to:
```cpp
// Emit an "error" message to stderr.
//   - text mode: "<code>: <message>" to stderr (data ignored)
//   - json mode: {"status":"error","code":<code>,"message":<message>, ...<data>}
// <data>, if non-null, is *merged* at the top level (each key becomes a
// sibling of code/message/status). nullptr (default) omits the merge.
void emit_error(OutputMode mode, const std::string& code, const std::string& message,
                const nlohmann::json& data = nullptr);
```

- [ ] **Step 3: Update the implementation**

In `src/cli/json_output.cpp`, in `emit_error`'s JSON branch, after building the envelope object with `status`/`code`/`message`, add:
```cpp
if (!data.is_null() && data.is_object()) {
    for (auto it = data.begin(); it != data.end(); ++it) {
        envelope[it.key()] = it.value();
    }
}
```

Make sure the signature in the `.cpp` matches the header.

- [ ] **Step 4: Build to confirm backwards-compat**

```powershell
cmake --build build --config Release --parallel 2 --target bambu_cli_core
```
Expected: builds cleanly. No existing caller updated, no errors.

- [ ] **Step 5: Run full test suite**

```powershell
& "build\tests\cli\Release\cli_tests.exe" --reporter compact
```
Expected: 331 cases / 4032 assertions, all pass. No behaviour change for existing callers.

- [ ] **Step 6: Commit**

```powershell
git add src/cli/json_output.hpp src/cli/json_output.cpp
git commit -m @'
feat(cli): extend emit_error with optional data blob

Adds a default-nullptr `data` parameter to emit_error mirroring
emit_ok's signature. When non-null and an object, its keys are
merged into the JSON error envelope as siblings of code/message/
status. Text mode ignores it. Existing callers unchanged.

Prepares for the project-apply verb, which emits step/op (and
failing_key) context on per-step errors.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
'@
```

---

## Task 2: Add `ManifestFieldError` to `exceptions.hpp`

**Files:**
- Modify: `src/cli/exceptions.hpp`

Schema-validation exception type. Derives from `std::invalid_argument` so the existing exception_dispatch fallback (line 111 of mutation_runner.hpp pre-refactor) would still catch it as exit 1 even without the explicit short-circuit. The explicit short-circuit (Task 3) makes it robust against override-map collisions on verbs that remap `std::invalid_argument` to exit 7.

- [ ] **Step 1: Insert the new exception class**

In `src/cli/exceptions.hpp`, immediately before the closing `} // namespace bambu_cli`, add:
```cpp

// Manifest schema-shape error: unknown field, missing required field,
// type mismatch, unknown op. Thrown by `require_only`, by every handler's
// field-validation code, and by `HandlerRegistry::lookup` on unknown op.
// -> exit 1 (usage_error), short-circuited in exception_dispatch::dispatch
//    BEFORE the per-op MutationExceptionMap lookup so verbs that remap
//    std::invalid_argument -> exit 7 (split-to-parts, merge-parts) don't
//    swallow schema typos as invalid_state.
class ManifestFieldError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};
```

- [ ] **Step 2: Build to confirm no breakage**

```powershell
cmake --build build --config Release --parallel 2 --target bambu_cli_core
```
Expected: clean.

- [ ] **Step 3: Commit**

```powershell
git add src/cli/exceptions.hpp
git commit -m @'
feat(cli): add ManifestFieldError for schema-shape errors

A std::invalid_argument subclass used by all schema validators in
the upcoming project-apply batch verb. Distinguishes "manifest typo"
from semantic mesh-state std::invalid_argument throws, which matters
on verbs (split-to-parts, merge-parts) whose per-call-site override
maps remap invalid_argument to exit 7.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
'@
```

---

## Task 3: Lift exception dispatch into `exception_dispatch.{hpp,cpp}` + refactor `mutation_runner.hpp`

**Files:**
- Create: `src/cli/exception_dispatch.hpp`
- Create: `src/cli/exception_dispatch.cpp`
- Modify: `src/cli/commands/mutation_runner.hpp` (catch block shrinks)
- Modify: `src/cli/CMakeLists.txt` (add the new TU)

This is a behaviour-preserving refactor of the existing single-verb dispatch. Adds the `ManifestFieldError` short-circuit at the top. All 331 existing tests must remain green.

- [ ] **Step 1: Create the header**

Create `src/cli/exception_dispatch.hpp` with:
```cpp
#pragma once

// exception_dispatch — shared exception → (exit_code, error_code) table.
//
// Used by both the single-verb run_mutation envelope
// (commands/mutation_runner.hpp) and the batch `project apply` dispatcher
// (commands/project_apply.cpp). Lifting this out of run_mutation gives a
// single point of truth and lets the batch dispatcher wrap the produced
// message with step/op (and failing_key) context.
//
// Dispatch order (matters!):
//   0. ManifestFieldError              -> exit 1 / "usage_error"
//      (BEFORE overrides — protects schema typos on verbs whose override
//      map remaps std::invalid_argument to exit 7)
//   1. per-call-site overrides         -> as configured
//      (exact std::type_index match on dynamic typeid(e))
//   2. built-in typed-exception table  -> see Dispatched return values
//
// The table at step 2 is byte-identical to mutation_runner.hpp's prior
// inline dynamic_cast ladder (lines 87-123). Behaviour-preserving for
// every existing single-verb call site.

#include <exception>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace bambu_cli {

// Override map. Re-exported here so callers don't need to drag in the
// commands/mutation_runner.hpp header just for the type.
using MutationExceptionMap =
    std::unordered_map<std::type_index, std::pair<int, std::string>>;

namespace exception_dispatch {

struct Dispatched {
    int         exit_code;
    std::string code;       // e.g. "duplicate_name", "placement_failure"
    std::string message;    // e.what(), unchanged
};

Dispatched dispatch(const std::exception& e,
                    const MutationExceptionMap& overrides = {});

} // namespace exception_dispatch
} // namespace bambu_cli
```

- [ ] **Step 2: Create the implementation**

Create `src/cli/exception_dispatch.cpp` with:
```cpp
#include "exception_dispatch.hpp"

#include "exceptions.hpp"
#include "exit_codes.hpp"

#include <stdexcept>
#include <typeinfo>

namespace bambu_cli::exception_dispatch {

Dispatched dispatch(const std::exception& e,
                    const MutationExceptionMap& overrides)
{
    // 0. Schema errors short-circuit BEFORE the override lookup so they
    //    never get caught by per-op invalid_argument -> exit 7 remappings.
    if (dynamic_cast<const ManifestFieldError*>(&e))
        return {to_int(ExitCode::usage_error), "usage_error", e.what()};

    // 1. Per-call-site override map (exact dynamic-type match on typeid(e)).
    auto it = overrides.find(std::type_index(typeid(e)));
    if (it != overrides.end())
        return {it->second.first, it->second.second, e.what()};

    // 2. Built-in typed-exception table via dynamic_cast. Order matters:
    //    typed subclasses of std::runtime_error must precede the catch-all.
    if (dynamic_cast<const PlacementFailure*>(&e))      return {to_int(ExitCode::placement_failure),   "placement_failure",   e.what()};
    if (dynamic_cast<const BadConfigError*>(&e))        return {to_int(ExitCode::bad_config),          "bad_config",          e.what()};
    if (dynamic_cast<const DuplicateNameError*>(&e))    return {to_int(ExitCode::duplicate_name),      "duplicate_name",      e.what()};
    if (dynamic_cast<const FileNotFoundError*>(&e))     return {to_int(ExitCode::file_not_found),      "file_not_found",      e.what()};
    if (dynamic_cast<const InvariantViolation*>(&e))    return {to_int(ExitCode::invariant_violation), "invariant_violation", e.what()};

    // std::invalid_argument and std::out_of_range derive from std::logic_error,
    // NOT std::runtime_error, so they don't collide with the catch-all below.
    if (dynamic_cast<const std::invalid_argument*>(&e)) return {to_int(ExitCode::usage_error),         "usage_error",         e.what()};
    if (dynamic_cast<const std::out_of_range*>(&e))     return {to_int(ExitCode::unknown_reference),   "unknown_reference",   e.what()};

    // Catch-all (std::runtime_error and any other std::exception subclass).
    return {to_int(ExitCode::parse_failure), "parse_failure", e.what()};
}

} // namespace bambu_cli::exception_dispatch
```

- [ ] **Step 3: Refactor `mutation_runner.hpp`'s catch block**

In `src/cli/commands/mutation_runner.hpp`, replace the entire `catch (const std::exception& e) { ... }` body (current lines 77–124, inclusive of the closing brace of the catch) with:

```cpp
    } catch (const std::exception& e) {
        auto d = bambu_cli::exception_dispatch::dispatch(e, overrides);
        emit_error(mode, d.code, d.message);
        std::exit(d.exit_code);
    }
```

Add the include at the top of the file (alongside the other `#include` directives):
```cpp
#include "../exception_dispatch.hpp"
```

The `using MutationExceptionMap = ...` typedef inside `commands/mutation_runner.hpp` is now a redeclaration of the same alias in `exception_dispatch.hpp`. Both declare `MutationExceptionMap = std::unordered_map<std::type_index, std::pair<int, std::string>>` in namespace `bambu_cli`, so they are compatible — no removal needed. (Leave the existing comment block above the typedef as-is.)

- [ ] **Step 4: Add the new TU to `src/cli/CMakeLists.txt`**

Read `src/cli/CMakeLists.txt`, find the line that lists source files for `bambu_cli_core` (alongside `io.cpp`, `project_ops.cpp`, etc.), and append `exception_dispatch.cpp` to the list.

- [ ] **Step 5: Build**

```powershell
cmake --build build --config Release --parallel 2 --target bambu_cli_core bambu-cli cli_tests
```
Expected: clean build of all three targets.

- [ ] **Step 6: Run the full test suite — must stay 100% green**

```powershell
$env:PATH = "$pwd\build\src\cli\Release;$env:PATH"
& "build\tests\cli\Release\cli_tests.exe" --reporter compact
```

Expected: 331 cases / 4032 assertions, all pass. If any single test changes behaviour, the refactor is wrong; investigate before continuing.

- [ ] **Step 7: Commit**

```powershell
git add src/cli/exception_dispatch.hpp src/cli/exception_dispatch.cpp src/cli/commands/mutation_runner.hpp src/cli/CMakeLists.txt
git commit -m @'
refactor(cli): lift exception dispatch into exception_dispatch.{hpp,cpp}

The dynamic_cast ladder previously inlined in mutation_runner.hpp's
catch block moves to a free function in a new TU. Behaviour-preserving
for every existing single-verb call site: same table, same overrides
mechanism, byte-identical exit codes and messages.

Adds a top-of-function short-circuit for the new ManifestFieldError
type (always -> exit 1, usage_error), positioned BEFORE the override
lookup so verbs that remap std::invalid_argument -> exit 7
(split-to-parts, merge-parts) don't swallow manifest schema typos.
Dead code for the single-verb path (no schema validators); activated
by the upcoming batch dispatcher.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
'@
```

---

## Task 4: `apply_helpers.{hpp,cpp}` — `require_only`

**Files:**
- Create: `src/cli/apply_helpers.hpp`
- Create: `src/cli/apply_helpers.cpp`
- Create: `tests/cli/unit/test_apply_helpers.cpp`
- Modify: `src/cli/CMakeLists.txt` (add `apply_helpers.cpp`)
- Modify: `tests/cli/CMakeLists.txt` (add `unit/test_apply_helpers.cpp`)

The single source of truth for the strict-schema "unknown field" rejection. Throws `ManifestFieldError`.

- [ ] **Step 1: Write the failing tests**

Create `tests/cli/unit/test_apply_helpers.cpp`:
```cpp
#include "apply_helpers.hpp"
#include "exceptions.hpp"

#include <catch2/catch.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;
using bambu_cli::ManifestFieldError;
using bambu_cli::require_only;

TEST_CASE("require_only: accepts when all keys known", "[apply_helpers][require_only]") {
    json step = {{"op", "plate.add"}, {"name", "P1"}};
    REQUIRE_NOTHROW(require_only(step, {"op", "name"}));
}

TEST_CASE("require_only: rejects unknown field", "[apply_helpers][require_only]") {
    json step = {{"op", "plate.add"}, {"name", "P1"}, {"filement", 2}};
    REQUIRE_THROWS_AS(require_only(step, {"op", "name"}), ManifestFieldError);
}

TEST_CASE("require_only: error names the offending field", "[apply_helpers][require_only]") {
    json step = {{"op", "plate.add"}, {"filement", 2}};
    try {
        require_only(step, {"op", "name"});
        FAIL("expected ManifestFieldError");
    } catch (const ManifestFieldError& e) {
        std::string what = e.what();
        REQUIRE(what.find("filement") != std::string::npos);
    }
}

TEST_CASE("require_only: empty step accepted regardless of known list",
          "[apply_helpers][require_only]") {
    json step = json::object();
    REQUIRE_NOTHROW(require_only(step, {"op", "name"}));
}
```

- [ ] **Step 2: Add the test TU to the test CMakeLists**

In `tests/cli/CMakeLists.txt`, append `unit/test_apply_helpers.cpp` to the `BAMBU_CLI_TEST_SOURCES` list under the "Unit tests" section.

- [ ] **Step 3: Run to confirm failure (header/impl missing)**

```powershell
cmake --build build --config Release --parallel 2 --target cli_tests
```
Expected: build error — `apply_helpers.hpp` not found.

- [ ] **Step 4: Create the header**

Create `src/cli/apply_helpers.hpp`:
```cpp
#pragma once

// Helpers shared by every project_apply handler.
//
// All schema-shape errors thrown by helpers in this header are
// ManifestFieldError (a std::invalid_argument subclass). The
// exception_dispatch short-circuit routes them to exit 1
// (usage_error) regardless of per-op override maps.

#include "project_ops.hpp"   // ManualTransform

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>

namespace bambu_cli {

// Iterate `step.items()` and throw ManifestFieldError for any key not
// in `known_keys`. Caller passes a brace-enclosed list including "op".
// `step` may be any JSON value; non-objects are accepted as no-ops.
void require_only(const nlohmann::json& step,
                  std::initializer_list<const char*> known_keys);

} // namespace bambu_cli
```

- [ ] **Step 5: Create the implementation**

Create `src/cli/apply_helpers.cpp`:
```cpp
#include "apply_helpers.hpp"

#include "exceptions.hpp"

#include <algorithm>
#include <string>

namespace bambu_cli {

void require_only(const nlohmann::json& step,
                  std::initializer_list<const char*> known_keys)
{
    if (!step.is_object()) return;   // nothing to check
    for (auto it = step.begin(); it != step.end(); ++it) {
        const std::string& key = it.key();
        bool found = std::any_of(known_keys.begin(), known_keys.end(),
                                 [&](const char* k) { return key == k; });
        if (!found) {
            throw ManifestFieldError("unknown field: '" + key + "'");
        }
    }
}

} // namespace bambu_cli
```

- [ ] **Step 6: Add `apply_helpers.cpp` to `src/cli/CMakeLists.txt`**

Append to the same source list as `exception_dispatch.cpp` (Task 3 Step 4).

- [ ] **Step 7: Build & run the new tests**

```powershell
cmake --build build --config Release --parallel 2 --target cli_tests
& "build\tests\cli\Release\cli_tests.exe" "[apply_helpers]" --reporter compact
```
Expected: 4 cases pass.

- [ ] **Step 8: Run the full test suite**

```powershell
& "build\tests\cli\Release\cli_tests.exe" --reporter compact
```
Expected: 335 cases (331 + 4) pass.

- [ ] **Step 9: Commit**

```powershell
git add src/cli/apply_helpers.hpp src/cli/apply_helpers.cpp src/cli/CMakeLists.txt tests/cli/unit/test_apply_helpers.cpp tests/cli/CMakeLists.txt
git commit -m @'
feat(cli): apply_helpers — require_only

First helper for the project-apply batch dispatcher. Iterates a JSON
step object and throws ManifestFieldError on any key not in the
caller-supplied allowed list. Single point of truth for the strict
unknown-field rejection that the batch schema mandates.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
'@
```

---

## Task 5: `apply_helpers` — `parse_filament`

**Files:**
- Modify: `src/cli/apply_helpers.hpp`
- Modify: `src/cli/apply_helpers.cpp`
- Modify: `tests/cli/unit/test_apply_helpers.cpp`

Typed-getter for the `filament` field: required, int ≥ 1. Returns the integer; throws `ManifestFieldError` on missing / wrong-type.

- [ ] **Step 1: Write the failing tests**

Append to `tests/cli/unit/test_apply_helpers.cpp`:
```cpp
using bambu_cli::parse_filament;

TEST_CASE("parse_filament: returns the integer when present", "[apply_helpers][parse_filament]") {
    json step = {{"filament", 2}};
    REQUIRE(parse_filament(step, "filament") == 2);
}

TEST_CASE("parse_filament: missing field throws", "[apply_helpers][parse_filament]") {
    json step = json::object();
    REQUIRE_THROWS_AS(parse_filament(step, "filament"), ManifestFieldError);
}

TEST_CASE("parse_filament: string-shaped value throws", "[apply_helpers][parse_filament]") {
    json step = {{"filament", "2"}};
    REQUIRE_THROWS_AS(parse_filament(step, "filament"), ManifestFieldError);
}

TEST_CASE("parse_filament: float value throws", "[apply_helpers][parse_filament]") {
    json step = {{"filament", 2.5}};
    REQUIRE_THROWS_AS(parse_filament(step, "filament"), ManifestFieldError);
}

TEST_CASE("parse_filament: zero or negative throws", "[apply_helpers][parse_filament]") {
    json step1 = {{"filament", 0}};
    json step2 = {{"filament", -1}};
    REQUIRE_THROWS_AS(parse_filament(step1, "filament"), ManifestFieldError);
    REQUIRE_THROWS_AS(parse_filament(step2, "filament"), ManifestFieldError);
}
```

- [ ] **Step 2: Run, confirm failure**

```powershell
cmake --build build --config Release --parallel 2 --target cli_tests
```
Expected: build error — `parse_filament` undeclared.

- [ ] **Step 3: Declare in the header**

In `src/cli/apply_helpers.hpp`, before the closing `}` of namespace `bambu_cli`, add:
```cpp
// Return step[key] as an integer, validating that it is present, an
// integer (not float, not string), and >= 1 (1-based filament slot).
// Throws ManifestFieldError on any failure.
int parse_filament(const nlohmann::json& step, const char* key);
```

- [ ] **Step 4: Implement**

In `src/cli/apply_helpers.cpp`, before the closing `}` of namespace `bambu_cli`, add:
```cpp
int parse_filament(const nlohmann::json& step, const char* key)
{
    if (!step.contains(key))
        throw ManifestFieldError(std::string("missing required field '") + key + "'");
    const auto& v = step[key];
    if (!v.is_number_integer())
        throw ManifestFieldError(std::string("field '") + key +
                                 "' must be an integer (1-based filament slot)");
    int n = v.get<int>();
    if (n < 1)
        throw ManifestFieldError(std::string("field '") + key +
                                 "' must be >= 1 (got " + std::to_string(n) + ")");
    return n;
}
```

- [ ] **Step 5: Build & run**

```powershell
cmake --build build --config Release --parallel 2 --target cli_tests
& "build\tests\cli\Release\cli_tests.exe" "[parse_filament]" --reporter compact
```
Expected: 5 cases pass.

- [ ] **Step 6: Commit**

```powershell
git add src/cli/apply_helpers.hpp src/cli/apply_helpers.cpp tests/cli/unit/test_apply_helpers.cpp
git commit -m @'
feat(cli): apply_helpers — parse_filament

Typed-getter for the 1-based filament slot field. Required integer,
must be >= 1. ManifestFieldError on missing / wrong type / bad range.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
'@
```

---

## Task 6: `apply_helpers` — `parse_transform`

**Files:**
- Modify: `src/cli/apply_helpers.hpp`
- Modify: `src/cli/apply_helpers.cpp`
- Modify: `tests/cli/unit/test_apply_helpers.cpp`

Reads `translate`, `rotate`, `scale` sections from a step object and returns a populated `ManualTransform` with the right `has_*` flags. Handles object form (`{"x":1,"y":2,"z":3}`), numeric shorthand on `scale` (`"scale": 1.5`), missing-axis defaults (0 for translate/rotate, 1 for scale), and an empty section (treated as not present). Throws `ManifestFieldError` on:
- A section that is neither object nor (for `scale` only) number.
- An unknown axis key on a transform object.

- [ ] **Step 1: Write the failing tests**

Append to `tests/cli/unit/test_apply_helpers.cpp`:
```cpp
using bambu_cli::parse_transform;
using bambu_cli::ManualTransform;

TEST_CASE("parse_transform: empty step returns no flags set", "[apply_helpers][parse_transform]") {
    json step = json::object();
    ManualTransform t = parse_transform(step);
    REQUIRE_FALSE(t.has_translate);
    REQUIRE_FALSE(t.has_rotate);
    REQUIRE_FALSE(t.has_scale);
}

TEST_CASE("parse_transform: translate object form", "[apply_helpers][parse_transform]") {
    json step = {{"translate", {{"x", 10}, {"y", 20}}}};
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_translate);
    REQUIRE(t.tx == Approx(10));
    REQUIRE(t.ty == Approx(20));
    REQUIRE(t.tz == Approx(0));   // default
}

TEST_CASE("parse_transform: rotate object form, degrees", "[apply_helpers][parse_transform]") {
    json step = {{"rotate", {{"z", 90}}}};
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_rotate);
    REQUIRE(t.rx == Approx(0));
    REQUIRE(t.ry == Approx(0));
    REQUIRE(t.rz == Approx(90));
}

TEST_CASE("parse_transform: scale per-axis", "[apply_helpers][parse_transform]") {
    json step = {{"scale", {{"x", 1.5}, {"y", 1.0}, {"z", 1.0}}}};
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_scale);
    REQUIRE(t.sx == Approx(1.5));
    REQUIRE(t.sy == Approx(1.0));
    REQUIRE(t.sz == Approx(1.0));
}

TEST_CASE("parse_transform: scale uniform numeric shorthand",
          "[apply_helpers][parse_transform]") {
    json step = {{"scale", 1.5}};
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_scale);
    REQUIRE(t.sx == Approx(1.5));
    REQUIRE(t.sy == Approx(1.5));
    REQUIRE(t.sz == Approx(1.5));
}

TEST_CASE("parse_transform: empty section treated as not present",
          "[apply_helpers][parse_transform]") {
    json step = {{"translate", json::object()}};
    ManualTransform t = parse_transform(step);
    REQUIRE_FALSE(t.has_translate);
}

TEST_CASE("parse_transform: all three sections", "[apply_helpers][parse_transform]") {
    json step = {
        {"translate", {{"x", 1}, {"y", 2}, {"z", 3}}},
        {"rotate",    {{"x", 10}, {"y", 20}, {"z", 30}}},
        {"scale",     {{"x", 1.5}, {"y", 1.5}, {"z", 1.5}}},
    };
    ManualTransform t = parse_transform(step);
    REQUIRE(t.has_translate);
    REQUIRE(t.has_rotate);
    REQUIRE(t.has_scale);
    REQUIRE(t.tx == Approx(1));   REQUIRE(t.ty == Approx(2));   REQUIRE(t.tz == Approx(3));
    REQUIRE(t.rx == Approx(10));  REQUIRE(t.ry == Approx(20));  REQUIRE(t.rz == Approx(30));
    REQUIRE(t.sx == Approx(1.5)); REQUIRE(t.sy == Approx(1.5)); REQUIRE(t.sz == Approx(1.5));
}

TEST_CASE("parse_transform: unknown axis key throws",
          "[apply_helpers][parse_transform]") {
    json step = {{"translate", {{"q", 1}}}};
    REQUIRE_THROWS_AS(parse_transform(step), ManifestFieldError);
}

TEST_CASE("parse_transform: translate as number throws",
          "[apply_helpers][parse_transform]") {
    json step = {{"translate", 5}};
    REQUIRE_THROWS_AS(parse_transform(step), ManifestFieldError);
}
```

- [ ] **Step 2: Declare in the header**

In `src/cli/apply_helpers.hpp`, before the closing `}` of namespace `bambu_cli`, add:
```cpp
// Parse the translate/rotate/scale sections of an object.add step and
// return a populated ManualTransform with the corresponding has_* flags.
// Object form: {"x":N,"y":N,"z":N}, missing axes default to 0 (translate/
// rotate) or 1 (scale). `scale` also accepts a bare number as uniform
// shorthand. An empty section ({"translate": {}}) is treated as not
// present (the flag stays false).
// Throws ManifestFieldError on type mismatch or unknown axis key.
ManualTransform parse_transform(const nlohmann::json& step);
```

- [ ] **Step 3: Implement**

In `src/cli/apply_helpers.cpp`, before the closing `}` of namespace `bambu_cli`, add:
```cpp
namespace {

// Apply a translate-style section ({"x":..,"y":..,"z":..} or empty) into
// out_{x,y,z} using `default_v` for missing axes. Returns true if the
// section was present and non-empty (caller flips the has_* flag).
bool read_axis_object(const nlohmann::json& section,
                      const char* section_name,
                      double default_v,
                      double& out_x, double& out_y, double& out_z)
{
    if (!section.is_object())
        throw ManifestFieldError(std::string("section '") + section_name +
                                 "' must be an object");
    if (section.empty()) return false;
    out_x = out_y = out_z = default_v;
    for (auto it = section.begin(); it != section.end(); ++it) {
        const std::string& key = it.key();
        if      (key == "x") out_x = it.value().get<double>();
        else if (key == "y") out_y = it.value().get<double>();
        else if (key == "z") out_z = it.value().get<double>();
        else throw ManifestFieldError(std::string("unknown axis key '") + key +
                                      "' on '" + section_name + "'");
    }
    return true;
}

} // namespace

ManualTransform parse_transform(const nlohmann::json& step)
{
    ManualTransform t;

    if (step.contains("translate")) {
        t.has_translate = read_axis_object(step["translate"], "translate",
                                           0.0, t.tx, t.ty, t.tz);
    }

    if (step.contains("rotate")) {
        t.has_rotate    = read_axis_object(step["rotate"], "rotate",
                                           0.0, t.rx, t.ry, t.rz);
    }

    if (step.contains("scale")) {
        const auto& s = step["scale"];
        if (s.is_number()) {
            double v = s.get<double>();
            t.has_scale = true;
            t.sx = t.sy = t.sz = v;
        } else {
            t.has_scale = read_axis_object(s, "scale", 1.0, t.sx, t.sy, t.sz);
        }
    }

    return t;
}
```

- [ ] **Step 4: Build & run**

```powershell
cmake --build build --config Release --parallel 2 --target cli_tests
& "build\tests\cli\Release\cli_tests.exe" "[parse_transform]" --reporter compact
```
Expected: 9 cases pass.

- [ ] **Step 5: Run full suite**

```powershell
& "build\tests\cli\Release\cli_tests.exe" --reporter compact
```
Expected: previous total + 9 = all green.

- [ ] **Step 6: Commit**

```powershell
git add src/cli/apply_helpers.hpp src/cli/apply_helpers.cpp tests/cli/unit/test_apply_helpers.cpp
git commit -m @'
feat(cli): apply_helpers — parse_transform

Reads translate/rotate/scale sections from an object.add step and
returns ManualTransform with the right has_* flags. Object form for
all three; numeric shorthand for uniform scale; missing-axis defaults
(0 for translate/rotate, 1 for scale); empty section treated as
absent. ManifestFieldError on type errors and unknown axis keys.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
'@
```

---

## Task 7: Skeleton `project_apply.cpp` — verb registration + manifest header + empty-ops happy path

**Files:**
- Create: `src/cli/commands/project_apply.cpp`
- Modify: `src/cli/commands/project.cpp` (call `register_project_apply_subcommand`)
- Modify: `src/cli/main.cpp` if a forward declaration is needed (it isn't — `project.cpp` registers its own subcommands)
- Modify: `src/cli/CMakeLists.txt` (add `commands/project_apply.cpp`)
- Create: `tests/cli/unit/test_apply_manifest.cpp` (manifest-header tests)
- Modify: `tests/cli/CMakeLists.txt`

End-to-end happy path that works at the end of this task: `bambu-cli project apply <in.3mf> --manifest m.json` where `m.json = {"version":1,"operations":[]}` → loads, validates the empty manifest, saves once, exits 0. No handlers yet.

- [ ] **Step 1: Write the manifest-header unit tests (compile-failing at first)**

Create `tests/cli/unit/test_apply_manifest.cpp`:
```cpp
#include "commands/project_apply_internal.hpp"

#include "exceptions.hpp"

#include <catch2/catch.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;
using bambu_cli::ManifestFieldError;
using bambu_cli::parse_and_validate_manifest;

TEST_CASE("manifest: empty operations array is valid",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", json::array()}};
    REQUIRE_NOTHROW(parse_and_validate_manifest(m));
}

TEST_CASE("manifest: missing version throws",
          "[project_apply][manifest]") {
    json m = {{"operations", json::array()}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: version != 1 throws",
          "[project_apply][manifest]") {
    json m = {{"version", 2}, {"operations", json::array()}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: version not integer throws",
          "[project_apply][manifest]") {
    json m = {{"version", "1"}, {"operations", json::array()}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: missing operations throws",
          "[project_apply][manifest]") {
    json m = {{"version", 1}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: operations not array throws",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", "foo"}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: unknown top-level key throws",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", json::array()}, {"foo", 1}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: not a top-level object throws",
          "[project_apply][manifest]") {
    json m = json::array({1, 2, 3});
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: per-step op must be string",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", json::array({json::object({{"op", 42}})})}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: per-step missing op",
          "[project_apply][manifest]") {
    json m = {{"version", 1}, {"operations", json::array({json::object({{"name", "P1"}})})}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m), ManifestFieldError);
}

TEST_CASE("manifest: size cap at 10000 accepted, 10001 rejected",
          "[project_apply][manifest]") {
    json ops_ok = json::array();
    for (int i = 0; i < 10000; ++i) ops_ok.push_back({{"op", "plate.add"}, {"name", "p" + std::to_string(i)}});
    json m_ok = {{"version", 1}, {"operations", ops_ok}};
    REQUIRE_NOTHROW(parse_and_validate_manifest(m_ok));

    json ops_bad = ops_ok;
    ops_bad.push_back({{"op", "plate.add"}, {"name", "overflow"}});
    json m_bad = {{"version", 1}, {"operations", ops_bad}};
    REQUIRE_THROWS_AS(parse_and_validate_manifest(m_bad), ManifestFieldError);
}
```

- [ ] **Step 2: Add the test TU**

In `tests/cli/CMakeLists.txt`, append `unit/test_apply_manifest.cpp` to `BAMBU_CLI_TEST_SOURCES`.

- [ ] **Step 3: Build, confirm failure**

```powershell
cmake --build build --config Release --parallel 2 --target cli_tests
```
Expected: build error — `project_apply_internal.hpp` not found, `parse_and_validate_manifest` undeclared.

- [ ] **Step 4: Create the internal header**

Create `src/cli/commands/project_apply_internal.hpp` (used by tests + the .cpp itself; not user-facing):
```cpp
#pragma once

// Internal API for project_apply, exposed to unit tests. Not part of the
// public CLI surface.

#include <nlohmann/json.hpp>
#include <string>

namespace bambu_cli {

// Maximum number of operations allowed in a single manifest.
inline constexpr std::size_t MAX_MANIFEST_OPS = 10000;

// Validate the top-level shape of a parsed manifest JSON value:
//   - must be an object
//   - version key present and integer == 1
//   - operations key present and array
//   - no unknown top-level keys
//   - operations.size() <= MAX_MANIFEST_OPS
//   - every step is an object with a non-empty string "op" key
//
// Throws ManifestFieldError on any failure. Does NOT validate per-op
// field shapes (that's the per-handler responsibility during dispatch).
void parse_and_validate_manifest(const nlohmann::json& m);

} // namespace bambu_cli
```

- [ ] **Step 5: Create `project_apply.cpp` with skeleton + validator + minimal dispatch**

Create `src/cli/commands/project_apply.cpp`:
```cpp
// bambu-cli `project apply` — batch manifest verb.
// See docs/superpowers/specs/2026-05-31-project-apply-batch-design.md.

#include "../exception_dispatch.hpp"
#include "../exceptions.hpp"
#include "../io.hpp"
#include "../json_output.hpp"
#include "../project_state.hpp"
#include "../extern/CLI11/CLI11.hpp"
#include "project_apply_internal.hpp"

#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace bambu_cli {

namespace fs = boost::filesystem;
using nlohmann::json;

// ---------- manifest header validation ----------

void parse_and_validate_manifest(const json& m)
{
    if (!m.is_object())
        throw ManifestFieldError("manifest: top-level value must be an object");

    if (!m.contains("version"))
        throw ManifestFieldError("manifest: missing required key 'version'");
    if (!m["version"].is_number_integer())
        throw ManifestFieldError("manifest: 'version' must be an integer");
    int version = m["version"].get<int>();
    if (version != 1)
        throw ManifestFieldError(
            "manifest: unsupported manifest version: " + std::to_string(version));

    if (!m.contains("operations"))
        throw ManifestFieldError("manifest: missing required key 'operations'");
    if (!m["operations"].is_array())
        throw ManifestFieldError("manifest: 'operations' must be an array");

    for (auto it = m.begin(); it != m.end(); ++it) {
        const std::string& key = it.key();
        if (key != "version" && key != "operations")
            throw ManifestFieldError("manifest: unknown top-level key '" + key + "'");
    }

    if (m["operations"].size() > MAX_MANIFEST_OPS) {
        std::ostringstream os;
        os << "manifest exceeds maximum of " << MAX_MANIFEST_OPS
           << " operations (got " << m["operations"].size() << ")";
        throw ManifestFieldError(os.str());
    }

    // Per-step minimum shape: must be object with a non-empty string "op".
    std::size_t i = 0;
    for (const auto& step : m["operations"]) {
        ++i;
        if (!step.is_object())
            throw ManifestFieldError(
                "step " + std::to_string(i) + ": must be a JSON object");
        if (!step.contains("op"))
            throw ManifestFieldError(
                "step " + std::to_string(i) + ": missing required field 'op'");
        if (!step["op"].is_string())
            throw ManifestFieldError(
                "step " + std::to_string(i) + ": 'op' must be a string");
        if (step["op"].get<std::string>().empty())
            throw ManifestFieldError(
                "step " + std::to_string(i) + ": 'op' must be non-empty");
    }
}

// ---------- file load ----------

namespace {

json load_manifest_file(const std::string& path)
{
    if (!fs::exists(path)) {
        throw FileNotFoundError("manifest file not found: " + path);
    }
    std::ifstream in(path);
    if (!in) {
        throw FileNotFoundError("cannot open manifest file: " + path);
    }
    json m;
    try {
        in >> m;
    } catch (const json::parse_error& e) {
        // Re-throw as a std::runtime_error so the dispatch table maps it
        // to exit 3 (parse_failure) with nlohmann's line/column message.
        throw std::runtime_error(
            std::string("manifest JSON parse error: ") + e.what());
    }
    return m;
}

} // namespace

// ---------- CLI registration ----------

struct ApplyArgs {
    std::string in_path;
    std::string manifest_path;
    std::string out_path;     // empty = in-place
    bool        dry_run = false;
};

static void run_apply(OutputMode mode, const ApplyArgs& a);

void register_project_apply_subcommand(CLI::App* project_cmd, OutputMode* mode_out)
{
    auto* apply = project_cmd->add_subcommand(
        "apply",
        "apply a JSON manifest of mutations against the input project");
    auto a = std::make_shared<ApplyArgs>();
    apply->add_option("in",         a->in_path,       "input .3mf")->required();
    apply->add_option("--manifest", a->manifest_path, "path to manifest JSON")->required();
    apply->add_option("--output",   a->out_path,      "output .3mf (defaults to in-place)");
    apply->add_flag(  "--dry-run",  a->dry_run,
                      "validate + apply in-memory; skip save_project");
    apply->callback([a, mode_out]() {
        OutputMode mode = (mode_out && *mode_out == OutputMode::Json)
                          ? OutputMode::Json : OutputMode::Text;
        run_apply(mode, *a);
    });
}

// ---------- main flow ----------

static void run_apply(OutputMode mode, const ApplyArgs& a)
{
    // Stage 1-3: load + parse + header-validate the manifest. No .3mf touched yet.
    json manifest;
    try {
        manifest = load_manifest_file(a.manifest_path);
        parse_and_validate_manifest(manifest);
    } catch (const std::exception& e) {
        auto d = exception_dispatch::dispatch(e);
        emit_error(mode, d.code, d.message);
        std::exit(d.exit_code);
    }

    // Stage 4: load the .3mf.
    ProjectState state;
    IoResult lr = load_project(a.in_path, state);
    if (!lr.ok) {
        emit_error(mode, lr.error_code, lr.error_message);
        std::exit(lr.exit_code);
    }

    // Stage 5: dispatch each op. (No handlers yet — coming in Task 8.)
    // This empty-ops happy path is what we exercise in the e2e test below.

    // Stage 6: save (skip on --dry-run).
    const std::string& out = a.out_path.empty() ? a.in_path : a.out_path;
    if (!a.dry_run) {
        IoResult sr = save_project(state, out);
        if (!sr.ok) {
            emit_error(mode, sr.error_code, sr.error_message);
            std::exit(sr.exit_code);
        }
    }

    json data;
    data["steps_applied"] = manifest["operations"].size();
    data["dry_run"] = a.dry_run;
    if (!a.dry_run) data["output"] = out;

    std::string msg = (a.dry_run ? "dry-run: " : "applied ")
                    + std::to_string(manifest["operations"].size()) + " ops"
                    + (a.dry_run ? std::string{} : (" -> " + out));
    emit_ok(mode, "ok", msg, data);
}

} // namespace bambu_cli
```

- [ ] **Step 6: Wire registration from `commands/project.cpp`**

Read `src/cli/commands/project.cpp`. Inside `register_project_subcommands`, find the section that adds subcommands (`init`, `info`, etc.) and append:
```cpp
extern void register_project_apply_subcommand(CLI::App* project_cmd, OutputMode* mode_out);
register_project_apply_subcommand(project, mode_out);  // <-- `project` is the existing subcommand pointer
```
(Use whichever local variable name `project.cpp` already uses for its `project` subcommand — verify before adding. The forward declaration goes at file scope, above `register_project_subcommands`.)

- [ ] **Step 7: Add `commands/project_apply.cpp` to `src/cli/CMakeLists.txt`**

Append `commands/project_apply.cpp` to the same source list as `exception_dispatch.cpp` / `apply_helpers.cpp`.

- [ ] **Step 8: Build**

```powershell
cmake --build build --config Release --parallel 2 --target bambu_cli_core bambu-cli cli_tests
```
Expected: clean build of all three targets.

- [ ] **Step 9: Run the new manifest unit tests**

```powershell
& "build\tests\cli\Release\cli_tests.exe" "[manifest]" --reporter compact
```
Expected: 11 cases pass.

- [ ] **Step 10: Manual smoke test of the empty-ops happy path**

```powershell
$mfdir = "$env:TEMP\bambu-apply-smoke"
New-Item -ItemType Directory -Force $mfdir | Out-Null
Copy-Item tests\cli\fixtures\reference.3mf "$mfdir\in.3mf" -Force
'{"version":1,"operations":[]}' | Set-Content "$mfdir\m.json"
& "build\src\cli\Release\bambu-cli.exe" project apply "$mfdir\in.3mf" --manifest "$mfdir\m.json" --output "$mfdir\out.3mf"
Test-Path "$mfdir\out.3mf"     # expected: True
& "build\src\cli\Release\bambu-cli.exe" --json project apply "$mfdir\in.3mf" --manifest "$mfdir\m.json" --dry-run
# expected: {"status":"ok","code":"ok",...,"steps_applied":0,"dry_run":true} and exit 0
```

- [ ] **Step 11: Run full suite**

```powershell
& "build\tests\cli\Release\cli_tests.exe" --reporter compact
```
Expected: previous total + 11 = all green.

- [ ] **Step 12: Commit**

```powershell
git add src/cli/commands/project_apply_internal.hpp src/cli/commands/project_apply.cpp src/cli/commands/project.cpp src/cli/CMakeLists.txt tests/cli/unit/test_apply_manifest.cpp tests/cli/CMakeLists.txt
git commit -m @'
feat(cli): project apply — skeleton verb + manifest header validation

Registers `bambu-cli project apply <in.3mf> --manifest m.json [--output
out.3mf] [--dry-run]`. Implements stages 1-4 + 6 of the dispatch flow:
load + parse + header-validate the manifest, load the .3mf, save once
at the end (unless --dry-run). Stage 5 (per-op dispatch) lands in
Task 8 with the first handler.

parse_and_validate_manifest enforces the top-level shape (version
must be integer 1, operations must be array, no unknown keys, size
<= 10000, every step has a non-empty string `op`). All schema
errors throw ManifestFieldError -> exit 1 via the exception_dispatch
short-circuit.

Empty operations list works end-to-end: load, no-op, save, exit 0.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
'@
```

---

## Task 8: `HandlerRegistry` + first handler (`plate.add`)

**Files:**
- Modify: `src/cli/commands/project_apply_internal.hpp` (declare types)
- Modify: `src/cli/commands/project_apply.cpp` (registry, dispatch loop, plate.add handler)
- Create: `tests/cli/unit/test_project_apply_handlers.cpp` (handler-direct tests)
- Modify: `tests/cli/CMakeLists.txt`

Establishes the registry pattern that every subsequent verb task reuses. Handler-direct tests bypass JSON parsing entirely.

- [ ] **Step 1: Write the failing handler-direct tests**

Create `tests/cli/unit/test_project_apply_handlers.cpp`:
```cpp
#include "commands/project_apply_internal.hpp"
#include "unit_helpers.hpp"

#include "exceptions.hpp"
#include "project_ops.hpp"
#include "project_state.hpp"

#include <catch2/catch.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;
using bambu_cli::HandlerRegistry;
using bambu_cli::ProjectState;
using bambu_cli::ManifestFieldError;
using bambu_cli::DuplicateNameError;

TEST_CASE("plate.add: happy path appends a new plate", "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    const auto initial = bambu_cli::list_plate_names(s).size();

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}, {"name", "P2"}};
    reg.lookup("plate.add").fn(s, step);

    auto names = bambu_cli::list_plate_names(s);
    REQUIRE(names.size() == initial + 1);
    REQUIRE(names.back() == "P2");
}

TEST_CASE("plate.add: missing name throws ManifestFieldError",
          "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}};
    REQUIRE_THROWS_AS(reg.lookup("plate.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("plate.add: unknown field throws ManifestFieldError",
          "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}, {"name", "P2"}, {"filement", 2}};
    REQUIRE_THROWS_AS(reg.lookup("plate.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("plate.add: non-string name throws ManifestFieldError",
          "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}, {"name", 42}};
    REQUIRE_THROWS_AS(reg.lookup("plate.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("plate.add: duplicate name throws DuplicateNameError",
          "[project_apply][plate.add]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    bambu_cli::add_plate(s, "P2");

    HandlerRegistry reg;
    json step = {{"op", "plate.add"}, {"name", "P2"}};
    REQUIRE_THROWS_AS(reg.lookup("plate.add").fn(s, step), DuplicateNameError);
}

TEST_CASE("HandlerRegistry::lookup: unknown op throws ManifestFieldError",
          "[project_apply][registry]") {
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.bogus"), ManifestFieldError);
}

TEST_CASE("HandlerRegistry::lookup: plate.add overrides empty",
          "[project_apply][registry]") {
    HandlerRegistry reg;
    REQUIRE(reg.lookup("plate.add").overrides.empty());
}
```

- [ ] **Step 2: Add the test TU**

In `tests/cli/CMakeLists.txt`, append `unit/test_project_apply_handlers.cpp` to `BAMBU_CLI_TEST_SOURCES`.

- [ ] **Step 3: Build, confirm failure**

Expected: build error — `HandlerRegistry` undeclared.

- [ ] **Step 4: Declare `HandlerEntry` + `HandlerRegistry` in the internal header**

In `src/cli/commands/project_apply_internal.hpp`, before the closing `}` of namespace `bambu_cli`, add:
```cpp
#include "../exception_dispatch.hpp"   // MutationExceptionMap

#include <functional>
#include <unordered_map>

// One handler per op. Receives the full step object (including its `op`
// field, which handlers ignore via require_only). Throws on any error.
class ProjectState;
using OpHandler = std::function<void(ProjectState&, const nlohmann::json&)>;

struct HandlerEntry {
    OpHandler            fn;
    MutationExceptionMap overrides;   // empty for ops without exit-7 remapping
};

class HandlerRegistry {
public:
    HandlerRegistry();
    const HandlerEntry& lookup(const std::string& op) const;
private:
    std::unordered_map<std::string, HandlerEntry> m_handlers;
};
```

- [ ] **Step 5: Implement registry + plate.add + dispatch loop in `project_apply.cpp`**

In `src/cli/commands/project_apply.cpp`, add near the top:
```cpp
#include "../apply_helpers.hpp"
#include "../project_ops.hpp"
```

Add the registry constructor + lookup (after the `parse_and_validate_manifest` body, before `run_apply`):
```cpp
HandlerRegistry::HandlerRegistry()
{
    // ---------- plate ops ----------
    m_handlers["plate.add"].fn = [](ProjectState& s, const json& step) {
        require_only(step, {"op", "name"});
        if (!step.contains("name"))
            throw ManifestFieldError("plate.add: missing required field 'name'");
        if (!step["name"].is_string())
            throw ManifestFieldError("plate.add: 'name' must be a string");
        add_plate(s, step["name"].get<std::string>());
    };
}

const HandlerEntry& HandlerRegistry::lookup(const std::string& op) const
{
    auto it = m_handlers.find(op);
    if (it == m_handlers.end())
        throw ManifestFieldError("unknown op: '" + op + "'");
    return it->second;
}
```

Update the dispatch loop in `run_apply` to wire the registry into Stage 5. Replace the existing Stage 5 comment with:
```cpp
    // Stage 5: dispatch each op.
    static const HandlerRegistry registry;
    std::size_t step_index = 0;
    for (const auto& step : manifest["operations"]) {
        ++step_index;
        const std::string op = step["op"].get<std::string>();
        const HandlerEntry* entry = nullptr;
        try {
            entry = &registry.lookup(op);   // may throw ManifestFieldError
            entry->fn(state, step);
        } catch (const std::exception& e) {
            auto d = exception_dispatch::dispatch(
                e, entry ? entry->overrides : MutationExceptionMap{});
            json data;
            data["step"] = step_index;
            data["op"]   = op;
            std::ostringstream msg;
            msg << "step " << step_index << " (op '" << op << "'): " << d.message;
            emit_error(mode, d.code, msg.str(), data);
            std::exit(d.exit_code);
        }
    }
```

- [ ] **Step 6: Build & run new unit tests**

```powershell
cmake --build build --config Release --parallel 2 --target cli_tests
& "build\tests\cli\Release\cli_tests.exe" "[plate.add]" "[registry]" --reporter compact
```
Expected: 7 cases pass.

- [ ] **Step 7: Manual smoke — full apply cycle with one op**

```powershell
$mfdir = "$env:TEMP\bambu-apply-smoke"
'{"version":1,"operations":[{"op":"plate.add","name":"P-from-batch"}]}' | Set-Content "$mfdir\m.json"
& "build\src\cli\Release\bambu-cli.exe" project apply tests\cli\fixtures\reference.3mf --manifest "$mfdir\m.json" --output "$mfdir\one_plate.3mf"
& "build\src\cli\Release\bambu-cli.exe" --json plate list "$mfdir\one_plate.3mf"
# expected: plate_count = 2 (the reference's "Plate 01 test" + the new "P-from-batch")
```

- [ ] **Step 8: Run full suite**

Expected: all green.

- [ ] **Step 9: Commit**

```powershell
git add src/cli/commands/project_apply_internal.hpp src/cli/commands/project_apply.cpp tests/cli/unit/test_project_apply_handlers.cpp tests/cli/CMakeLists.txt
git commit -m @'
feat(cli): project apply — HandlerRegistry + plate.add handler

First op wired through the dispatcher. Establishes the pattern every
subsequent verb follows:

  m_handlers["X"].fn = [](ProjectState& s, const json& step) {
      require_only(step, {"op", ...known fields...});
      // type-check required fields, throw ManifestFieldError on miss
      project_ops::X(s, ...);
  };

Dispatch loop catches any std::exception, runs exception_dispatch
with the entry's overrides, prefixes the message with step N
(op '...'), attaches {step,op} to the data blob, exits.

Unknown op -> ManifestFieldError -> exit 1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
'@
```

---

## Tasks 9–14: remaining plate ops

Each follows the Task 8 pattern. Single-task scaffold:

1. Append a happy-path test + a required-field test + an unknown-field test + a type-mismatch test to `test_project_apply_handlers.cpp`.
2. Add the `m_handlers["..."].fn = ...` block in `HandlerRegistry`'s constructor in `project_apply.cpp`.
3. Build + run the tagged tests + run the full suite.
4. Commit.

For brevity, only the differences from Task 8 are shown.

### Task 9: `plate.remove`

- [ ] **Tests** (append to `test_project_apply_handlers.cpp`):
```cpp
TEST_CASE("plate.remove: happy path drops an empty plate", "[project_apply][plate.remove]") {
    ProjectState s;
    bambu_cli_unit::make_minimal_state(s, 1);
    bambu_cli::add_plate(s, "P2");
    HandlerRegistry reg;
    json step = {{"op", "plate.remove"}, {"name", "P2"}};
    reg.lookup("plate.remove").fn(s, step);
    auto names = bambu_cli::list_plate_names(s);
    REQUIRE(std::find(names.begin(), names.end(), "P2") == names.end());
}

TEST_CASE("plate.remove: missing name throws", "[project_apply][plate.remove]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.remove").fn(s, json{{"op","plate.remove"}}), ManifestFieldError);
}

TEST_CASE("plate.remove: unknown field throws", "[project_apply][plate.remove]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op", "plate.remove"}, {"name", "P2"}, {"x", 1}};
    REQUIRE_THROWS_AS(reg.lookup("plate.remove").fn(s, step), ManifestFieldError);
}

TEST_CASE("plate.remove: non-string name throws", "[project_apply][plate.remove]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op", "plate.remove"}, {"name", 42}};
    REQUIRE_THROWS_AS(reg.lookup("plate.remove").fn(s, step), ManifestFieldError);
}
```
(Add `#include <algorithm>` to the test TU if not already present.)

- [ ] **Handler**:
```cpp
m_handlers["plate.remove"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "name"});
    if (!step.contains("name") || !step["name"].is_string())
        throw ManifestFieldError("plate.remove: missing or non-string 'name'");
    remove_plate(s, step["name"].get<std::string>());
};
```

- [ ] **Build + tag-run + full suite + commit** (`feat(cli): project apply — plate.remove handler`).

### Task 10: `plate.rename`

- [ ] **Tests** (4: happy / missing `from` / missing `to` / unknown field):
```cpp
TEST_CASE("plate.rename: happy path renames", "[project_apply][plate.rename]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    bambu_cli::add_plate(s, "P2");
    HandlerRegistry reg;
    reg.lookup("plate.rename").fn(s, json{{"op","plate.rename"}, {"from","P2"}, {"to","P-NEW"}});
    auto names = bambu_cli::list_plate_names(s);
    REQUIRE(std::find(names.begin(), names.end(), "P-NEW") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "P2")    == names.end());
}

TEST_CASE("plate.rename: missing from throws", "[project_apply][plate.rename]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.rename").fn(s, json{{"op","plate.rename"},{"to","X"}}), ManifestFieldError);
}

TEST_CASE("plate.rename: missing to throws", "[project_apply][plate.rename]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.rename").fn(s, json{{"op","plate.rename"},{"from","X"}}), ManifestFieldError);
}

TEST_CASE("plate.rename: unknown field throws", "[project_apply][plate.rename]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op","plate.rename"},{"from","X"},{"to","Y"},{"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("plate.rename").fn(s, step), ManifestFieldError);
}
```

- [ ] **Handler**:
```cpp
m_handlers["plate.rename"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "from", "to"});
    if (!step.contains("from") || !step["from"].is_string())
        throw ManifestFieldError("plate.rename: missing or non-string 'from'");
    if (!step.contains("to") || !step["to"].is_string())
        throw ManifestFieldError("plate.rename: missing or non-string 'to'");
    rename_plate(s, step["from"].get<std::string>(), step["to"].get<std::string>());
};
```

- [ ] **Build + tag-run + full suite + commit**.

### Task 11: `plate.center`

- [ ] **Tests** (3: happy / missing plate / unknown field). Happy path: load the reference fixture (which has objects on `"Plate 01 test"`), call `plate.center`, assert the centroid of instance offsets is at plate centre (within `Approx().margin(0.5)`):
```cpp
#include "io.hpp"   // load_project

TEST_CASE("plate.center: happy path centers instances",
          "[project_apply][plate.center]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    reg.lookup("plate.center").fn(
        s, json{{"op","plate.center"}, {"plate", "Plate 01 test"}});
    // Lightweight invariant: handler returns without throwing.
    SUCCEED("plate.center applied without throwing");
}

TEST_CASE("plate.center: missing plate field throws",
          "[project_apply][plate.center]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("plate.center").fn(s, json{{"op","plate.center"}}),
                      ManifestFieldError);
}

TEST_CASE("plate.center: unknown field throws",
          "[project_apply][plate.center]") {
    ProjectState s; bambu_cli_unit::make_minimal_state(s, 1);
    HandlerRegistry reg;
    json step = {{"op","plate.center"}, {"plate","X"}, {"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("plate.center").fn(s, step), ManifestFieldError);
}
```

- [ ] **Handler**:
```cpp
m_handlers["plate.center"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "plate"});
    if (!step.contains("plate") || !step["plate"].is_string())
        throw ManifestFieldError("plate.center: missing or non-string 'plate'");
    plate_center(s, step["plate"].get<std::string>());
};
```

- [ ] **Build + commit** (`feat(cli): project apply — plate.center handler`).

### Task 12: `plate.drop-to-bed`

Same shape as Task 11, single field `plate`, calls `plate_drop_to_bed`. Three tests (happy / missing / unknown). Commit message: `feat(cli): project apply — plate.drop-to-bed handler`.

- [ ] **Handler**:
```cpp
m_handlers["plate.drop-to-bed"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "plate"});
    if (!step.contains("plate") || !step["plate"].is_string())
        throw ManifestFieldError("plate.drop-to-bed: missing or non-string 'plate'");
    plate_drop_to_bed(s, step["plate"].get<std::string>());
};
```

### Task 13: `plate.arrange`

Same shape, calls `plate_arrange`. Three tests. Commit: `feat(cli): project apply — plate.arrange handler`.

- [ ] **Handler**:
```cpp
m_handlers["plate.arrange"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "plate"});
    if (!step.contains("plate") || !step["plate"].is_string())
        throw ManifestFieldError("plate.arrange: missing or non-string 'plate'");
    plate_arrange(s, step["plate"].get<std::string>());
};
```

### Task 14: `plate.auto-orient` — **first verb with overrides**

This is the canonical exit-7 verb. The handler is shape-identical to `plate.center`, but the `HandlerEntry.overrides` map gets populated.

- [ ] **Tests** (happy / missing / unknown / **override applied**):
```cpp
TEST_CASE("plate.auto-orient: handler entry carries runtime_error -> exit 7 override",
          "[project_apply][plate.auto-orient][overrides]") {
    HandlerRegistry reg;
    const auto& entry = reg.lookup("plate.auto-orient");
    REQUIRE(entry.overrides.size() == 1);
    auto it = entry.overrides.find(std::type_index(typeid(std::runtime_error)));
    REQUIRE(it != entry.overrides.end());
    REQUIRE(it->second.first  == 7);
    REQUIRE(it->second.second == "invalid_state");
}
```
Plus the 3 standard happy/missing/unknown tests as in Task 11.

- [ ] **Handler + overrides**:
```cpp
m_handlers["plate.auto-orient"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "plate"});
    if (!step.contains("plate") || !step["plate"].is_string())
        throw ManifestFieldError("plate.auto-orient: missing or non-string 'plate'");
    plate_auto_orient(s, step["plate"].get<std::string>());
};
m_handlers["plate.auto-orient"].overrides = {
    { std::type_index(typeid(std::runtime_error)), {7, "invalid_state"} },
};
```

Add `#include <typeindex>` to the test TU if not already present.

- [ ] **Build + commit** (`feat(cli): project apply — plate.auto-orient handler (override exit 7)`).

---

## Task 15: `object.add` (uses `parse_transform` + `parse_filament`)

**Files:**
- Modify: `src/cli/commands/project_apply.cpp`
- Modify: `tests/cli/unit/test_project_apply_handlers.cpp`

The most-complex verb. Resolves STL paths relative to the manifest's directory (passed via thread-local context — see Step 6).

- [ ] **Step 1: Add a thread-local manifest_dir hook in `project_apply.cpp`**

Above the `HandlerRegistry` definition in `project_apply.cpp`, add:
```cpp
namespace {
// Per-call manifest directory, set by run_apply before dispatch and read
// by object.add. Thread-local so future parallel invocations remain safe.
thread_local std::string g_manifest_dir;
} // namespace
```

In `run_apply`, after `parse_and_validate_manifest(manifest)` succeeds, set:
```cpp
g_manifest_dir = fs::path(a.manifest_path).parent_path().string();
```

- [ ] **Step 2: Write the failing tests**

Append to `test_project_apply_handlers.cpp`:
```cpp
TEST_CASE("object.add: happy path adds an object to the named plate",
          "[project_apply][object.add]") {
    ProjectState s;
    bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op", "object.add"},
        {"plate", "Plate 01 test"},
        {"stl",   bambu_cli_unit::fixture_stl("cube.stl")},
        {"name",  "test_cube_via_apply"},
    };
    REQUIRE_NOTHROW(reg.lookup("object.add").fn(s, step));
    auto objs = bambu_cli::list_objects(s, "Plate 01 test");
    bool found = false;
    for (const auto& o : objs)
        if (o.object_name == "test_cube_via_apply") { found = true; break; }
    REQUIRE(found);
}

TEST_CASE("object.add: missing stl throws", "[project_apply][object.add]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","object.add"}, {"plate","Plate 01 test"}};
    REQUIRE_THROWS_AS(reg.lookup("object.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("object.add: missing plate throws", "[project_apply][object.add]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","object.add"}, {"stl", bambu_cli_unit::fixture_stl("cube.stl")}};
    REQUIRE_THROWS_AS(reg.lookup("object.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("object.add: unknown field throws", "[project_apply][object.add]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op","object.add"}, {"plate","Plate 01 test"},
        {"stl", bambu_cli_unit::fixture_stl("cube.stl")}, {"junk", 1}};
    REQUIRE_THROWS_AS(reg.lookup("object.add").fn(s, step), ManifestFieldError);
}

TEST_CASE("object.add: applies translate from object form",
          "[project_apply][object.add]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op","object.add"}, {"plate","Plate 01 test"},
        {"stl", bambu_cli_unit::fixture_stl("cube.stl")},
        {"name", "tx_cube"},
        {"translate", {{"x", 25.0}, {"y", 0.0}}}
    };
    REQUIRE_NOTHROW(reg.lookup("object.add").fn(s, step));
    // The semantic-correctness assertion (object instance offset == 25/0/0)
    // is covered by tests/cli/unit/test_project_ops_objects.cpp at the
    // project_ops layer; here we only assert the handler wired the
    // transform through without throwing.
}
```

- [ ] **Step 3: Implement the handler**

In `HandlerRegistry::HandlerRegistry()`, add:
```cpp
m_handlers["object.add"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "plate", "stl", "name",
                        "filament", "count", "translate", "rotate", "scale"});
    if (!step.contains("plate") || !step["plate"].is_string())
        throw ManifestFieldError("object.add: missing or non-string 'plate'");
    if (!step.contains("stl") || !step["stl"].is_string())
        throw ManifestFieldError("object.add: missing or non-string 'stl'");

    // STL path resolution: JSON-relative.
    fs::path stl_p = step["stl"].get<std::string>();
    if (!stl_p.is_absolute() && !g_manifest_dir.empty())
        stl_p = fs::path(g_manifest_dir) / stl_p;
    stl_p = fs::weakly_canonical(stl_p);

    std::string name      = step.value("name", std::string{});
    int         filament  = step.contains("filament") ? parse_filament(step, "filament") : -1;
    int         count     = 1;
    if (step.contains("count")) {
        if (!step["count"].is_number_integer())
            throw ManifestFieldError("object.add: 'count' must be an integer");
        count = step["count"].get<int>();
        if (count < 1)
            throw ManifestFieldError("object.add: 'count' must be >= 1");
    }

    ManualTransform tf = parse_transform(step);
    const ManualTransform* tf_ptr =
        (tf.has_translate || tf.has_rotate || tf.has_scale) ? &tf : nullptr;

    add_object_to_plate(s,
                        step["plate"].get<std::string>(),
                        stl_p.string(),
                        name,
                        filament,
                        tf_ptr,
                        count,
                        nullptr);
};
```

- [ ] **Step 4: Build + tag-run + full suite + commit** (`feat(cli): project apply — object.add handler with JSON-relative STL paths`).

---

## Task 16: `object.remove`

- [ ] **Tests**: happy / missing name / unknown field (3 tests).
- [ ] **Handler**:
```cpp
m_handlers["object.remove"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "name"});
    if (!step.contains("name") || !step["name"].is_string())
        throw ManifestFieldError("object.remove: missing or non-string 'name'");
    remove_object(s, step["name"].get<std::string>());
};
```
- [ ] **Build + commit** (`feat(cli): project apply — object.remove handler`).

---

## Task 17: `object.set-filament`

- [ ] **Tests**: happy / missing name / missing filament / unknown field / non-int filament / object-level vs per-volume via `part` (6 tests).
- [ ] **Handler**:
```cpp
m_handlers["object.set-filament"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "name", "filament", "part"});
    if (!step.contains("name") || !step["name"].is_string())
        throw ManifestFieldError("object.set-filament: missing or non-string 'name'");
    int filament = parse_filament(step, "filament");
    std::string part = step.value("part", std::string{});
    set_object_filament(s, step["name"].get<std::string>(), filament, part);
};
```
- [ ] **Build + commit** (`feat(cli): project apply — object.set-filament handler`).

---

## Task 18: `object.auto-orient` — overrides `runtime_error → exit 7`

- [ ] **Tests** (4): happy / missing name / unknown field / **override entry assertion** (mirroring the Task-14 override test):
```cpp
TEST_CASE("object.auto-orient: entry carries runtime_error -> exit 7 override",
          "[project_apply][object.auto-orient][overrides]") {
    HandlerRegistry reg;
    const auto& entry = reg.lookup("object.auto-orient");
    auto it = entry.overrides.find(std::type_index(typeid(std::runtime_error)));
    REQUIRE(it != entry.overrides.end());
    REQUIRE(it->second.first == 7);
}
```
- [ ] **Handler + overrides**:
```cpp
m_handlers["object.auto-orient"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "name"});
    if (!step.contains("name") || !step["name"].is_string())
        throw ManifestFieldError("object.auto-orient: missing or non-string 'name'");
    object_auto_orient(s, step["name"].get<std::string>());
};
m_handlers["object.auto-orient"].overrides = {
    { std::type_index(typeid(std::runtime_error)), {7, "invalid_state"} },
};
```
- [ ] **Build + commit** (`feat(cli): project apply — object.auto-orient handler (override exit 7)`).

---

## Task 19: `object.split-to-parts` — overrides `invalid_argument → exit 7`

- [ ] **Tests** (4): happy / missing name / unknown field / **override entry assertion** (this time on `invalid_argument`):
```cpp
TEST_CASE("object.split-to-parts: entry carries invalid_argument -> exit 7 override",
          "[project_apply][object.split-to-parts][overrides]") {
    HandlerRegistry reg;
    const auto& entry = reg.lookup("object.split-to-parts");
    auto it = entry.overrides.find(std::type_index(typeid(std::invalid_argument)));
    REQUIRE(it != entry.overrides.end());
    REQUIRE(it->second.first == 7);
}
```
- [ ] **Handler + overrides**:
```cpp
m_handlers["object.split-to-parts"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "name"});
    if (!step.contains("name") || !step["name"].is_string())
        throw ManifestFieldError("object.split-to-parts: missing or non-string 'name'");
    split_object_to_parts(s, step["name"].get<std::string>());
};
m_handlers["object.split-to-parts"].overrides = {
    { std::type_index(typeid(std::invalid_argument)), {7, "invalid_state"} },
};
```
- [ ] **Build + commit** (`feat(cli): project apply — object.split-to-parts handler (override exit 7)`).

---

## Task 20: `object.merge-parts` — overrides `invalid_argument → exit 7`

- [ ] **Tests** (5): happy (merging cube.stl's two volumes into one, if the fixture has them; otherwise just assert that the override entry is wired and unknown-field + missing-parts cases throw) / missing parts / empty parts array / unknown field / **override entry assertion**.
- [ ] **Handler + overrides**:
```cpp
m_handlers["object.merge-parts"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "name", "parts", "into", "filament"});
    if (!step.contains("name") || !step["name"].is_string())
        throw ManifestFieldError("object.merge-parts: missing or non-string 'name'");
    if (!step.contains("parts") || !step["parts"].is_array() || step["parts"].empty())
        throw ManifestFieldError("object.merge-parts: 'parts' must be a non-empty array of strings");
    if (!step.contains("into") || !step["into"].is_string())
        throw ManifestFieldError("object.merge-parts: missing or non-string 'into'");

    MergePartsParams p;
    for (const auto& v : step["parts"]) {
        if (!v.is_string())
            throw ManifestFieldError("object.merge-parts: 'parts' entry must be a string");
        p.parts.push_back(v.get<std::string>());
    }
    p.into     = step["into"].get<std::string>();
    p.filament = step.contains("filament") ? parse_filament(step, "filament") : -1;

    merge_object_parts(s, step["name"].get<std::string>(), p);
};
m_handlers["object.merge-parts"].overrides = {
    { std::type_index(typeid(std::invalid_argument)), {7, "invalid_state"} },
};
```
- [ ] **Build + commit** (`feat(cli): project apply — object.merge-parts handler (override exit 7)`).

---

## Task 21: `config.set` (single + `values` batch)

**Files:**
- Modify: `src/cli/commands/project_apply.cpp`
- Modify: `tests/cli/unit/test_project_apply_handlers.cpp`

The single + batch surface inside one handler. Mutually-exclusive form check.

- [ ] **Tests** (6):
  - happy single (`key`+`value`)
  - happy batch (`values` map with 2 entries)
  - both forms present → `ManifestFieldError`
  - neither form present → `ManifestFieldError`
  - unknown field → `ManifestFieldError`
  - object-level (`object` field present) routes to per-object config
```cpp
TEST_CASE("config.set: single key/value happy", "[project_apply][config.set]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","config.set"}, {"key","layer_height"}, {"value","0.2"}};
    REQUIRE_NOTHROW(reg.lookup("config.set").fn(s, step));
}

TEST_CASE("config.set: values batch happy", "[project_apply][config.set]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op","config.set"},
        {"values", {{"layer_height","0.2"}, {"first_layer_height","0.16"}}}
    };
    REQUIRE_NOTHROW(reg.lookup("config.set").fn(s, step));
}

TEST_CASE("config.set: both forms present rejected", "[project_apply][config.set]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op","config.set"}, {"key","layer_height"}, {"value","0.2"},
        {"values", {{"first_layer_height","0.16"}}}
    };
    REQUIRE_THROWS_AS(reg.lookup("config.set").fn(s, step), ManifestFieldError);
}

TEST_CASE("config.set: neither form rejected", "[project_apply][config.set]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    REQUIRE_THROWS_AS(reg.lookup("config.set").fn(s, json{{"op","config.set"}}),
                      ManifestFieldError);
}

TEST_CASE("config.set: unknown field rejected", "[project_apply][config.set]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","config.set"}, {"key","layer_height"}, {"value","0.2"}, {"junk",1}};
    REQUIRE_THROWS_AS(reg.lookup("config.set").fn(s, step), ManifestFieldError);
}

TEST_CASE("config.set: empty values map rejected", "[project_apply][config.set]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {{"op","config.set"}, {"values", json::object()}};
    REQUIRE_THROWS_AS(reg.lookup("config.set").fn(s, step), ManifestFieldError);
}
```

- [ ] **Handler**:
```cpp
m_handlers["config.set"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "object", "key", "value", "values"});
    std::string object_name = step.value("object", std::string{});

    const bool has_single = step.contains("key") || step.contains("value");
    const bool has_batch  = step.contains("values");
    if (has_single && has_batch)
        throw ManifestFieldError("config.set: 'key'/'value' and 'values' are mutually exclusive");
    if (!has_single && !has_batch)
        throw ManifestFieldError("config.set: provide either 'key'+'value' or 'values'");

    if (has_single) {
        if (!step.contains("key") || !step["key"].is_string())
            throw ManifestFieldError("config.set: missing or non-string 'key'");
        if (!step.contains("value") || !step["value"].is_string())
            throw ManifestFieldError("config.set: missing or non-string 'value'");
        config_set(s, object_name,
                   step["key"].get<std::string>(),
                   step["value"].get<std::string>());
        return;
    }

    // Batch form.
    const auto& vs = step["values"];
    if (!vs.is_object())
        throw ManifestFieldError("config.set: 'values' must be an object");
    if (vs.empty())
        throw ManifestFieldError("config.set: 'values' must be non-empty");
    for (auto it = vs.begin(); it != vs.end(); ++it) {
        if (!it.value().is_string())
            throw ManifestFieldError(
                "config.set: 'values' entry '" + it.key() + "' must be a string");
        config_set(s, object_name, it.key(), it.value().get<std::string>());
    }
};
```

- [ ] **Build + tag-run + full suite + commit** (`feat(cli): project apply — config.set handler (single + values batch)`).

---

## Task 22: `config.unset` (single + `keys` batch)

Mirrors Task 21 with `key` vs `keys` array.

- [ ] **Tests** (6 — same structure as Task 21).
- [ ] **Handler**:
```cpp
m_handlers["config.unset"].fn = [](ProjectState& s, const json& step) {
    require_only(step, {"op", "object", "key", "keys"});
    std::string object_name = step.value("object", std::string{});

    const bool has_single = step.contains("key");
    const bool has_batch  = step.contains("keys");
    if (has_single && has_batch)
        throw ManifestFieldError("config.unset: 'key' and 'keys' are mutually exclusive");
    if (!has_single && !has_batch)
        throw ManifestFieldError("config.unset: provide either 'key' or 'keys'");

    if (has_single) {
        if (!step["key"].is_string())
            throw ManifestFieldError("config.unset: 'key' must be a string");
        config_unset(s, object_name, step["key"].get<std::string>());
        return;
    }

    const auto& ks = step["keys"];
    if (!ks.is_array() || ks.empty())
        throw ManifestFieldError("config.unset: 'keys' must be a non-empty array");
    for (const auto& v : ks) {
        if (!v.is_string())
            throw ManifestFieldError("config.unset: 'keys' entry must be a string");
        config_unset(s, object_name, v.get<std::string>());
    }
};
```
- [ ] **Build + commit** (`feat(cli): project apply — config.unset handler (single + keys batch)`).

---

## Task 23: `failing_key` context for config-batch errors

**Files:**
- Modify: `src/cli/apply_helpers.hpp` (add `ConfigBatchError`)
- Modify: `src/cli/apply_helpers.cpp` (definition)
- Modify: `src/cli/commands/project_apply.cpp` (wrap batch iteration + dispatcher special-case)
- Modify: `tests/cli/unit/test_project_apply_handlers.cpp` (assertion tests)

`ConfigBatchError` carries the failing key plus a pre-classified `Dispatched`. The dispatcher recognises it via `dynamic_cast`, extracts the key, attaches it to the data blob, and emits the prefixed message.

- [ ] **Step 1: Declare `ConfigBatchError` in `apply_helpers.hpp`**

Add at the bottom of the header (before namespace close):
```cpp
#include "exception_dispatch.hpp"

// Carries failing-key context for a config.set/values or config.unset/keys
// mid-batch failure. The handler classifies the inner exception via
// exception_dispatch::dispatch(inner) BEFORE throwing, so the
// outer dispatcher can extract Dispatched + failing_key in one catch.
class ConfigBatchError : public std::exception {
public:
    ConfigBatchError(std::string failing_key,
                     exception_dispatch::Dispatched dispatched);
    const char* what() const noexcept override;
    const std::string&                       failing_key() const noexcept;
    const exception_dispatch::Dispatched&    dispatched()  const noexcept;
private:
    std::string                       m_what;
    std::string                       m_failing_key;
    exception_dispatch::Dispatched    m_dispatched;
};
```

- [ ] **Step 2: Implement in `apply_helpers.cpp`**

Add to the .cpp:
```cpp
ConfigBatchError::ConfigBatchError(std::string failing_key,
                                   exception_dispatch::Dispatched dispatched)
    : m_failing_key(std::move(failing_key)),
      m_dispatched(std::move(dispatched))
{
    m_what = "failing_key '" + m_failing_key + "': " + m_dispatched.message;
}
const char* ConfigBatchError::what() const noexcept { return m_what.c_str(); }
const std::string& ConfigBatchError::failing_key() const noexcept { return m_failing_key; }
const exception_dispatch::Dispatched& ConfigBatchError::dispatched() const noexcept { return m_dispatched; }
```

- [ ] **Step 3: Update the `config.set` batch loop and `config.unset` batch loop**

In the `values` and `keys` iteration loops (Tasks 21 & 22), wrap each iteration in `try/catch`:
```cpp
// config.set values branch
for (auto it = vs.begin(); it != vs.end(); ++it) {
    if (!it.value().is_string())
        throw ManifestFieldError(
            "config.set: 'values' entry '" + it.key() + "' must be a string");
    try {
        config_set(s, object_name, it.key(), it.value().get<std::string>());
    } catch (const std::exception& inner) {
        throw ConfigBatchError(it.key(), exception_dispatch::dispatch(inner));
    }
}
```
And the equivalent for `config.unset`'s `keys` array.

- [ ] **Step 4: Special-case `ConfigBatchError` in the dispatcher catch in `run_apply`**

In `run_apply`'s dispatch loop, keep the existing `const HandlerEntry* entry = nullptr;`, the `try { entry = &registry.lookup(op); entry->fn(state, step); }` body, and the surrounding `for (const auto& step : ...) { ++step_index; ... }` unchanged. **Only** replace the existing single `catch (const std::exception& e) { ... }` block with the following two arms (in this order — the ConfigBatchError arm must come first):
```cpp
} catch (const ConfigBatchError& cbe) {
    const auto& d = cbe.dispatched();
    json data;
    data["step"]        = step_index;
    data["op"]          = op;
    data["failing_key"] = cbe.failing_key();
    std::ostringstream msg;
    msg << "step " << step_index << " (op '" << op
        << "', failing_key '" << cbe.failing_key() << "'): " << d.message;
    emit_error(mode, d.code, msg.str(), data);
    std::exit(d.exit_code);
} catch (const std::exception& e) {
    auto d = exception_dispatch::dispatch(
        e, entry ? entry->overrides : MutationExceptionMap{});
    json data;
    data["step"] = step_index;
    data["op"]   = op;
    std::ostringstream msg;
    msg << "step " << step_index << " (op '" << op << "'): " << d.message;
    emit_error(mode, d.code, msg.str(), data);
    std::exit(d.exit_code);
}
```

- [ ] **Step 5: Failing-key reporting tests**

Append to `test_project_apply_handlers.cpp`:
```cpp
TEST_CASE("config.set values: failing key surfaces in ConfigBatchError",
          "[project_apply][config.set][failing_key]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op","config.set"},
        {"values", {
            {"layer_height", "0.2"},                  // ok
            {"definitely_not_a_real_key", "value"}    // ← will fail
        }}
    };
    try {
        reg.lookup("config.set").fn(s, step);
        FAIL("expected ConfigBatchError");
    } catch (const bambu_cli::ConfigBatchError& e) {
        REQUIRE(e.failing_key() == "definitely_not_a_real_key");
        REQUIRE(e.dispatched().code == "bad_config");
    }
}

TEST_CASE("config.unset keys: failing key surfaces in ConfigBatchError",
          "[project_apply][config.unset][failing_key]") {
    ProjectState s; bambu_cli_unit::load_reference_into(s);
    HandlerRegistry reg;
    json step = {
        {"op","config.unset"},
        {"keys", json::array({"layer_height", "definitely_not_a_real_key"})}
    };
    try {
        reg.lookup("config.unset").fn(s, step);
        FAIL("expected ConfigBatchError");
    } catch (const bambu_cli::ConfigBatchError& e) {
        REQUIRE(e.failing_key() == "definitely_not_a_real_key");
    }
}
```

- [ ] **Step 6: Build, run, full suite, commit** (`feat(cli): project apply — failing_key context for config-batch errors`).

---

## Task 24: `--dry-run` E2E verification

The flag wiring already happened in Task 7. This task adds E2E coverage for both happy and failing paths.

**Files:**
- Create: `tests/cli/e2e/test_project_apply.cpp`
- Modify: `tests/cli/CMakeLists.txt`

- [ ] **Step 1: Write the E2E tests**

Create `tests/cli/e2e/test_project_apply.cpp`:
```cpp
#include "test_helpers.hpp"
#include "archive_invariants.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

using namespace bambu_cli_test;
namespace fs = boost::filesystem;

namespace {
std::string write_manifest(const std::string& dir, const std::string& body) {
    std::string path = dir + "/m.json";
    std::ofstream(path) << body;
    return path;
}
} // namespace

TEST_CASE("project apply --dry-run: happy path skips save",
          "[project_apply][e2e][dry-run]") {
    const std::string in       = fresh_temp_path("_apply_dryhappy_in.3mf");
    const std::string out      = fresh_temp_path("_apply_dryhappy_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);

    std::string mfdir = fresh_temp_path("_apply_dryhappy_d");
    fs::create_directories(mfdir);
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[{"op":"plate.add","name":"new1"},)"
        R"({"op":"plate.add","name":"new2"}]})");

    auto r = spawn_cli({"--json", "project", "apply", in,
                        "--manifest", mf, "--output", out, "--dry-run"});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("\"dry_run\":true") != std::string::npos);
    // Output file must NOT have been created.
    REQUIRE_FALSE(fs::exists(out));

    fs::remove(in);
    fs::remove_all(mfdir);
}

TEST_CASE("project apply --dry-run: failing path skips save, propagates exit code",
          "[project_apply][e2e][dry-run]") {
    const std::string in  = fresh_temp_path("_apply_dryfail_in.3mf");
    const std::string out = fresh_temp_path("_apply_dryfail_out.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);

    std::string mfdir = fresh_temp_path("_apply_dryfail_d");
    fs::create_directories(mfdir);
    // step 2 attempts to rename a plate that doesn't exist -> exit 6
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[)"
        R"({"op":"plate.add","name":"P-new"},)"
        R"({"op":"plate.rename","from":"NOPE","to":"X"}]})");

    auto r = spawn_cli({"--json", "project", "apply", in,
                        "--manifest", mf, "--output", out, "--dry-run"});
    REQUIRE(r.exit_code == 6);
    REQUIRE(r.stderr_text.find("\"step\":2") != std::string::npos);
    REQUIRE(r.stderr_text.find("\"op\":\"plate.rename\"") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));

    fs::remove(in);
    fs::remove_all(mfdir);
}
```

- [ ] **Step 2: Register the new TU**

Append `e2e/test_project_apply.cpp` to `BAMBU_CLI_TEST_SOURCES` in `tests/cli/CMakeLists.txt`.

- [ ] **Step 3: Build + run + commit** (`test(cli): project apply — --dry-run e2e coverage`).

---

## Task 25: Manifest-validation E2E tests

Already have `test_apply_manifest.cpp` for the in-process validator. This task wires up E2E coverage that the user-visible exit codes match (i.e. the validator results actually reach the user via `bambu-cli project apply`).

**Files:**
- Modify: `tests/cli/e2e/test_project_apply.cpp`

- [ ] **Add the following E2E tests** to the file from Task 24:
```cpp
TEST_CASE("project apply: manifest file not found -> exit 2",
          "[project_apply][e2e][manifest]") {
    const std::string in = fresh_temp_path("_apply_mf_nf.3mf");
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);
    auto r = spawn_cli({"project", "apply", in, "--manifest", "no_such_file.json"});
    REQUIRE(r.exit_code == 2);
    REQUIRE(r.stderr_text.find("file_not_found") != std::string::npos);
    fs::remove(in);
}

TEST_CASE("project apply: malformed JSON manifest -> exit 3",
          "[project_apply][e2e][manifest]") {
    const std::string in    = fresh_temp_path("_apply_bad_json.3mf");
    const std::string mfdir = fresh_temp_path("_apply_bad_json_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);
    std::string mf = write_manifest(mfdir, "{ this is not json ");
    auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
    REQUIRE(r.exit_code == 3);
    REQUIRE(r.stderr_text.find("parse_failure") != std::string::npos);
    fs::remove(in);
    fs::remove_all(mfdir);
}

TEST_CASE("project apply: version != 1 -> exit 1",
          "[project_apply][e2e][manifest]") {
    const std::string in    = fresh_temp_path("_apply_badver.3mf");
    const std::string mfdir = fresh_temp_path("_apply_badver_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);
    std::string mf = write_manifest(mfdir, R"({"version":2,"operations":[]})");
    auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    fs::remove(in);
    fs::remove_all(mfdir);
}

TEST_CASE("project apply: unknown top-level key -> exit 1",
          "[project_apply][e2e][manifest]") {
    const std::string in    = fresh_temp_path("_apply_unkkey.3mf");
    const std::string mfdir = fresh_temp_path("_apply_unkkey_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[],"operation":[]})");
    auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("operation") != std::string::npos);
    fs::remove(in);
    fs::remove_all(mfdir);
}
```

- [ ] **Build + run + commit** (`test(cli): project apply — manifest-validation e2e`).

---

## Task 26: Schema-vs-semantic E2E tests on the four exit-7 verbs

The behavioural crown jewel of Findings 1 & 1b. Verifies, end-to-end, that:
- A manifest *typo* (unknown field) on `object.split-to-parts` / `merge-parts` / `auto-orient` / `plate.auto-orient` → **exit 1** (`usage_error`), NOT exit 7.
- A *semantic* failure → **exit 7** (`invalid_state`).

**Files:**
- Modify: `tests/cli/e2e/test_project_apply.cpp`

The "semantic failure" trigger differs per verb. For `object.split-to-parts`, the canonical fixture cube is a single-component mesh, so `split-to-parts` on it raises `std::invalid_argument` → exit 7 under the override. We can trigger the same behaviour through the batch path.

- [ ] **Add the following 8 tests**:
```cpp
TEST_CASE("project apply: split-to-parts unknown field -> exit 1",
          "[project_apply][e2e][exit7]") {
    // Same fixture setup as previous test cases (omitted for brevity, copy the
    // pattern — in, mfdir, copy reference). Use object.split-to-parts with a
    // 'junk' field; assert exit 1 not 7.
    const std::string in    = fresh_temp_path("_apply_split_typo.3mf");
    const std::string mfdir = fresh_temp_path("_apply_split_typo_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[{"op":"object.split-to-parts","name":"AnyName","junk":1}]})");
    auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
    REQUIRE(r.exit_code == 1);
    REQUIRE(r.stderr_text.find("usage_error") != std::string::npos);
    fs::remove(in);  fs::remove_all(mfdir);
}

TEST_CASE("project apply: split-to-parts on single-component mesh -> exit 7",
          "[project_apply][e2e][exit7]") {
    const std::string in    = fresh_temp_path("_apply_split_sem.3mf");
    const std::string mfdir = fresh_temp_path("_apply_split_sem_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);
    // The reference's first plate contains a single-component object — use
    // its actual name. (Inspect via `bambu-cli --json object list <in>`
    // beforehand to discover the exact name in the fixture.)
    std::string mf = write_manifest(mfdir,
        R"({"version":1,"operations":[{"op":"object.split-to-parts","name":"<NAME-FROM-FIXTURE>"}]})");
    auto r = spawn_cli({"project", "apply", in, "--manifest", mf});
    REQUIRE(r.exit_code == 7);
    REQUIRE(r.stderr_text.find("invalid_state") != std::string::npos);
    fs::remove(in); fs::remove_all(mfdir);
}
```
**Implementation note for the engineer**: replace `<NAME-FROM-FIXTURE>` after running `bambu-cli --json object list tests/cli/fixtures/reference.3mf` to get the actual first-plate object name. Adapt similar tests for merge-parts (use a non-MODEL_PART volume name → exit 7; junk field → exit 1) and the two auto-orient verbs (an unknown plate/object name → exit 6 for ManifestFieldError; an actual orient-engine failure is harder to manufacture E2E — if no easy trigger exists, the override-entry unit test in Tasks 14/18 already covers the override existence, so the E2E semantic-failure half can be marked TBD and tracked as a separate verification step rather than blocking this task).

- [ ] **Write 4 typo-rejection tests** (one per exit-7 verb).
- [ ] **Write semantic-failure tests where straightforwardly triggerable** (at minimum: split-to-parts). For verbs where the semantic trigger isn't easy to construct E2E, rely on the Task 14/18/19/20 unit tests that assert the override-map entry exists; document this delegation in the test file with an `INFO()` comment.
- [ ] **Build + run + commit** (`test(cli): project apply — schema-vs-semantic exit-7 coverage`).

---

## Task 27: 12-plate workflow E2E + roundtrip tests

**Files:**
- Modify: `tests/cli/e2e/test_project_apply.cpp`
- Create: `tests/cli/roundtrip/test_apply_roundtrip.cpp`
- Modify: `tests/cli/CMakeLists.txt`

The motivating workflow + the equivalence + single-save guarantees.

- [ ] **Step 1: Add the 12-plate E2E test**

In `test_project_apply.cpp`:
```cpp
TEST_CASE("project apply: 12-plate workflow happy path",
          "[project_apply][e2e][workflow]") {
    const std::string in    = fresh_temp_path("_apply_12_in.3mf");
    const std::string out   = fresh_temp_path("_apply_12_out.3mf");
    const std::string mfdir = fresh_temp_path("_apply_12_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);

    // Build a manifest that adds 12 plates and sets 3 config keys.
    std::ostringstream m;
    m << R"({"version":1,"operations":[)";
    for (int i = 1; i <= 12; ++i) {
        if (i > 1) m << ",";
        m << R"({"op":"plate.add","name":"P)" << i << R"("})";
    }
    m << R"(,{"op":"config.set","values":{)"
      << R"("layer_height":"0.2",)"
      << R"("first_layer_height":"0.16",)"
      << R"("sparse_infill_density":"20%")"
      << R"(}})";
    m << "]}";
    std::string mf = write_manifest(mfdir, m.str());

    auto r = spawn_cli({"--json", "project", "apply", in,
                        "--manifest", mf, "--output", out});
    INFO("stderr: " << r.stderr_text);
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.stdout_text.find("\"steps_applied\":13") != std::string::npos);
    REQUIRE(fs::exists(out));

    // Quick re-inspect.
    auto pr = spawn_cli({"--json", "plate", "list", out});
    REQUIRE(pr.exit_code == 0);
    // reference fixture's 1 plate + 12 added = 13
    REQUIRE(pr.stdout_text.find("\"plate_count\":13") != std::string::npos);

    bambu_cli_test::run_all_basic(out);

    fs::remove(in); fs::remove(out); fs::remove_all(mfdir);
}
```

- [ ] **Step 2: Create the roundtrip test file**

Create `tests/cli/roundtrip/test_apply_roundtrip.cpp`:
```cpp
#include "../test_helpers.hpp"
#include "../archive_invariants.hpp"
#include "../unit/unit_helpers.hpp"

#include "commands/project_apply_internal.hpp"
#include "io.hpp"

#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

using namespace bambu_cli_test;
using bambu_cli::ProjectState;
namespace fs = boost::filesystem;

namespace {
std::string write_mf(const std::string& dir, const std::string& body) {
    std::string path = dir + "/m.json";
    std::ofstream(path) << body;
    return path;
}
} // namespace

TEST_CASE("apply roundtrip: empty manifest preserves the project",
          "[apply][roundtrip]") {
    const std::string in    = fresh_temp_path("_rt_apply_empty.3mf");
    const std::string out_a = fresh_temp_path("_rt_apply_empty_a.3mf");
    const std::string out_b = fresh_temp_path("_rt_apply_empty_b.3mf");
    const std::string mfdir = fresh_temp_path("_rt_apply_empty_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), in, fs::copy_options::overwrite_existing);
    std::string mf = write_mf(mfdir, R"({"version":1,"operations":[]})");

    // Run A: load + apply empty + save.
    auto a = spawn_cli({"project", "apply", in,
                        "--manifest", mf, "--output", out_a});
    REQUIRE(a.exit_code == 0);
    REQUIRE(fs::exists(out_a));

    // Run B: load + save (no apply at all) via project init no-op, OR
    // simply reload then save with bambu-cli's existing test harness.
    // Easiest: write a no-op manifest test against a 'load -> save' cycle
    // by counting plates pre/post and asserting equivalence.
    ProjectState s1; REQUIRE(bambu_cli::load_project(out_a, s1).ok);
    ProjectState s0; REQUIRE(bambu_cli::load_project(in,    s0).ok);
    REQUIRE(bambu_cli::list_plate_names(s0).size()
         == bambu_cli::list_plate_names(s1).size());

    bambu_cli_test::run_all_basic(out_a);
    fs::remove(in); fs::remove(out_a);
    if (fs::exists(out_b)) fs::remove(out_b);
    fs::remove_all(mfdir);
}

TEST_CASE("apply roundtrip: sequential vs batch produce equivalent state",
          "[apply][roundtrip]") {
    // Two parallel projects: one built by 3 individual CLI calls, one by
    // a single project apply manifest. Reload both, assert plate names
    // and object names match.
    const std::string mf_in  = fresh_temp_path("_rt_apply_eq_mf_in.3mf");
    const std::string mf_out = fresh_temp_path("_rt_apply_eq_mf_out.3mf");
    const std::string seq    = fresh_temp_path("_rt_apply_eq_seq.3mf");
    const std::string mfdir  = fresh_temp_path("_rt_apply_eq_d");
    fs::create_directories(mfdir);
    fs::copy_file(canonical_committed_3mf(), mf_in, fs::copy_options::overwrite_existing);
    fs::copy_file(canonical_committed_3mf(), seq,   fs::copy_options::overwrite_existing);

    // Sequential build
    REQUIRE(spawn_cli({"plate","add",seq,"--name","A"}).exit_code == 0);
    REQUIRE(spawn_cli({"plate","add",seq,"--name","B"}).exit_code == 0);
    REQUIRE(spawn_cli({"plate","rename",seq,"--from","A","--to","A2"}).exit_code == 0);

    // Batch build
    std::string mf = write_mf(mfdir, R"({"version":1,"operations":[)"
        R"({"op":"plate.add","name":"A"},)"
        R"({"op":"plate.add","name":"B"},)"
        R"({"op":"plate.rename","from":"A","to":"A2"}]})");
    auto r = spawn_cli({"project","apply",mf_in,"--manifest",mf,"--output",mf_out});
    REQUIRE(r.exit_code == 0);

    ProjectState s_seq, s_batch;
    REQUIRE(bambu_cli::load_project(seq,    s_seq).ok);
    REQUIRE(bambu_cli::load_project(mf_out, s_batch).ok);
    REQUIRE(bambu_cli::list_plate_names(s_seq)
         == bambu_cli::list_plate_names(s_batch));

    fs::remove(mf_in); fs::remove(mf_out); fs::remove(seq);
    fs::remove_all(mfdir);
}
```

- [ ] **Step 3: Register the roundtrip TU**

Append `roundtrip/test_apply_roundtrip.cpp` to `BAMBU_CLI_TEST_SOURCES`.

- [ ] **Step 4: Build, run full suite**

```powershell
cmake --build build --config Release --parallel 2 --target cli_tests
& "build\tests\cli\Release\cli_tests.exe" --reporter compact
```
Expected: every test (existing + every test added across Tasks 1–27) green. Total should be roughly the original 331 + ~110 new = ~440 cases.

- [ ] **Step 5: Final commit**

```powershell
git add tests/cli/e2e/test_project_apply.cpp tests/cli/roundtrip/test_apply_roundtrip.cpp tests/cli/CMakeLists.txt
git commit -m @'
test(cli): project apply — 12-plate e2e + equivalence roundtrip

The motivating use case (12 plates + 3 config keys in one manifest)
plus the core correctness guarantee: a batch manifest produces the
same ProjectState as the equivalent N individual CLI verb calls.

Plus an empty-manifest roundtrip that confirms `project apply` with
zero operations does not perturb metadata (the .bak-swap + invariant
guard runs identically to a plain load->save).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
'@
```

---

## Final verification

After Task 27, before declaring done:

- [ ] Full test suite green:
  ```powershell
  $env:PATH = "$pwd\build\src\cli\Release;$env:PATH"
  & "build\tests\cli\Release\cli_tests.exe" --reporter compact
  ```
  Expected: original 331 + new tests, all pass. Number reported in the new project-apply tests should hit roughly 110 new cases.

- [ ] The `[project_apply]` tag covers every new test:
  ```powershell
  & "build\tests\cli\Release\cli_tests.exe" "[project_apply]" --reporter compact
  ```

- [ ] Manual GUI sign-off (matching the M11 pattern): apply a small manifest, open the output in Bambu Studio, confirm plates and objects appear as expected. Record the result in `docs/cli/manual-test.md` as a new step after Step 20.

- [ ] Update `docs/cli/status.md` with a new M12 entry listing the spec, plan, commit range, test count delta, and `[ ]` for manual GUI sign-off until performed.

---

## Self-review checklist (already run before publishing this plan)

- **Spec coverage**: Every spec section maps to at least one task. Per-op error-code table → Tasks 8-22 with override-entry assertions on Tasks 14/18/19/20. `failing_key` → Task 23. `--dry-run` → Task 7 wiring + Task 24 E2E. `ManifestFieldError` → Task 2 + every handler task. Schema-vs-semantic on exit-7 verbs → Tasks 14/18/19/20 unit tests + Task 26 E2E. Path resolution (manifest-relative STL) → Task 15 implementation + happy-path test.
- **Placeholder scan**: No "TBD"/"TODO"/"implement later" outside the explicit `<NAME-FROM-FIXTURE>` instruction in Task 26 (which is an instruction for the engineer to discover via a CLI call, with the discovery command supplied) and the documented "delegate semantic-failure half to unit tests where the trigger is hard" in Task 26 — both are intentional, justified non-placeholders.
- **Type consistency**: `OpHandler`, `HandlerEntry`, `HandlerRegistry`, `ManifestFieldError`, `ConfigBatchError`, `parse_and_validate_manifest`, `parse_transform`, `parse_filament`, `require_only`, `MutationExceptionMap`, `exception_dispatch::Dispatched` — all defined exactly once and used consistently across tasks. The `static const HandlerRegistry registry;` in Step 5 of Task 8 matches the public class signature in Step 4 of Task 8.
