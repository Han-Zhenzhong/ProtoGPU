## Plan: Multi-PTX Shim Override Design

Design an additive extension to the existing GPUSIM_CUDART_SHIM_PTX_OVERRIDE flow so one fatbin/module can be backed by multiple text PTX files while preserving current single-file usage. The recommended approach is to keep the existing environment variable name, extend its value to accept a path list, centralize parsing/validation in the CUDA shim fatbin registry, and keep kernel dispatch semantics unchanged because cudaLaunchKernel already searches across FatbinModule::ptx_texts for the requested .entry. Public Runtime/CLI changes should be additive and limited to parity helpers only if needed; the primary behavior change lives in the shim, scripts, docs, and tests.

**Steps**
1. Define the external contract for multi-file override in the shim. Specify that GPUSIM_CUDART_SHIM_PTX_OVERRIDE keeps working for one path and additionally accepts multiple PTX paths in one value. Decide and document the path-list delimiter policy up front: recommended is platform-native list semantics (`:` on Linux/WSL, `;` on Windows) to match established environment-variable expectations. Also define ordering and precedence: listed files are loaded in order, exact duplicate PTX texts may still be deduplicated later by the existing registry logic, and kernel lookup remains first PTX containing the requested entry.
2. Define error-handling semantics before code changes because this affects both user experience and tests. Recommended rule: once GPUSIM_CUDART_SHIM_PTX_OVERRIDE is set, the shim should treat it as authoritative and fail fast if any listed file is unreadable, empty, or not PTX-like instead of silently falling back to fatbin extraction. This aligns better with EnvOrEmbeddedAssetsProvider fail-fast behavior and avoids ambiguous launches. This step blocks steps 3, 5, 6, and 7.
3. Refactor PTX override ingestion in /home/hanzz/ProtoGPU/cuda/src/cudart_shim/fatbin_registry.cpp. Split the current single-file branch inside FatbinRegistry::extract_ptx_texts_mvp into small helpers: parse env value into ordered paths, slurp each file, validate header shape with looks_like_ptx_header, append all valid PTX texts into the existing output vector, and surface enough context for diagnostics (env name, offending path, index in list). Keep the existing fatbin scanning path untouched when the env var is absent. Depends on 1 and 2.
4. Confirm and preserve kernel resolution semantics in /home/hanzz/ProtoGPU/cuda/src/cudart_shim/exports.cpp. The current cudaLaunchKernel path already iterates FatbinModule::ptx_texts and selects the first PTX containing the requested entry before parsing/binding. The design should explicitly keep that behavior, but improve diagnostics to mention override path count and possibly source-path order if resolution fails or duplicate entries create ambiguity. Depends on 3.
5. Decide whether additive public API parity is required outside the shim. Because the user requested runtime/CLI coverage, evaluate a minimal parity surface rather than a broad redesign: add optional helper overloads that accept multiple PTX file paths or PTX texts only if they materially help tests, demos, or non-shim usage. Do not redesign Runtime around multi-module state unless discovery shows a real need. Preserve all current single-file Runtime methods and gpu-sim-cli flags. This can run in parallel with 6 after 3, but should be scoped tightly.
6. Update script and demo contract for the new format. Adjust Linux shell helpers and demo runners so they can either pass through an already-list-valued GPUSIM_CUDART_SHIM_PTX_OVERRIDE or compose one from multiple arguments in a backward-compatible way. Update usage text and examples in the setup/demo scripts to show both single-file and multi-file forms. Depends on 1, 2, and 3.
7. Expand automated coverage in layers.
   First layer: add focused tests around env parsing and multi-file extraction behavior in shim-adjacent tests or new unit coverage for FatbinRegistry helpers.
   Second layer: add shim launch tests proving entry resolution works when kernels are distributed across multiple PTX texts, including first-match behavior and not-found diagnostics.
   Third layer: add at least one script or integration-style verification path using the demo tooling with a multi-file override value. Depends on 3 and 4; script coverage depends on 6.
8. Update user-facing documentation and migration notes. Document the extended GPUSIM_CUDART_SHIM_PTX_OVERRIDE format, delimiter rules, file-order semantics, fail-fast behavior, and example commands in the CUDA shim user docs and demo/script READMEs. Include one explicit backward-compatibility statement that a single path remains valid unchanged. Depends on 1, 2, 3, and 6.
9. Verify end-to-end with both regression and feature-specific checks. Re-run existing single-file shim/demo flows unchanged, then run new multi-file scenarios with kernels split across PTX files and with malformed path lists to validate diagnostics. Depends on 4, 6, 7, and 8.

**Relevant files**
- /home/hanzz/ProtoGPU/cuda/src/cudart_shim/fatbin_registry.cpp — primary design target; current single-file GPUSIM_CUDART_SHIM_PTX_OVERRIDE handling lives in FatbinRegistry::extract_ptx_texts_mvp.
- /home/hanzz/ProtoGPU/cuda/src/cudart_shim/fatbin_registry.h — confirms FatbinModule already stores std::vector<std::string> ptx_texts, so ingestion can expand without changing the module container shape.
- /home/hanzz/ProtoGPU/cuda/src/cudart_shim/exports.cpp — current cudaLaunchKernel already scans mod->ptx_texts and binds the requested entry from the first matching PTX text.
- /home/hanzz/ProtoGPU/cuda/src/cudart_shim/assets_provider.cpp — reference pattern for fail-fast environment-driven asset loading and helpful consistency target for override semantics.
- /home/hanzz/ProtoGPU/scripts/setup_cudart_shim_running_env.sh — current helper only accepts one override path argument; likely needs argument/usage redesign.
- /home/hanzz/ProtoGPU/scripts/run_cuda_shim_e2e_demo_integration.sh — current integration runner assumes one PTX override path and is the natural regression/feature verification entry point.
- /home/hanzz/ProtoGPU/scripts/run_cuda_shim_e2e_streaming_demo_cu.sh — another script entry point that references the override variable and should stay behaviorally aligned.
- /home/hanzz/ProtoGPU/cuda/docs/doc_user/cuda-shim.md — main user doc for the shim environment variables; must define the new multi-file contract.
- /home/hanzz/ProtoGPU/cuda/demo/README.md — demo guidance currently recommends one explicit PTX text override.
- /home/hanzz/ProtoGPU/cuda/demo/README.zh-CN.md — localized demo guidance that mirrors the one-file behavior.
- /home/hanzz/ProtoGPU/scripts/README.md — script README references the current override usage.
- /home/hanzz/ProtoGPU/scripts/README.zh-CN.md — localized script README references the current override usage.
- /home/hanzz/ProtoGPU/src/runtime/runtime.cpp — optional additive parity surface if multi-PTX helpers are introduced for public Runtime APIs.
- /home/hanzz/ProtoGPU/include/gpusim/runtime.h — optional additive public declarations if Runtime gains helper overloads for multiple PTX inputs.
- /home/hanzz/ProtoGPU/src/apps/cli/main.cpp — optional CLI parity surface only if the design chooses to expose multi-PTX input outside the shim.
- /home/hanzz/ProtoGPU/tests/cuda/cudart_shim_memory_smoke_tests.cpp — existing shim smoke infrastructure; may stay unchanged but is a nearby place to assess coverage gaps.
- /home/hanzz/ProtoGPU/tests/public_api_in_memory_tests.cpp — reference for lightweight public API tests if additive Runtime multi-text helpers are introduced.

**Verification**
1. Validate regression for current behavior: run the existing shim demo/integration flow with a single GPUSIM_CUDART_SHIM_PTX_OVERRIDE path and confirm no behavior change.
2. Add tests proving a multi-path env value loads multiple PTX texts in order and that cudaLaunchKernel can resolve an entry found only in the second or later PTX file.
3. Add tests proving duplicate or overlapping entries produce deterministic behavior and actionable diagnostics; the recommended contract is first-match by listed order unless a stricter ambiguity error is chosen during implementation.
4. Add tests proving fail-fast behavior when one listed PTX file is missing, unreadable, empty, or not valid PTX text.
5. If Runtime/CLI parity helpers are added, add focused tests showing single-file APIs remain source-compatible and multi-file helpers do not alter existing single-kernel behavior.
6. Review all docs/scripts examples to ensure the documented delimiter and quoting rules are valid for Linux shells and any supported Windows script entry points.

**Decisions**
- Included scope: CUDA shim multi-file override design, scripts, docs, tests, and narrowly scoped Runtime/CLI parity if it materially supports the feature.
- Excluded scope: redesigning fatbin decoding itself, changing embedded asset loading, or replacing current single-file public APIs.
- Compatibility decision: keep GPUSIM_CUDART_SHIM_PTX_OVERRIDE as the single compatibility surface and extend only its value format.
- Recommended semantic decision: treat an explicitly set GPUSIM_CUDART_SHIM_PTX_OVERRIDE as authoritative and fail fast on invalid inputs.
- Recommended architectural decision: keep launch-time entry resolution in cudaLaunchKernel and avoid introducing a new module graph unless implementation uncovers a real ambiguity problem.

**Further Considerations**
1. Delimiter policy: use platform-native path-list delimiters for `GPUSIM_CUDART_SHIM_PTX_OVERRIDE`, which means `:` on Linux/WSL and `;` on Windows. This matches user expectations for environment-variable path lists and fits the existing script surface best.
2. Duplicate entry names across PTX files: preserve first-match semantics by listed order. The shim should continue selecting the first PTX text containing the requested `.entry`, and documentation/tests should make that ordering rule explicit.
3. Public API parity: keep Runtime and `gpu-sim-cli` unchanged in v1. Multi-PTX support is shim-scoped for now; additive Runtime or CLI helpers should only be considered later if tests or real usage show a concrete need.
