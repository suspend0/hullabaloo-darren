#include "qsbr.h"

#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>

using std::chrono::steady_clock;
using std::chrono::system_clock;

namespace darr {
template <typename T, typename... Ts>
void log(T&& item, Ts&&... rest) {
  static std::mutex cout_lock;
  std::lock_guard<std::mutex> _(cout_lock);

  std::time_t now = std::time(nullptr);
  std::tm tm = *std::localtime(&now);

  std::cout << std::this_thread::get_id()     //
            << std::put_time(&tm, " %F %T ")  //
            << std::forward<T>(item);
  ((std::cout << ' ' << std::forward<Ts>(rest)), ...);
  std::cout << '\n';
}

std::string* random_string() {
  constexpr static char charset[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  constexpr static size_t max_index = (sizeof(charset) - 1);
  size_t length = rand() % 64;
  auto* str = new std::string(length, 0);
  std::generate_n(str->begin(), length,
                  [] { return charset[rand() % max_index]; });
  return str;
}

//
// Demonstrates `SingleWriterQuiescentStateReclamation` (QSBR)
//
//  - reader threads register with the system and signal
//    when they are not using shared data
//  - a single writer thread mutates, and then delegates
//    destruction to the system
//
void threads_test() {
  constexpr static auto run_for = std::chrono::seconds(10);
  constexpr static auto stat_every = std::chrono::seconds(2);
  constexpr static auto use_qsbr = true;

  SingleWriterQuiescentStateReclamation<std::string> qsbr;
  std::atomic<bool> running{true};

  // construct the data we're going to mutate
  std::array<std::atomic<std::string*>, 256> map{};
  std::generate_n(map.begin(), map.size(), random_string);

  // logic for reader & writer threads
  auto reader = [&] {
    auto handle = qsbr.create_reader();
    uint32_t counter;
    while (running.load()) {
      auto idx = rand() % map.size();
      counter += map[idx].load()->size();
      handle->on_quiesce();
    }
    log("counted", counter);
  };
  auto writer = [&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    size_t writes = 0;
    auto tp = steady_clock::now();
    while (running.load()) {
      auto idx = rand() % map.size();
      std::string* next = random_string();
      std::string* prev = map[idx].exchange(next);
      if (use_qsbr) {
        // This queues the release until the reader calls `on_quiesce()`
        qsbr.destroy_later(prev);
        auto lag = qsbr.garbage_collect();
        if (auto now = steady_clock::now(); now > tp + stat_every) {
          log("pending", qsbr.pending_garbage(), "lag", lag);
          tp = now;
        }
      } else {
        // the string will be destroyed at end of scope, which is
        // a race between the reader and writer threads, and could
        // cause a heap use-after-free
        delete prev;
      }
    }
  };
  auto stopper = [&] {
    std::this_thread::sleep_for(run_for);
    running = false;
  };

  // start a bunch of threads
  std::vector<std::thread> threads;
  threads.emplace_back(stopper);
  for (int i = 0; i < 4; i++) {
    threads.emplace_back(reader);
  }

  // do writes from this thread
  writer();

  // wait for everything to stop
  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace darr

int main() {
  darr::log("running...");
  darr::threads_test();
  darr::log("...complete");
}
