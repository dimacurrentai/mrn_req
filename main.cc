#include "pls.h"

PLS_INCLUDE_HEADER_ONLY_CURRENT();

#include "bricks/dflags/dflags.h"
#include "blocks/http/api.h"
#include "bricks/sync/waitable_atomic.h"

DEFINE_uint16(base_port, 7001, "The port to run on, will use this and a few more after it.");
DEFINE_uint16(n_executors, 3, "The number of executors (threads, simulated workers) to spawn.");

using CHUNKED_T =
    current::net::HTTPServerConnection::ChunkedResponseSender<CURRENT_BRICKS_HTTP_DEFAULT_CHUNK_CACHE_SIZE>;

struct TaskStepResponse {
  int64_t next_dt = 0;                // Set to nonzero to be `.Advance()`-d later on, or zero to be stopped.
  std::function<void()> side_effect;  // Set to be executed by the LEADER, and swallowed silently by the followers.
};

struct ExecutableTaskTrait {
  virtual ~ExecutableTaskTrait() {}

  // Return a zero and your executable task is destroyed right afterwards.
  // Return a positive value and your `.Advance()` is called in this number of microseconds.
  virtual TaskStepResponse Advance() = 0;
};

struct DivisorsStateMachine final : ExecutableTaskTrait {
  Request r;
  CHUNKED_T c;
  int n;
  int i;
  explicit DivisorsStateMachine(Request r, CHUNKED_T c, int n) : r(std::move(r)), c(std::move(c)), n(n), i(n) {}

  // The state machine implemented explicly, compare to its implicit logic with `sleep_for()` in the previous commit.
  TaskStepResponse Advance() override {
    while (i > 0) {
      if ((n % i) == 0) {
        auto const s = current::strings::Printf("RESPONSE|local|A divisor of %d is %d.\n", n, i);
        return {(i--) * 10'000, [this, s]() { c(s); }};
      } else {
        i--;
      }
    }
    return {0, [this]() { c(current::strings::Printf("RESPONSE|local|Done for %d!\n", n)); }};
  }
};

struct NodeState final {
  std::map<std::chrono::microseconds, std::unique_ptr<ExecutableTaskTrait>> pqueue;
  void PushTask(std::chrono::microseconds ts, std::unique_ptr<ExecutableTaskTrait> task) {
    // For the code to be simple, we assume at most one task for a given timestamp.
    // So keep incrementing the timestamp by one microsecond until we find an "empty slot".
    // Not algorithmically perfect, but doesn't matter for now.
    while (pqueue.count(ts)) {
      ts += std::chrono::microseconds(1);
    }
    pqueue[ts] = std::move(task);
  }
};

struct Worker {
  HTTPRoutesScope scope;
  current::WaitableAtomic<NodeState> waitable_node_state;
  std::thread worker_thread;
  Worker(int n, uint16_t port)
      : worker_thread([this]() {
          std::chrono::microseconds const WAIT_SKIP(1);
          std::chrono::microseconds const WAIT_FOREVER(1'000'000'000'000);
          std::chrono::microseconds wait_duration(WAIT_FOREVER);
          while (true) {
            using PAIR_T = std::pair<std::chrono::microseconds, std::unique_ptr<ExecutableTaskTrait>>;
            bool const waiting_forever = wait_duration == WAIT_FOREVER;
            PAIR_T pair = waitable_node_state.WaitFor(
                [waiting_forever](NodeState const& node_state) {
                  if (waiting_forever) {
                    return !node_state.pqueue.empty();
                  } else {
                    return !node_state.pqueue.empty() && node_state.pqueue.begin()->first < current::time::Now();
                  }
                },
                [](NodeState& node_state) -> PAIR_T {
                  if (!node_state.pqueue.empty() && node_state.pqueue.begin()->first < current::time::Now()) {
                    // If there is a task and its time has come, then execute its `.Advance()`.
                    PAIR_T result = std::move(*node_state.pqueue.begin());
                    node_state.pqueue.erase(node_state.pqueue.begin());
                    return result;
                  } else if (!node_state.pqueue.empty()) {
                    // If there is a task but its time has not come, note when its time does come.
                    return {node_state.pqueue.begin()->first, nullptr};
                  } else {
                    return PAIR_T();
                  }
                },
                wait_duration);
            if (pair.second) {
              // There's a task to run.
              TaskStepResponse response = pair.second->Advance();
              int64_t const next_dt = response.next_dt;
              if (response.side_effect) {
                response.side_effect();
              }
              // Re-insert it back to the list of tasks to run, if needed.
              // Otherwise it will be destroyed as this scope ends, right after the next `if`,
              // thus closing the connection.
              if (next_dt) {
                std::chrono::microseconds next_ts = pair.first + std::chrono::microseconds(next_dt);
                waitable_node_state.MutableUse(
                    [next_ts, &pair](NodeState& node_state) { node_state.PushTask(next_ts, std::move(pair.second)); });
              }
              // If we executed something, no need to wait for the next cycle,
              // since we do not know when the next task is.
              wait_duration = WAIT_SKIP;
            } else {
              // If no tasks emerged during the wait period, `pair.first` would be `microseconds(0)`.
              // In this case, we need to wait until new tasks emerge.
              wait_duration = WAIT_FOREVER;
            }
          }
        }) {
    auto& server = HTTP(current::net::BarePort(port));
    auto ok = [](Request r) { r("OK, try /100\n"); };
    scope += server.Register("/", ok);
    scope += server.Register("/ok", ok);
    scope += server.Register("/status", ok);
    scope += server.Register("/", URLPathArgs::CountMask::One, [this](Request r) {
      int n;
      if (!(std::istringstream(r.url_path_args[0]) >> n)) {
        r("Need a number as a URL path arg.\n");
      } else if (!(n >= 1 && n <= 1000)) {
        r("Need a number between 1 and 1000 as a URL path arg.\n");
      } else {
        auto now = current::time::Now();
        CHUNKED_T c = r.SendChunkedResponse();
        auto task = std::make_unique<DivisorsStateMachine>(std::move(r), std::move(c), n);
        waitable_node_state.MutableUse(
            [n, now, &task](NodeState& node_state) { node_state.PushTask(now, std::move(task)); });
      }
    });
  }

  ~Worker() {
    // NOTE(dkorolev): This does not `.Join()` the HTTP server, but it's fine, it'll run until Ctrl+C regardless.
    worker_thread.join();
  }
};

int main() {
  std::vector<std::unique_ptr<Worker>> workers;
  for (uint16_t i = 0; i < FLAGS_n_executors; ++i) {
    workers.push_back(std::make_unique<Worker>(int(i), FLAGS_base_port + i));
  }
}
