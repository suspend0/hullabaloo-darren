#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

/*
 * Stats are buffered event counters which flush to a
 * provided client on a schedule.
 */

namespace darr {
namespace stats {
// these acquire the global lock
uint64_t read_counter(const std::string& name);
uint64_t read_gauge(const std::string& name);
std::chrono::nanoseconds read_timing(const std::string& name);

/**
 * Instances of these stats are thread safe, and are safe
 * to use as singletons or as thread_local.  Users may
 * prefer thread_local for high-volume stats to minimize
 * inter-CPU traffic.
 *
 * When constructed, these stats register themselves with
 * a global, which acquires a central lock.  Deregistration
 * happens at destruction.  Therefore, these stat objects
 * are, relatively speaking, expensive to construct and
 * users should take care they are long lived.
 *
 * Two instances of these stats with the same name, even
 * if in different translation units, will emit as the the
 * same value to the client (combined with sum).
 *
 * Stat names may have tags of the form "stat#tag:val",
 * and can be placed in maps and similar structures.
 *
 * Tags should be used sparingly, and always with a small,
 * bounded set of values. They should not originate from
 * unbounded sources, such as timestamps, user ids, or
 * request ids.
 *
 * Any statistic with tags will cause an additional stat to
 * be emitted that is the base name with ".total" appended.
 * In order to avoid double counting, the library will abort
 * if there is a stat that ends in ".total" and shares a
 * base name with any tagged stats.
 *
 * Sample usage: tags
 *
 *     // these three emit two stats: "requests" and "requests.total"
 *     // Users can use DD filtering with "requests" to look at the type,
 *     // or look at "requests.total" to see total of all request types
 *     darr::stats::Counter f1_requests{"requests#req_type:f1"};
 *     darr::stats::Counter r1_requests{"requests#req_type:r1"};
 *     darr::stats::Counter n1_requests{"requests#req_type:n1"};
 *     // If uncommented, this line would cause an abort
 *     //darr::stats::Counter total_requests{"requests.total"};
 *
 *
 * Sample usage: dispatch object:
 *
 *     // anon namespace to localize to this translation unit
 *     namespace {
 *     struct Stats {
 *     darr::stats::Counter requests{"dns_requests"};
 *     darr::stats::Counter rejects{"dns_rejects"};
 *     darr::stats::Counter f1_requests{"f1_requests"};
 *     };
 *     thead_local Stats stats;
 *     } // anonymous namesapce
 *
 *     namespace service {
 *     void do_foo() {
 *       ++stats.requests;
 *     }
 *     } // namespace service
 *
 * Sample usage: service singleton
 *
 *     class Impl {
 *      public:
 *       void run() {
 *         // this is program life b/c Impl::run loops forever
 *         darr::stats::Counter messages_stat{"message_type_incoming"};
 *         while(read()) {
 *           ++messages_stat;
 *         }
 *       }
 *     }
 *
 * Sample usage: process distribution
 *
 *     class Shard {
 *       // the size of all shards is combined before sending to DD
 *       darr::stats::Gauge map_size_stat{"map_size"};
 *      public:
 *       void accept(thing) {
 *         // process thing
 *         map_size_stat = map_.size();
 *       }
 *      private:
 *       std::map<foo, bar> map_;
 *     }
 */

class Counter {
 public:
  Counter(std::string);
  Counter(Counter&&);
  ~Counter();

  void operator++() { val_.fetch_add(1, std::memory_order_relaxed); }
  void operator++(int) { val_.fetch_add(1, std::memory_order_relaxed); }
  void operator+=(uint32_t v) { val_.fetch_add(v, std::memory_order_relaxed); }
  size_t read() const { return val_.load(std::memory_order_relaxed); }
  uint32_t drain() { return val_.exchange(0, std::memory_order_relaxed); }

 private:
  std::atomic<uint32_t> val_{0};
};

class Gauge {
  friend class IncrementingGauge;

 public:
  Gauge(std::string);
  Gauge(Gauge&&);
  ~Gauge();

  Gauge& operator=(size_t v) {
    val_.store(v, std::memory_order_relaxed);
    update_max(v);
    return *this;
  }

  size_t read() const { return val_.load(std::memory_order_relaxed); }
  uint32_t drain() { return max_.exchange(read(), std::memory_order_relaxed); }

 private:
  void update_max(uint32_t v) {
    uint32_t prev = max_.load(std::memory_order_relaxed);
    while (prev < v &&
           !max_.compare_exchange_weak(prev, v, std::memory_order_relaxed))
      ;
  }

 private:
  std::atomic<uint32_t> max_{0};
  std::atomic<uint32_t> val_{0};
};

class Timing {
 public:
  Timing(std::string);
  Timing(Timing&&);
  ~Timing();

  void operator+=(std::chrono::nanoseconds v) {
    val_.fetch_add(v.count(), std::memory_order_relaxed);
  }
  std::chrono::nanoseconds read() const {
    return std::chrono::nanoseconds(val_.load(std::memory_order_relaxed));
  }
  std::chrono::nanoseconds drain() {
    return std::chrono::nanoseconds(
        val_.exchange(0, std::memory_order_relaxed));
  }

 private:
  std::atomic<uint64_t> val_{0};
};

/**
 * Client interface.
 */
class Client {
 public:
  virtual void count(std::string_view name, uint64_t value) = 0;
  virtual void count(std::string_view name, uint64_t value,
                     std::string_view tag) = 0;
  virtual void gauge(std::string_view name, uint64_t value) = 0;
  virtual void gauge(std::string_view name, uint64_t value,
                     std::string_view tag) = 0;
  // only reports millisecond precision
  virtual void timing(std::string_view name, std::chrono::nanoseconds) = 0;

  virtual ~Client() {}
};

/**
 * Allow the returned object to collect to stop publishing
 *
 * {
 *   // start publishing, it stops at end of scope
 *   auto emitter = darr::stats::start_publishing(client);
 * }
 */
class Publisher {
 public:
  virtual ~Publisher() {}
};
std::unique_ptr<Publisher> start_publishing(
    Client& client,
    std::chrono::milliseconds publish_frequency = std::chrono::seconds(10));

}  // namespace stats
}  // namespace darr
