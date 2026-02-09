#include "cudart_shim/stream_scheduler.h"

namespace gpusim_cudart_shim {

void StreamScheduler::submit(cudaStream_t stream, Command cmd) {
  if (!cmd) return;

  // For MVP we keep ordering semantics by queueing per-stream, but execute synchronously.
  std::vector<Command> to_run;
  {
    std::scoped_lock lk(mu_);
    auto& q = queues_[stream];
    q.push_back(std::move(cmd));
    to_run.swap(q);
  }

  for (auto& c : to_run) {
    if (c) c();
  }
}

void StreamScheduler::drain_all() {
  // submit() executes immediately, so there is nothing to do.
}

} // namespace gpusim_cudart_shim
