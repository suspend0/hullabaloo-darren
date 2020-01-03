#include "stats.h"

#include <iostream>
#include <thread>

namespace darr {

// This writes the stats to stdout
struct TestClient : stats::Client {
  virtual void count(std::string_view name, uint64_t value) {
    std::cout << "C:" << name << " " << value << "\n";
  }
  virtual void count(std::string_view name, uint64_t value,
                     std::string_view tag) {
    std::cout << "C:" << name << "#" << tag << " " << value << "\n";
  }
  virtual void gauge(std::string_view name, uint64_t value) {
    std::cout << "G:" << name << " " << value << "\n";
  }
  virtual void gauge(std::string_view name, uint64_t value,
                     std::string_view tag) {
    std::cout << "G:" << name << "#" << tag << " " << value << "\n";
  }
  // only reports millisecond precision
  virtual void timing(std::string_view name, std::chrono::nanoseconds ns) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(ns);
    std::cout << "T:" << name << " " << ms.count() << "\n";
  }
};

//
// Two threads. The emit_thread() runs the emitter for 500 ms, and
// and the stats thread creates and modifies stats objects.
//
std::chrono::milliseconds run_time{500};
void emit_thread() {
  TestClient client;
  auto emitter =
      stats::start_publishing(client, std::chrono::milliseconds(100));
  std::this_thread::sleep_for(run_time);
}

//
// The counters flush as they are emitted, so iterations will have
// different values depending on how the cycle type of this thread
// interacts with the cycle type of the background thread
//
void stats_thread() {
  using std::chrono::duration_cast;
  using std::chrono::milliseconds;
  using std::chrono::steady_clock;

  // these auto-register with the system, and un-register
  // when destructed
  stats::Counter a{"count.a"};
  stats::Counter b{"count.b"};
  stats::Gauge c{"gauge.c"};

  auto now = steady_clock::now();
  for (auto expires = now + run_time / 2; now < expires;
       now = steady_clock::now()) {
    ++a;
    b += 2;
    c = duration_cast<milliseconds>(expires - now).count();
    std::this_thread::sleep_for(milliseconds(75));
  }
}
}  // namespace darr

int main() {
  std::cout << "starting...\n";

  std::thread publ(darr::emit_thread);
  std::thread stat(darr::stats_thread);
  publ.join();
  stat.join();

  std::cout << "stopping...\n";
}
