#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gpusim {

struct SourceLocation final {
  std::string file;
  std::int64_t line = 0;
  std::int64_t column = 0;
};

struct Diagnostic final {
  std::string module;
  std::string code;
  std::string message;
  std::optional<SourceLocation> location;
  std::optional<std::string> function;
  std::optional<std::int64_t> inst_index;
};

using KernelId = std::uint64_t;
using CTAId = std::uint64_t;
using WarpId = std::uint64_t;
using ThreadId = std::uint64_t;
using SmId = std::uint64_t;
using StreamId = std::uint64_t;
using CmdId = std::uint64_t;
using EventId = std::uint64_t;

using PC = std::int64_t; // InstList index

enum class AddrSpace : std::uint8_t { Global, Shared, Local, Const, Param };

enum class ValueType : std::uint8_t { U32, S32, U64, S64, F32 };

struct LaneMask final {
  std::uint32_t width = 32;
  std::uint32_t bits_lo = 0xFFFF'FFFFu; // warp_size<=32 baseline
};

struct Dim3 final {
  std::uint32_t x = 1;
  std::uint32_t y = 1;
  std::uint32_t z = 1;
};

struct LaunchConfig final {
  Dim3 grid_dim;
  Dim3 block_dim;
  std::uint32_t warp_size = 32;
};

struct Builtins final {
  Dim3 tid;
  Dim3 ntid;
  Dim3 ctaid;
  Dim3 nctaid;
  std::uint32_t laneid = 0;
  std::uint32_t warpid = 0;
};

struct Value final {
  ValueType type = ValueType::U32;
  std::vector<std::uint64_t> lanes;
};

enum class OperandKind : std::uint8_t { Reg, Pred, Imm, Addr, Symbol, Special };

struct Operand final {
  OperandKind kind = OperandKind::Imm;
  ValueType type = ValueType::U32;
  std::int64_t reg_id = -1;
  std::int64_t pred_id = -1;
  std::int64_t imm_i64 = 0;
  std::string symbol;
  std::string special;
};

struct PredGuard final {
  std::int64_t pred_id = -1;
  bool is_negated = false;
};

struct InstMods final {
  std::string type_mod;
  std::string space;
  std::vector<std::string> flags;
};

struct InstRecord final {
  std::string opcode;
  InstMods mods;
  std::optional<PredGuard> pred;
  std::vector<Operand> operands;
  std::optional<SourceLocation> dbg;
};

enum class MicroOpKind : std::uint8_t { Exec, Control, Mem };

enum class MicroOpOp : std::uint16_t {
  Mov,
  Add,
  Mul,
  Shl,
  Fma,
  Setp,
  Ld,
  St,
  Bra,
  Ret,
};

struct MicroOpAttrs final {
  ValueType type = ValueType::U32;
  AddrSpace space = AddrSpace::Global;
  std::vector<std::string> flags;
};

struct MicroOp final {
  MicroOpKind kind = MicroOpKind::Exec;
  MicroOpOp op = MicroOpOp::Mov;
  std::vector<Operand> inputs;
  std::vector<Operand> outputs;
  MicroOpAttrs attrs;
  LaneMask guard;
};

enum class BlockedReason : std::uint8_t { None, Barrier, Mem, Dependency, Error };

struct StepResult final {
  bool progressed = false;
  bool warp_done = false;
  BlockedReason blocked_reason = BlockedReason::None;
  std::optional<Diagnostic> diag;
  std::optional<PC> next_pc;
};

struct MemResult final {
  bool progressed = false;
  bool blocked = false;
  std::optional<Diagnostic> fault;
};

enum class EventCategory : std::uint8_t { Stream, Copy, Fetch, Exec, Ctrl, Mem, Commit };

struct Event final {
  std::uint64_t ts = 0;
  EventCategory category = EventCategory::Stream;
  std::string action;

  std::optional<KernelId> kernel_id;
  std::optional<CTAId> cta_id;
  std::optional<WarpId> warp_id;
  std::optional<ThreadId> thread_id;
  std::optional<SmId> sm_id;
  std::optional<StreamId> stream_id;
  std::optional<CmdId> cmd_id;
  std::optional<EventId> event_id;

  std::optional<PC> pc;
  std::optional<LaneMask> lane_mask;
  std::optional<std::string> opcode_or_uop;
  std::optional<std::uint64_t> addr;
  std::optional<std::uint64_t> size;

  std::optional<std::string> extra_json;
};

LaneMask lane_mask_all(std::uint32_t warp_size);
LaneMask lane_mask_and(const LaneMask& a, const LaneMask& b);
bool lane_mask_test(const LaneMask& m, std::uint32_t lane);
std::string lane_mask_to_hex(const LaneMask& m);

ValueType parse_value_type(const std::string& type_mod);
std::string to_string(EventCategory c);

} // namespace gpusim
