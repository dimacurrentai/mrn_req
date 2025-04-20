#include "pls.h"

PLS_INCLUDE_HEADER_ONLY_CURRENT();

#include "bricks/dflags/dflags.h"
#include "blocks/http/api.h"

DEFINE_uint16(port, 7001, "The port to run on.");

int main() {
  auto& server = HTTP(current::net::BarePort(FLAGS_port));
  auto ok = [](Request r) { r("OK, try /100\n"); };
  auto scope = server.Register("/", ok);
  scope += server.Register("/ok", ok);
  scope += server.Register("/status", ok);
  scope += server.Register("/", URLPathArgs::CountMask::One, [](Request r) {
    int n;
    if (!(std::istringstream(r.url_path_args[0]) >> n)) {
      r("Need a number as a URL path arg.\n");
    } else if (!(n >= 1 && n <= 1000)) {
      r("Need a number between 1 and 1000 as a URL path arg.\n");
    } else {
      std::thread([n](Request r) {
        auto c = r.SendChunkedResponse();
        for (int i = n; i >= 1; --i) {
          if ((n % i) == 0) {
            c(current::strings::Printf("RESPONSE|local|A divisor of %d is %d.\n", n, i));
            std::this_thread::sleep_for(std::chrono::milliseconds(i * 10));
          }
        }
        c(current::strings::Printf("RESPONSE|local|Done for %d!\n", n));
      }, std::move(r)).detach();
    }
  });
  server.Join();
}
