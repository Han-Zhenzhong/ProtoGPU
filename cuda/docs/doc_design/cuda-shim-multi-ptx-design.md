# CUDA Runtime Shim Multi-PTX Override Design

This document describes the detailed design for extending the CUDA Runtime shim PTX override path from a single PTX text file to an ordered list of PTX text files.

It is derived from:
- Logical design: `cuda/docs/doc_design/cuda-shim-logical-design.md`
- Plan draft: `cuda/docs/doc_plan/plan_dev/plan-multiPtxTextFileHandling.prompt.md`

Scope of this doc:
- Extend `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` so the shim can load multiple PTX text files for one registered fatbin module.
- Preserve current single-file behavior unchanged.
- Keep launch-time kernel resolution in the existing CUDA shim flow.
- Update shim-adjacent scripts, docs, and tests to match the new contract.

---

## 1) Goals & non-goals

### 1.1 Goals (v1)

- Keep `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` as the only PTX override environment variable.
- Allow `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` to contain either:
	- one PTX file path, or
	- an ordered list of PTX file paths.
- Load override PTX files in listed order and store them in `FatbinModule::ptx_texts`.
- Preserve current kernel launch resolution behavior:
	- `cudaLaunchKernel` selects the first PTX text containing the requested `.entry`.
- Fail fast when the override env var is explicitly set but contains invalid input.
- Preserve the existing fallback behavior when the override env var is not set:
	- extract PTX from the fatbin using the existing MVP extraction logic.
- Keep the implementation localized to the CUDA shim path as much as possible.

### 1.2 Non-goals (v1)

- No new environment variable for multi-PTX input.
- No redesign of fatbin decoding or architecture selection.
- No change to public `gpusim::Runtime` multi-file support.
- No change to `gpu-sim-cli` flags or CLI semantics.
- No ambiguity-error mode for duplicate `.entry` names across PTX files; v1 uses ordered first-match semantics.
- No support for escaping the path-list delimiter inside a single env-var element.

---

## 2) Problem statement

Current shim behavior supports an escape hatch through `GPUSIM_CUDART_SHIM_PTX_OVERRIDE`, but only for one PTX text file.

Current implementation details:
- `FatbinRegistry::extract_ptx_texts_mvp` in `cuda/src/cudart_shim/fatbin_registry.cpp` checks `GPUSIM_CUDART_SHIM_PTX_OVERRIDE`.
- If set, it reads exactly one file, validates it with `looks_like_ptx_header`, stores it as the only PTX text for the module, and returns early.
- `cudaLaunchKernel` in `cuda/src/cudart_shim/exports.cpp` already iterates over `FatbinModule::ptx_texts` and selects the first PTX text containing the requested `.entry`.

This creates a mismatch:
- the module data structure already supports multiple PTX texts,
- the launch path already resolves across multiple PTX texts,
- but the override ingestion path only allows one file.

The feature should therefore extend input handling, not invent a new module or dispatch model.

---

## 3) External contract

### 3.1 Environment variable contract

`GPUSIM_CUDART_SHIM_PTX_OVERRIDE` remains the single PTX override surface.

Accepted forms:
- single path
- platform-native path list

Delimiter rules:
- Linux and WSL: `:`
- Windows: `;`

Examples:

Linux / WSL:

```bash
export GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PWD/a.ptx:$PWD/b.ptx:$PWD/c.ptx"
```

Windows:

```powershell
$env:GPUSIM_CUDART_SHIM_PTX_OVERRIDE = "C:\ptx\a.ptx;C:\ptx\b.ptx"
```

Backward compatibility:
- A single path remains valid unchanged.
- Existing scripts that pass one file remain valid.

### 3.2 Order and precedence

The listed PTX paths define the module PTX search order.

Rules:
- Files are read left-to-right.
- PTX texts are appended to `FatbinModule::ptx_texts` in that same order.
- Exact duplicate PTX texts may still be removed later by the existing deduplication pass in the registry.
- `cudaLaunchKernel` searches the resulting PTX texts in order and selects the first PTX text containing the requested `.entry`.

This means path order is semantically significant.

### 3.3 Override vs fatbin extraction

When `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` is set and non-empty:
- the override is authoritative,
- the shim must not fall back to fatbin extraction.

When the env var is unset or empty:
- the shim uses the existing fatbin extraction path.

Rationale:
- explicit override must be predictable,
- fallback after partial override failure would hide configuration bugs,
- this matches the fail-fast asset policy already used for `GPUSIM_CONFIG`, `GPUSIM_PTX_ISA`, and `GPUSIM_INST_DESC`.

---

## 4) Failure semantics

### 4.1 Fail-fast policy

If `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` is explicitly set, every listed file must satisfy all of the following:
- the path element is non-empty,
- the file exists and is readable,
- the file contents are non-empty,
- the contents pass `looks_like_ptx_header`.

If any element fails validation:
- the override load is treated as failed,
- no fatbin fallback is attempted,
- the module is considered to have no valid override PTX,
- the failure must be observable through launch-time diagnostics.

### 4.2 Diagnostic requirements

Diagnostics must include enough context to debug the failure quickly.

Minimum diagnostic fields:
- env var name: `GPUSIM_CUDART_SHIM_PTX_OVERRIDE`
- failing path
- failing index in the list
- failure reason:
	- empty path element
	- cannot open file
	- empty file
	- file does not look like PTX text

Recommended error message shape:

```text
GPUSIM_CUDART_SHIM_PTX_OVERRIDE invalid at index 1 path='/tmp/b.ptx': file does not look like PTX text
```

### 4.3 Handling malformed path lists

The parser should treat malformed lists conservatively.

Rules:
- consecutive delimiters produce an empty path element and therefore a hard failure,
- leading delimiter produces an empty first element and therefore a hard failure,
- trailing delimiter produces an empty last element and therefore a hard failure,
- surrounding whitespace is trimmed from each path element before validation.

Non-goal in v1:
- no quoted-element parser,
- no escape sequence support for embedding the delimiter inside a path element.

---

## 5) Detailed implementation design

### 5.1 Primary implementation area

Primary file:
- `cuda/src/cudart_shim/fatbin_registry.cpp`

Supporting files:
- `cuda/src/cudart_shim/fatbin_registry.h`
- `cuda/src/cudart_shim/exports.cpp`
- script and doc files listed later in this document

The design deliberately keeps the core behavior change inside `FatbinRegistry::extract_ptx_texts_mvp`.

### 5.2 Refactoring structure in `fatbin_registry.cpp`

Refactor the current single-file override branch into small helpers.

Recommended helper breakdown:

```text
path_list_delimiter_for_platform()
trim_ascii(string_view) -> string_view
split_override_path_list(value, delimiter) -> vector<string>
slurp_text_file(path) -> expected<string, error>
load_override_ptx_texts(value) -> expected<vector<string>, error>
```

Required behavior for each helper:

1. `path_list_delimiter_for_platform()`
- returns `':'` on Linux/WSL builds,
- returns `';'` on Windows builds.

2. `trim_ascii(...)`
- trims leading and trailing ASCII whitespace,
- does not alter internal characters.

3. `split_override_path_list(...)`
- splits by the platform delimiter,
- preserves list order,
- detects empty elements after trimming.

4. `slurp_text_file(...)`
- opens the file in binary mode,
- reads full contents into a `std::string`,
- distinguishes “cannot open” from “empty file”.

5. `load_override_ptx_texts(...)`
- parses the env var,
- reads all elements in order,
- validates each file with `looks_like_ptx_header`,
- returns all PTX texts in order on success,
- returns a structured failure on the first invalid element.

### 5.3 Registry behavior

`FatbinRegistry::extract_ptx_texts_mvp(void* fat_cubin)` should behave as follows:

1. If `fat_cubin` is null:
- return an empty vector as today.

2. If `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` is unset or empty:
- run the existing fatbin extraction path unchanged.

3. If `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` is set and non-empty:
- parse it as a path list,
- load all PTX files in order,
- on success:
	- return the loaded PTX texts,
	- skip fatbin extraction,
- on failure:
	- emit a clear diagnostic,
	- return an empty vector,
	- skip fatbin extraction.

This preserves the “override is authoritative” rule without forcing a wider architectural change.

### 5.4 Structured failure representation

Implementation should avoid building ad-hoc strings deep inside inner helpers.

Recommended internal shape:

```text
struct OverrideLoadError {
	size_t index;
	std::string path;
	std::string reason;
};
```

Rationale:
- easier testing,
- easier conversion into diagnostics,
- avoids repeating formatting logic.

This type can remain file-local to `fatbin_registry.cpp`.

### 5.5 Deduplication behavior

The current registry deduplicates exact PTX text matches after extraction.

v1 rule:
- keep exact-text deduplication unchanged.

Implications:
- duplicate paths containing identical PTX text may collapse to one entry,
- order still matters for distinct PTX texts,
- no semantic deduplication by `.entry` name is added.

---

## 6) Launch-time behavior

### 6.1 Kernel resolution remains in `cudaLaunchKernel`

`cuda/src/cudart_shim/exports.cpp` already performs the necessary resolution loop:
- obtain `KernelInfo` from the registered host function,
- obtain `FatbinModule` from the module id,
- scan `mod->ptx_texts`,
- select the first PTX text containing the requested `.entry`.

v1 keeps this design unchanged.

### 6.2 Duplicate `.entry` names across PTX files

Final v1 rule:
- duplicate entry names are allowed,
- first match by PTX list order wins.

Rationale:
- consistent with the existing loop structure,
- additive rather than disruptive,
- simple to document and test.

### 6.3 Launch diagnostics

When a requested `.entry` is not found across the loaded PTX texts, diagnostics should remain actionable.

Existing behavior already includes:
- requested entry name,
- PTX count,
- a best-effort list of discovered entry names.

Recommended additions for multi-PTX override cases:
- mention that the module PTX set came from `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` when that is the active source,
- optionally include the PTX source count in the message if not already present.

No new dispatch structure is required.

---

## 7) Scripts and tooling

### 7.1 Affected scripts

- `scripts/setup_cudart_shim_running_env.sh`
- `scripts/run_cuda_shim_e2e_demo_integration.sh`
- `scripts/run_cuda_shim_e2e_streaming_demo_cu.sh`

### 7.2 `setup_cudart_shim_running_env.sh`

Current behavior:
- accepts one optional PTX override path argument.

v1 design:
- preserve the current single-path argument form,
- allow callers to pass an already-composed env var value unchanged,
- optionally allow multiple PTX path arguments and join them with the platform delimiter when the script is extended.

Minimum acceptable v1 behavior:
- script docs must explain how to manually set a multi-path override value,
- the script must not break when `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` already contains a path list.

### 7.3 Demo runner scripts

Current demo runners assume one PTX file.

v1 design:
- keep single-file demo defaults,
- add at least one documented multi-file example,
- ensure script logic does not reject a valid multi-path override env var.

The scripts do not need to become generic PTX list builders if that adds complexity without clear value.

---

## 8) Documentation updates

### 8.1 Required doc updates

- `cuda/docs/doc_user/cuda-shim.md`
- `cuda/demo/README.md`
- `cuda/demo/README.zh-CN.md`
- `scripts/README.md`
- `scripts/README.zh-CN.md`

### 8.2 Required content updates

Each updated doc must describe:
- single-path compatibility is unchanged,
- multi-path values are supported,
- Linux/WSL delimiter is `:`,
- Windows delimiter is `;`,
- listed PTX files are searched in order,
- first matching `.entry` wins,
- invalid override input fails fast,
- fatbin fallback does not occur when the override env var is explicitly set.

### 8.3 Documentation examples

At least one example should show kernels split across multiple PTX files, for example:

```bash
export GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PWD/kernels_a.ptx:$PWD/kernels_b.ptx"
```

Documentation should also caution that path order changes resolution behavior when duplicate `.entry` names exist.

---

## 9) Testing strategy

### 9.1 Unit-level coverage

Add focused tests for helper behavior where practical.

Recommended coverage:
- single-path env value
- multi-path env value with two or more valid PTX files
- whitespace trimming around path elements
- empty element failure
- missing file failure
- empty file failure
- non-PTX file failure

If helpers remain file-local, test through shim-adjacent behavior rather than forcing public exposure.

### 9.2 Shim launch coverage

Add tests proving launch resolution across multiple PTX texts.

Required cases:
- requested `.entry` exists only in the second PTX file
- duplicate `.entry` exists in two PTX files and the first file wins
- requested `.entry` is absent from all PTX files and diagnostics remain actionable

### 9.3 Integration-style coverage

Add at least one integration-style path using the existing demo tooling.

Required checks:
- existing single-file override flow still works unchanged,
- multi-file override flow works when PTX content is split across files,
- malformed override list fails predictably.

### 9.4 Non-goal for tests

v1 does not require new Runtime or CLI tests for multi-PTX APIs because Runtime and CLI remain unchanged.

---

## 10) Affected code paths

### 10.1 Must change

- `cuda/src/cudart_shim/fatbin_registry.cpp`
- `scripts/setup_cudart_shim_running_env.sh`
- `scripts/run_cuda_shim_e2e_demo_integration.sh`
- `scripts/run_cuda_shim_e2e_streaming_demo_cu.sh`
- `cuda/docs/doc_user/cuda-shim.md`
- `cuda/demo/README.md`
- `cuda/demo/README.zh-CN.md`
- `scripts/README.md`
- `scripts/README.zh-CN.md`
- new or updated tests covering shim override behavior

### 10.2 Should remain unchanged in v1

- `include/gpusim/runtime.h`
- `src/runtime/runtime.cpp`
- `src/apps/cli/main.cpp`

These are explicitly out of scope for the initial implementation.

---

## 11) Acceptance criteria

The implementation is complete for v1 when all of the following are true:

1. `GPUSIM_CUDART_SHIM_PTX_OVERRIDE=<single-path>` behaves exactly as before.
2. `GPUSIM_CUDART_SHIM_PTX_OVERRIDE=<path1><delim><path2>` loads both PTX texts into the module in the given order.
3. `cudaLaunchKernel` can resolve an entry found only in the second or later PTX file.
4. Duplicate `.entry` names resolve deterministically to the first matching PTX in list order.
5. Any invalid override element causes a clear failure without falling back to fatbin extraction.
6. Existing single-file demo and shim flows continue to pass.
7. Updated docs and scripts accurately reflect delimiter rules, ordering semantics, and fail-fast behavior.
8. No new public Runtime or CLI API is introduced for v1.

---

## 12) Implementation notes for code review

Reviewers should explicitly confirm the following:

- The env-var parsing logic is platform-correct.
- Empty list elements are rejected, not skipped.
- Override failures do not silently fall back to fatbin extraction.
- The launch path still resolves the first matching `.entry` in PTX order.
- Script updates do not break existing single-path workflows.
- Documentation matches the implemented delimiter and ordering rules exactly.

Any implementation that leaves delimiter handling, duplicate-entry resolution, or failure semantics implicit should be considered incomplete.
