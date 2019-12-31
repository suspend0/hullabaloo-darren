#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <variant>

namespace darr {

/**
 * This class implements the details of a Quiescent-State Based
 * Reclamation strategy which allows a single writer and
 * multiple readers.
 *
 * The writer thread maintains a global epoch pointer which is
 * incremented as changes are made, and reader threads store
 * the epoch at which they were *outside* a critical section.
 * The writer puts items to delete on a garbage list and then
 * deletes them once all readers are newer than the point at
 * which the garbage was created.
 *
 * If this seems a bit fuzzy, google QSBR & its sibling
 * EBR (Epoch Based Reclamation)
 */
template <typename... GarbageT>
class SingleWriterQuiescentStateReclamation {
  using Epoch = uint64_t;
  using AtomicEpoch = std::atomic<Epoch>;

 public:  // == Types == == ==
  class Reader {
   public:
    Reader(AtomicEpoch& e) : global_{e} {
      static_assert(sizeof(Reader) == 64 - 16,
                    "One cache line, minus std::list overhead");
      padding[0] = 17;  // "use" the private member
      on_quiesce();     // quiesce here so we don't artificially delay GC
    }
    void on_quiesce() { local_ = global_.load(); }
    Epoch current_epoch() { return local_.load(); }

   private:
    AtomicEpoch local_;
    AtomicEpoch& global_;
    uint64_t padding[4];
  };
  using ReaderDestructor = std::function<void(Reader*)>;
  using ReaderHandle = std::unique_ptr<Reader, ReaderDestructor>;

  struct Trash {
    Epoch epoch;
    std::variant<std::unique_ptr<const GarbageT>...> item;
  };

 public:  // == Constructor == == ==
  SingleWriterQuiescentStateReclamation() = default;
  SingleWriterQuiescentStateReclamation(
      const SingleWriterQuiescentStateReclamation&) = delete;

 public:  // == Methods == == ==
  size_t pending_garbage() { return garbage_.size(); }

  // Schedules destruction once there are no readers
  template <typename T>
  void destroy_later(std::unique_ptr<const T>&& g) {
    garbage_.emplace_back(Trash{global_epoch_.load(), std::move(g)});
  }
  template <typename T>
  void destroy_later(const T* p) {
    destroy_later(std::unique_ptr<const T>{p});
  }

  // Manages active readers
  ReaderHandle create_reader() {
    std::lock_guard<std::mutex> locked(readers_lock_);
    readers_.emplace_front(global_epoch_);
    auto* ptr = &readers_.front();
    return ReaderHandle{ptr, [this](auto* r) {
                          std::lock_guard<std::mutex> locked(readers_lock_);
                          readers_.remove_if([&](auto&& e) { return &e == r; });
                        }};
  }

  // Garbage collect what we can
  uint64_t garbage_collect() {
    std::lock_guard<std::mutex> locked(readers_lock_);
    Epoch global_epoch = global_epoch_.fetch_add(1);
    Epoch gc_epoch = min_quiesced_epoch();

    if (gc_epoch > 0 && gc_epoch < global_epoch) {
      gc_epoch = gc_epoch - 1;
      while (!garbage_.empty() && garbage_.front().epoch < gc_epoch) {
        garbage_.pop_front();
      }
    }

    return (global_epoch > gc_epoch) ? global_epoch - gc_epoch : 0;
  }

 private:
  // Returns the epoch available for gc
  Epoch min_quiesced_epoch() {
    // max here to allow writer to collect if there are no readers
    Epoch min = std::numeric_limits<Epoch>::max();
    // can't use std::min_element here b/c we need a snapshot
    auto iter = readers_.begin(), end = readers_.end();
    for (++iter; iter != end; ++iter) {
      min = std::min(min, iter->current_epoch());
    }
    return min;
  }

 private:
  AtomicEpoch global_epoch_{0};
  std::mutex readers_lock_{};
  std::list<Reader> readers_{};
  std::deque<Trash> garbage_{};
};

}  // namespace darr
