#include "control_flow_analysis.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace gpusim {

namespace {

struct BitSet final {
  std::size_t n = 0;
  std::vector<std::uint64_t> w;

  static BitSet all(std::size_t n_) {
    BitSet b;
    b.n = n_;
    b.w.assign((n_ + 63) / 64, 0xFFFF'FFFF'FFFF'FFFFull);
    if (!b.w.empty() && (n_ % 64) != 0) {
      const std::uint64_t mask = (1ull << (n_ % 64)) - 1ull;
      b.w.back() &= mask;
    }
    return b;
  }

  static BitSet singleton(std::size_t n_, std::size_t idx) {
    BitSet b;
    b.n = n_;
    b.w.assign((n_ + 63) / 64, 0);
    b.set(idx);
    return b;
  }

  bool test(std::size_t idx) const {
    const std::size_t wi = idx / 64;
    const std::size_t bi = idx % 64;
    return (w[wi] & (1ull << bi)) != 0;
  }

  void set(std::size_t idx) {
    const std::size_t wi = idx / 64;
    const std::size_t bi = idx % 64;
    w[wi] |= (1ull << bi);
  }

  void reset(std::size_t idx) {
    const std::size_t wi = idx / 64;
    const std::size_t bi = idx % 64;
    w[wi] &= ~(1ull << bi);
  }

  bool equals(const BitSet& o) const { return n == o.n && w == o.w; }

  void iand(const BitSet& o) {
    for (std::size_t i = 0; i < w.size(); i++) w[i] &= o.w[i];
  }

  void ior(const BitSet& o) {
    for (std::size_t i = 0; i < w.size(); i++) w[i] |= o.w[i];
  }
};

struct InstCtrlInfo final {
  bool has_bra = false;
  bool has_ret = false;
  bool is_predicated = false;
  std::optional<PC> bra_target;
};

static std::optional<InstCtrlInfo> classify_inst(const InstRecord& inst,
                                                 const DescriptorRegistry& registry,
                                                 const Expander& expander,
                                                 std::uint32_t warp_size) {
  auto desc = registry.lookup(inst);
  if (!desc) return std::nullopt;

  InstCtrlInfo info;
  info.is_predicated = inst.pred.has_value();

  const auto uops = expander.expand(inst, *desc, warp_size);
  for (const auto& u : uops) {
    if (u.kind != MicroOpKind::Control) continue;
    if (u.op == MicroOpOp::Bra) {
      info.has_bra = true;
      if (u.inputs.size() == 1 && u.inputs[0].kind == OperandKind::Imm) {
        info.bra_target = static_cast<PC>(u.inputs[0].imm_i64);
      }
    } else if (u.op == MicroOpOp::Ret) {
      info.has_ret = true;
    }
  }

  return info;
}

} // namespace

ControlFlowAnalysisResult analyze_control_flow(const KernelImage& kernel,
                                              const DescriptorRegistry& registry,
                                              const Expander& expander,
                                              std::uint32_t warp_size) {
  ControlFlowAnalysisResult out;

  const std::size_t n_insts = kernel.insts.size();
  const std::size_t exit_node = n_insts;
  const std::size_t n_nodes = n_insts + 1;
  out.exit_pc = static_cast<PC>(n_insts);
  out.reconv_pc.assign(n_insts, std::nullopt);

  std::vector<std::vector<std::size_t>> succ(n_nodes);

  // Build CFG successors.
  for (std::size_t pc = 0; pc < n_insts; pc++) {
    const auto info_opt = classify_inst(kernel.insts[pc], registry, expander, warp_size);

    const auto add_edge = [&](std::size_t from, std::size_t to) {
      succ[from].push_back(to);
    };

    if (!info_opt.has_value()) {
      // If we cannot classify (missing descriptor), fall back to linear flow.
      const std::size_t ft = (pc + 1 < n_insts) ? (pc + 1) : exit_node;
      add_edge(pc, ft);
      continue;
    }

    const auto& info = *info_opt;
    if (info.has_ret) {
      add_edge(pc, exit_node);
      continue;
    }

    if (info.has_bra) {
      if (info.is_predicated) {
        // For predicated BRA (potential divergence), we must be able to reason about both successors.
        if (!info.bra_target.has_value()) {
          out.diag = Diagnostic{ "simt", "E_RECONV_INVALID", "BRA target is missing/invalid", kernel.insts[pc].dbg, kernel.name, static_cast<std::int64_t>(pc) };
          return out;
        }
        if (*info.bra_target < 0 || static_cast<std::size_t>(*info.bra_target) >= n_insts) {
          out.diag = Diagnostic{ "simt", "E_RECONV_INVALID", "BRA target PC out of range", kernel.insts[pc].dbg, kernel.name, static_cast<std::int64_t>(pc) };
          return out;
        }
        add_edge(pc, static_cast<std::size_t>(*info.bra_target));
        const std::size_t ft = (pc + 1 < n_insts) ? (pc + 1) : exit_node;
        add_edge(pc, ft);
      } else {
        // For unconditional BRA, keep analysis permissive: invalid targets should be surfaced
        // by the runtime as E_PC_OOB (or other runtime diagnostics).
        if (!info.bra_target.has_value()) {
          const std::size_t ft = (pc + 1 < n_insts) ? (pc + 1) : exit_node;
          add_edge(pc, ft);
        } else {
          const bool in_range = (*info.bra_target >= 0) && (static_cast<std::size_t>(*info.bra_target) < n_insts);
          add_edge(pc, in_range ? static_cast<std::size_t>(*info.bra_target) : exit_node);
        }
      }
      continue;
    }

    const std::size_t ft = (pc + 1 < n_insts) ? (pc + 1) : exit_node;
    add_edge(pc, ft);
  }

  // EXIT has no successors.

  // Compute post-dominators.
  std::vector<BitSet> postdom;
  postdom.reserve(n_nodes);
  for (std::size_t i = 0; i < n_nodes; i++) {
    if (i == exit_node) postdom.push_back(BitSet::singleton(n_nodes, exit_node));
    else postdom.push_back(BitSet::all(n_nodes));
  }

  bool changed = true;
  std::size_t iters = 0;
  while (changed) {
    changed = false;
    iters++;
    if (iters > n_nodes * 4 + 64) {
      out.diag = Diagnostic{ "simt", "E_RECONV_INVALID", "postdom did not converge", std::nullopt, kernel.name, std::nullopt };
      return out;
    }

    for (std::size_t i = 0; i < n_nodes; i++) {
      if (i == exit_node) continue;

      // new_set = {i} U (intersection of postdom[succ])
      BitSet new_set = BitSet::all(n_nodes);
      if (succ[i].empty()) {
        new_set = postdom[exit_node];
      } else {
        for (std::size_t k = 0; k < succ[i].size(); k++) {
          if (k == 0) new_set = postdom[succ[i][k]];
          else new_set.iand(postdom[succ[i][k]]);
        }
      }
      new_set.set(i);

      if (!new_set.equals(postdom[i])) {
        postdom[i] = std::move(new_set);
        changed = true;
      }
    }
  }

  // Compute immediate post-dominator for each predicated BRA.
  for (std::size_t pc = 0; pc < n_insts; pc++) {
    const auto info_opt = classify_inst(kernel.insts[pc], registry, expander, warp_size);
    if (!info_opt.has_value()) continue;
    const auto& info = *info_opt;
    if (!info.has_bra || !info.is_predicated) continue;

    // candidates = postdom[pc] \ {pc}
    bool found = false;
    std::size_t ipdom = exit_node;

    for (std::size_t c = 0; c < n_nodes; c++) {
      if (c == pc) continue;
      if (!postdom[pc].test(c)) continue;

      // c is immediate if it does NOT postdominate any other candidate.
      bool immediate = true;
      for (std::size_t d = 0; d < n_nodes; d++) {
        if (d == pc || d == c) continue;
        if (!postdom[pc].test(d)) continue;
        if (postdom[d].test(c)) {
          immediate = false;
          break;
        }
      }

      if (immediate) {
        ipdom = c;
        found = true;
        break;
      }
    }

    if (!found) {
      out.diag = Diagnostic{ "simt", "E_RECONV_MISS", "missing ipdom for predicated BRA", kernel.insts[pc].dbg, kernel.name, static_cast<std::int64_t>(pc) };
      return out;
    }

    out.reconv_pc[pc] = (ipdom == exit_node) ? out.exit_pc : static_cast<PC>(ipdom);
  }

  return out;
}

} // namespace gpusim
