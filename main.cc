#include "pls.h"

PLS_INCLUDE_HEADER_ONLY_CURRENT();

#include "bricks/dflags/dflags.h"
#include "blocks/http/api.h"

DEFINE_uint16(port, 7001, "The port to run on.");

int main() {
  auto& server = HTTP(current::net::BarePort(FLAGS_port));
  auto scope = server.Register("/", [](Request r) {
    r("Hello, World!\n");
  });
  scope += server.Register("/chunked", [](Request r) {
    std::thread([](Request r) {
      auto c = r.SendChunkedResponse();
      int const n = 10;
      for (int i = 0; i < n; ++i) {
        c(current::strings::Printf("Chunk %d/%d\n", i + 1, n));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
      c("Done!\n");
    }, std::move(r)).detach();
  });
  server.Join();
}
