#include "pls.h"

PLS_INCLUDE_HEADER_ONLY_CURRENT();

#include "bricks/dflags/dflags.h"
#include "blocks/http/api.h"
#include "bricks/sync/waitable_atomic.h"

DEFINE_uint16(port, 7001, "The port to run on.");

using CHUNKED_T =
    current::net::HTTPServerConnection::ChunkedResponseSender<CURRENT_BRICKS_HTTP_DEFAULT_CHUNK_CACHE_SIZE>;

struct RequestContext {
  // Request r;
  // CHUNKED_T c;
  int n;
  int i;
  std::chrono::microseconds t0;
  explicit RequestContext(Request r, CHUNKED_T c, int n, std::chrono::microseconds t0) {
    //  : r(std::move(r)), c(std::move(c)), n(n), i(n), t0(t0) {
    std::thread(
        [n](Request r, CHUNKED_T c) {
          for (int i = n; i >= 1; --i) {
            if ((n % i) == 0) {
              c(current::strings::Printf("RESPONSE|local|A divisor of %d is %d.\n", n, i));
              std::this_thread::sleep_for(std::chrono::milliseconds(i * 10));
            }
          }
          c(current::strings::Printf("RESPONSE|local|Done for %d!\n", n));
        },
        std::move(r),
        std::move(c))
        .detach();
  }
};

struct NodeState final {
  std::vector<RequestContext> contexts;
};

int main() {
  current::WaitableAtomic<NodeState> waitable_node_state;

  auto& server = HTTP(current::net::BarePort(FLAGS_port));
  auto ok = [](Request r) { r("OK, try /100\n"); };
  auto scope = server.Register("/", ok);
  scope += server.Register("/ok", ok);
  scope += server.Register("/status", ok);
  scope += server.Register("/", URLPathArgs::CountMask::One, [&waitable_node_state](Request r) {
    int n;
    if (!(std::istringstream(r.url_path_args[0]) >> n)) {
      r("Need a number as a URL path arg.\n");
    } else if (!(n >= 1 && n <= 1000)) {
      r("Need a number between 1 and 1000 as a URL path arg.\n");
    } else {
      CHUNKED_T c = r.SendChunkedResponse();
      waitable_node_state.MutableUse([n, &r, &c](NodeState& node_state) {
        node_state.contexts.emplace_back(std::move(r), std::move(c), n, current::time::Now());
      });
    }
  });
  server.Join();
}
