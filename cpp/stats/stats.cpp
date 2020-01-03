#include <algorithm>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "stats.h"

namespace darr {
namespace stats {
void default_fatal_callback(std::string msg) {
  std::cerr << "FATAL Stats " << msg;
  std::abort();
}
void (*fatal_error_handler)(std::string msg) = &default_fatal_callback;
}  // namespace stats
}  // namespace darr

namespace {
using namespace darr::stats;
using nanoseconds = std::chrono::nanoseconds;

struct StatsSystem {
  // StatsSystem does not get destructed to avoid ordering
  // its destruction with its clients
  StatsSystem() = default;
  ~StatsSystem() = delete;
  // all accesses are protected by this global mutex
  std::mutex stats_lock;
  // multimap feels natural here but it doesn't fit our iteration model
  std::map<std::string, std::vector<Counter*>> counters;
  std::map<std::string, std::vector<Gauge*>> gauges;
  std::map<std::string, std::vector<Timing*>> timings;
  // these are counters which have been destructed but not yet published
  std::map<std::string, uint32_t> dead_counters;
  std::map<std::string, uint32_t> dead_gauges;
  std::map<std::string, nanoseconds> dead_timings;

  // == Generic methods

  template <typename MapT, typename T>
  void add(MapT& map, std::string& name, T* item) {
    validate_name(name);
    std::lock_guard<std::mutex> _(stats_lock);
    validate_total(name, map);
    map[name].push_back(item);
  }
  template <typename MapT, typename DeadMapT, typename T>
  void remove(MapT& map, DeadMapT& save, T* item) {
    // since vector order doesn't matter, we swap the
    // back into the vacated slot instead of shifting
    // everything down.
    std::lock_guard<std::mutex> _(stats_lock);
    for (auto& entry : map) {
      auto& vec = entry.second;
      auto it = std::find(begin(vec), end(vec), item);
      if (it != vec.end()) {
        using std::swap;
        swap(*it, vec.back());
        save[entry.first] += vec.back()->drain();
        vec.pop_back();
        if (vec.empty()) {
          map.erase(entry.first);
        }
        return;  // iterators may be invalid now
      }
    }
  }
  template <typename MapT, typename T>
  void add(MapT& map, T* from_item, T* to_item) {
    std::lock_guard<std::mutex> _(stats_lock);
    for (auto& entry : map) {
      auto& vec = entry.second;
      auto it = std::find(begin(vec), end(vec), from_item);
      if (it != vec.end()) {
        vec.push_back(to_item);
        return;
      }
    }
  }

  // == Type-specific mutators

  void add(std::string& nm, Counter* item) { return add(counters, nm, item); }
  void add(Counter* from, Counter* to) { return add(counters, from, to); }
  void remove(Counter* item) { return remove(counters, dead_counters, item); }

  void add(std::string& name, Gauge* item) { return add(gauges, name, item); }
  void add(Gauge* from, Gauge* to) { return add(gauges, from, to); }
  void remove(Gauge* item) { return remove(gauges, dead_gauges, item); }

  void add(std::string& nm, Timing* item) { return add(timings, nm, item); }
  void add(Timing* from, Timing* to) { return add(timings, from, to); }
  void remove(Timing* item) { return remove(timings, dead_timings, item); }

  uint64_t to_int(std::chrono::nanoseconds ns) { return ns.count(); }
  uint64_t to_int(uint32_t u) { return u; }

  // == Read

  template <typename MapT>
  uint64_t read(const MapT& map, const std::string& name) {
    uint64_t result = std::numeric_limits<uint64_t>::max();
    auto it = map.find(name);
    if (it != map.end()) {
      result = 0;
      for (auto& v : it->second) {
        result += to_int(v->read());
      }
    }
    return result;
  }
  uint64_t read_counter(const std::string& name) {
    std::lock_guard<std::mutex> _(stats_lock);
    return read(counters, name);
  }
  uint64_t read_gauge(const std::string& name) {
    std::lock_guard<std::mutex> _(stats_lock);
    return read(gauges, name);
  }
  nanoseconds read_timing(const std::string& name) {
    std::lock_guard<std::mutex> _(stats_lock);
    return nanoseconds(read(timings, name));
  }

  // == Iteration

  template <typename FuncT>
  void iterate_counters(FuncT&& cb) {
    std::lock_guard<std::mutex> _(stats_lock);
    for (auto& entry : counters) {
      uint32_t v = 0;
      for (auto& item : entry.second) {
        v += item->drain();
      }
      cb(entry.first, v);
    }
    for (auto& entry : dead_counters) {
      cb(entry.first, entry.second);
    }
    dead_counters.clear();
  }

  template <typename FuncT>
  void iterate_gauges(FuncT&& cb) {
    std::lock_guard<std::mutex> _(stats_lock);
    for (auto& entry : gauges) {
      uint32_t v = 0;
      for (auto& item : entry.second) {
        v += item->drain();
      }
      cb(entry.first, v);
    }
    for (auto& entry : dead_gauges) {
      cb(entry.first, entry.second);
    }
    dead_gauges.clear();
  }

  template <typename FuncT>
  void iterate_timings(FuncT&& cb) {
    std::lock_guard<std::mutex> _(stats_lock);
    for (auto& entry : timings) {
      nanoseconds v{0};
      for (auto& item : entry.second) {
        v += item->drain();
      }
      cb(entry.first, v);
    }
    for (auto& entry : dead_timings) {
      cb(entry.first, entry.second);
    }
    dead_timings.clear();
  }

  // == Util methods

  void validate_name(const std::string& name) {
    auto split_step = [](std::string_view& str, char c) {
      std::string_view result{};
      if (auto p = str.find(c); p != std::string::npos) {
        result = str.substr(0, p);
        str = str.substr(p);
      } else {
        result = str;
        str = {};
      }
      return result;
    };
    auto check_name = [](std::string_view name) {
      bool bad = std::any_of(std::begin(name), std::end(name), [](auto& c) {
        return c == ' ' || c == ':' || c == '|' || c == '@';
      });
      if (bad) {
        fatal_error_handler(std::string(name) +
                            " cannot contain space/colon/bar/@");
      }
    };
    auto check_tags = [&check_name, &split_step](std::string_view tags) {
      while (!tags.empty()) {
        auto tag = split_step(tags, ',');
        auto pos = tag.find(':');
        if (pos == std::string::npos) {
          fatal_error_handler(std::string(tags) +
                              ": improperly formatted tag '" +
                              std::string(tag) + "' expect name:value");
        }
        check_name(tag.substr(0, pos));   // tag name
        check_name(tag.substr(pos + 1));  // tag val
      }
    };

    auto pos = name.find('#');
    if (pos == std::string::npos) {
      check_name(name);
    } else {
      check_name(name.substr(0, pos));
      check_tags(name.substr(pos + 1));
    }
  }

  template <typename MapT>
  void validate_total(const std::string& name, MapT& existing) {
    auto totalpos = name.rfind('.');
    bool is_total =
        (totalpos != std::string::npos && name.substr(totalpos + 1) == "total");

    auto tagpos = name.find('#');
    if (tagpos == std::string::npos) {
      // If this is a total, check for tags with this prefix
      auto prefix = name.substr(0, totalpos);
      prefix.push_back('#');
      if (is_total) {
        auto it = existing.upper_bound(prefix);
        if (it != existing.end() && it->first.size() >= prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), it->first.begin())) {
          fatal_error_handler(name + " would duplicate generated total for " +
                              it->first);
        }
      }
    } else {
      // Check for a total with this prefix
      auto total = name.substr(0, tagpos) + ".total";
      if (existing.count(total) > 0) {
        fatal_error_handler(total + " duplicates generated total for " + name);
      }
    }
  }
};

StatsSystem& system() {
  static StatsSystem* instance = new StatsSystem();
  return *instance;
}

class PublishThread : public Publisher {
 public:
  PublishThread(Client& client, std::chrono::milliseconds publish_frequency)
      : client_{client},
        publish_frequency_{publish_frequency},
        thread_{&PublishThread::run, this, shutdown_signal_.get_future()} {}
  ~PublishThread() {
    shutdown_signal_.set_value();
    thread_.join();
  }

  void run(std::future<void> shutdown_signal) {
    // while not ready to shutdown ...
    while (shutdown_signal.wait_for(publish_frequency_) !=
           std::future_status::ready) {
      emit();
    }
    emit();
  }

  void emit() {
    {
      std::map<std::string, uint32_t> totals;
      system().iterate_counters([&](const std::string& name, uint32_t value) {
        auto pos = name.find('#');
        if (pos == std::string::npos) {
          client_.count(name, value);
        } else {
          // split into name & tag, and add to total
          totals[name.substr(0, pos).append(".total")] += value;
          client_.count(name.substr(0, pos), value, name.substr(pos + 1));
        }
      });
      for (auto& entry : totals) {
        client_.count(entry.first, entry.second);
      }
    }

    {
      std::map<std::string, uint32_t> totals;
      system().iterate_gauges([&](const std::string& name, uint32_t value) {
        auto pos = name.find('#');
        if (pos == std::string::npos) {
          client_.gauge(name, value);
        } else {
          totals[name.substr(0, pos).append(".total")] += value;
          client_.gauge(name.substr(0, pos), value, name.substr(pos + 1));
        }
      });
      for (auto& entry : totals) {
        client_.gauge(entry.first, entry.second);
      }
    }

    system().iterate_timings([&](const std::string& name, nanoseconds value) {
      client_.timing(name, value);
    });
  }

 private:
  Client& client_;
  std::chrono::milliseconds publish_frequency_;
  std::promise<void> shutdown_signal_;
  std::thread thread_;
};

}  // namespace

// --- --- --- // --- --- --- // --- --- --- // --- --- --- //

namespace darr {
namespace stats {

// The name is not held in the counter object iself so we can fit
// more in a cache line.
Counter::Counter(std::string name) { system().add(name, this); };
Counter::Counter(Counter&& c) {
  system().add(&c, this);  // this does name lookup from &c
  val_ += c.drain();
}
Counter::~Counter() { system().remove(this); };

Gauge::Gauge(std::string name) { system().add(name, this); };
Gauge::Gauge(Gauge&& g) {
  system().add(&g, this);  // this does name lookup from &g
  val_ = 0;
}
Gauge::~Gauge() { system().remove(this); };

Timing::Timing(std::string name) { system().add(name, this); };
Timing::Timing(Timing&& t) {
  system().add(&t, this);  // this does name lookup from &c
  val_ += t.drain().count();
}
Timing::~Timing() { system().remove(this); };

std::unique_ptr<Publisher> start_publishing(
    Client& client, std::chrono::milliseconds publish_frequency) {
  return std::make_unique<PublishThread>(client, publish_frequency);
}

// the below are used for tests
uint64_t read_counter(const std::string& name) {
  return system().read_counter(name);
}
uint64_t read_gauge(const std::string& name) {
  return system().read_gauge(name);
}
nanoseconds read_timing(const std::string& name) {
  return system().read_timing(name);
}

void iterate_counters(
    const std::function<void(const std::string&, uint32_t)> cb) {
  system().iterate_counters(cb);
}
void iterate_gauges(
    const std::function<void(const std::string&, uint32_t)> cb) {
  system().iterate_gauges(cb);
}
void iterate_timings(
    const std::function<void(const std::string&, nanoseconds)> cb) {
  system().iterate_timings(cb);
}

}  // namespace stats
}  // namespace darr
