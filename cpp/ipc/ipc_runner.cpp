#include "api.h"

#include <iostream>
#include <vector>

namespace darr {

// we use `a` and `b` here b/c both EXPECT_EQ(expected, actual)
// and EXPECT_EQ(actual, expected) are reasonable
template <typename T1, typename T2>
void expect_eq(T1&& a, T2&& b, const char* a_expr, const char* b_expr) {
  if (!(a == b)) {
    std::cerr << "EXPECTED " << a_expr << " == " << b_expr << "\n"
              << "     WAS " << a << " == " << b << "\n";
  }
}
#define EXPECT_EQ(a, b) expect_eq(a, b, #a, #b);

void run() {
  std::vector<uint8_t> bytes;
  {
    SomeRequest req;
    auto& h = req.headers.emplace_back();
    h.name = "Vary";
    h.value = "all";

    marshalling::write_item(bytes, req);
  }
  {
    SomeRequest req;
    marshalling::read_item(bytes, req);
    EXPECT_EQ(req.headers.size(), 1);
    EXPECT_EQ(req.headers[0].name, "Vary");
    EXPECT_EQ(req.headers[0].value, "all");
  }
}
}  // namespace darr

int main() {
  std::cout << "starting\n";

  darr::run();

  std::cout << "exiting\n";
}
