# CUDA Runtime Shim Multi-PTX Override Dev Plan

Inputs:
- Design doc: `cuda/docs/doc_design/cuda-shim-multi-ptx-design.md`
- Logical design: `cuda/docs/doc_design/cuda-shim-logical-design.md`
- Existing shim dev plan pattern: `cuda/docs/doc_plan/plan_dev/plan-cudaShim-dev-plan.md`

Target outcome:
- The CUDA shim supports `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` as either a single PTX path or an ordered list of PTX paths.
- Single-path behavior remains unchanged.
- Launch-time kernel resolution continues to use first-match-by-order semantics across `FatbinModule::ptx_texts`.
- Shim scripts, user docs, and tests are aligned with the new contract.
- `gpusim::Runtime` and `gpu-sim-cli` remain unchanged in v1.

---

## 0) Constraints & acceptance gates

Hard requirements (must hold before calling v1 done):
- `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` remains the only PTX override env var.
- Single-path override usage remains source- and behavior-compatible.
- Multi-path override uses platform-native delimiter semantics:
  - Linux/WSL: `:`
  - Windows: `;`
- Listed PTX files are loaded in order.
- Launch resolves the first PTX containing the requested `.entry`.
- If the override env var is explicitly set and any element is invalid, the shim fails fast and does not fall back to fatbin extraction.
- Empty path-list elements are treated as errors, not skipped.
- Runtime and CLI public APIs remain unchanged in v1.

Non-goals (v1):
- No new env var.
- No fatbin format redesign.
- No ambiguity-error mode for duplicate `.entry` names.
- No Runtime multi-PTX helper APIs.
- No `gpu-sim-cli` multi-PTX flags.
- No delimiter escaping or quoted list-element parser.

---

## 1) Milestone M0 — Contract freeze + implementation prep

### Deliverables
- Finalized implementation contract derived from the design doc.
- Code-review checklist for delimiter handling, fail-fast semantics, and first-match launch behavior.
- Task breakdown aligned with shim code, scripts, docs, and tests.

### Steps
1. Confirm the implementation contract from the design doc as fixed rules:
   - platform-native delimiters
   - first-match `.entry` resolution
   - explicit override is authoritative
   - Runtime and CLI unchanged in v1
2. Identify the exact code paths that must change:
   - `cuda/src/cudart_shim/fatbin_registry.cpp`
   - `cuda/src/cudart_shim/exports.cpp`
   - shim scripts
   - shim docs
   - shim tests
3. Write down the validation matrix for:
   - single-file override
   - multi-file override
   - malformed path list
   - duplicate `.entry`
   - entry only present in later PTX file

### Validation gate
- The team has one unambiguous implementation contract and does not need to decide behavior during coding.

---

## 2) Milestone M1 — Refactor PTX override parsing in FatbinRegistry

### Deliverables
- `FatbinRegistry::extract_ptx_texts_mvp` supports parsing the override env var as a path list.
- Override parsing and file loading are refactored into small internal helpers.
- Failure details are represented structurally, not only as ad-hoc strings.

### Steps
1. Refactor the current single-file override branch in `cuda/src/cudart_shim/fatbin_registry.cpp` into helper functions for:
   - choosing the platform delimiter
   - trimming ASCII whitespace
   - splitting the env var into ordered path elements
   - slurping a PTX text file
   - validating PTX text with `looks_like_ptx_header`
2. Add a file-local structured error representation, for example:
   - `OverrideLoadError { index, path, reason }`
3. Implement path-list parsing rules:
   - preserve order
   - detect empty elements after trimming
   - treat malformed lists as hard failure
4. Preserve existing exact-text deduplication after extraction.

### Validation gate
- A valid path list returns an ordered `std::vector<std::string>` of PTX texts.
- Empty element, unreadable file, empty file, and non-PTX file all fail predictably.

---

## 3) Milestone M2 — Authoritative override semantics

### Deliverables
- Override handling becomes authoritative when the env var is explicitly set.
- Fatbin fallback remains only for the unset/empty-env case.

### Steps
1. Update `FatbinRegistry::extract_ptx_texts_mvp` behavior:
   - env var unset or empty: existing fatbin extraction path unchanged
   - env var set and valid: return override PTX texts, skip fatbin extraction
   - env var set and invalid: emit clear diagnostic, return no override PTX texts, skip fatbin extraction
2. Ensure this behavior is explicit in code comments near the override branch.
3. Confirm that null `fat_cubin` still returns empty output exactly as today.

### Validation gate
- No invalid override input silently falls back to fatbin extraction.
- Single-file override still behaves exactly as before.

---

## 4) Milestone M3 — Launch-path confirmation and diagnostics

### Deliverables
- Existing first-match resolution in `cudaLaunchKernel` remains intact.
- Launch diagnostics remain actionable for multi-PTX cases.

### Steps
1. Review `cuda/src/cudart_shim/exports.cpp` to preserve the current loop over `mod->ptx_texts`.
2. Keep the first PTX text containing the requested `.entry` as the chosen PTX.
3. Improve diagnostics when useful so multi-PTX cases remain debuggable:
   - PTX count
   - requested entry
   - best-effort discovered entries
   - optional note that override PTX came from the env var
4. Do not introduce a new module graph or dispatch layer.

### Validation gate
- Entry found only in the second or later PTX file launches correctly.
- Duplicate `.entry` names choose the first matching PTX deterministically.
- Missing `.entry` error remains actionable.

---

## 5) Milestone M4 — Script compatibility and examples

### Deliverables
- Shim helper scripts remain backward-compatible with single-file usage.
- Multi-file override usage is documented and does not break existing script flow.

### Steps
1. Update `scripts/setup_cudart_shim_running_env.sh` so it does not interfere with an already-composed `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` value.
2. Keep the existing single-path argument form unchanged.
3. Decide on the minimum acceptable script change for v1:
   - at minimum, docs explain manual multi-path env usage
   - optionally, the script may accept multiple PTX arguments and join them with the platform delimiter
4. Review `scripts/run_cuda_shim_demo_integration.sh` and `scripts/run_cuda_shim_streaming_demo_cu.sh` so they accept a valid multi-path override env var without rejecting it.

### Validation gate
- Existing single-file script flows still work unchanged.
- A user can run a multi-file override flow without script breakage.

---

## 6) Milestone M5 — Documentation alignment

### Deliverables
- CUDA shim user docs and script docs reflect the new contract precisely.
- Examples include both single-file and multi-file forms.

### Steps
1. Update:
   - `cuda/docs/doc_user/cuda-shim.md`
   - `cuda/demo/README.md`
   - `cuda/demo/README.zh-CN.md`
   - `scripts/README.md`
   - `scripts/README.zh-CN.md`
2. Document the following explicitly:
   - single-path compatibility remains unchanged
   - multi-path values are supported
   - Linux/WSL delimiter is `:`
   - Windows delimiter is `;`
   - PTX files are searched in order
   - first matching `.entry` wins
   - invalid override input fails fast
   - explicit override disables fatbin fallback
3. Add at least one multi-file example command.
4. Add one note warning that path order changes behavior when duplicate `.entry` names exist.

### Validation gate
- The docs describe the implemented behavior exactly, with no remaining “recommendation” wording.

---

## 7) Milestone M6 — Automated tests for override parsing and launch behavior

### Deliverables
- Automated coverage exists for path parsing, failure semantics, and launch resolution.
- Single-file regression is explicitly protected.

### Steps
1. Add shim-adjacent tests for override parsing behavior where practical:
   - single path
   - multiple paths
   - whitespace trimming
   - empty element failure
   - missing file failure
   - empty file failure
   - non-PTX file failure
2. Add launch-resolution tests:
   - entry only in second PTX file
   - duplicate `.entry` in two PTX files, first one wins
   - entry absent from all PTX files
3. Add at least one integration-style verification path using existing demo tooling or nearby smoke coverage.
4. Keep Runtime and CLI tests unchanged unless they are indirectly affected.

### Validation gate
- Regression and new feature behavior are both covered by automated tests.

---

## 8) Milestone M7 — End-to-end verification and cleanup

### Deliverables
- End-to-end shim behavior is verified for both old and new override forms.
- Code and docs are consistent enough for review.

### Steps
1. Re-run the existing single-file demo/shim flow.
2. Run a multi-file scenario with kernels split across PTX files.
3. Run malformed-list scenarios to confirm fail-fast behavior.
4. Review the final diagnostics for clarity and actionable detail.
5. Remove any temporary debug-only logging or helper code that was only needed during implementation.

### Validation gate
- All acceptance criteria from the design doc are satisfied.

---

## Appendix A — Task checklist

- [x] Freeze multi-PTX override implementation contract in code comments and review notes
- [x] Refactor `fatbin_registry.cpp` override path into helpers
- [x] Add structured override-load error representation
- [x] Implement platform-native path-list parsing
- [x] Reject empty path-list elements
- [x] Reject unreadable, empty, and non-PTX override files
- [x] Preserve exact-text deduplication
- [x] Preserve first-match launch resolution in `exports.cpp`
- [x] Improve multi-PTX diagnostics where useful
- [x] Keep single-file script usage unchanged
- [x] Ensure scripts tolerate a multi-path env var
- [x] Update shim user docs
- [x] Update demo READMEs
- [x] Update script READMEs
- [x] Add parsing and launch tests
- [x] Re-run single-file regression flow
- [x] Run multi-file end-to-end verification

---

## Appendix B — Acceptance checklist

- [x] Single-path override behavior unchanged
- [x] Multi-path override loads PTX texts in listed order
- [x] Later-file entry resolution works
- [x] Duplicate `.entry` uses first-match semantics
- [x] Invalid override input fails fast
- [x] No fallback to fatbin extraction when override is explicitly set
- [x] Docs and scripts match implemented behavior
- [x] No new Runtime or CLI API in v1
