#pragma once

#include <array>
#include <variant>
#include <vector>

namespace darr {

/**
 * A hacked together vector type which stores N items
 * on the stack before switching to the heap.  It does
 * not support erase; if you resize down it zero-initializes
 * but doesn't destruct.  When using stack storage it has
 * the semantics of std::array rather than vector.
 *
 * Better implementations of this pattern exist as
 * boost::small_vector and folly::small_vector
 */
template <typename T, size_t N>
class small_vector {
  using stack_type = std::array<T, N>;
  using heap_type = std::vector<T>;

 public:
  using value_type = T;

  // == basic accessors
  // ==
  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }
  value_type* data() { return &(*this)[0]; }
  const value_type* data() const { return &(*this)[0]; }

  // == iteration
  // ==
  value_type* begin() { return &(*this)[0]; }
  value_type* end() { return &(*this)[size_]; }
  const value_type* begin() const { return &(*this)[0]; }
  const value_type* end() const { return &(*this)[size_]; }

  // == get individual values
  // ==
  value_type& at(size_t offset) {
    if (!(offset < size_))
      throw std::out_of_range("small_vector");
    return (*this)[offset];
  }
  value_type& operator[](size_t offset) {
    return std::visit([&](auto& d) -> value_type& { return d[offset]; }, data_);
  }
  const value_type& operator[](size_t offset) const {
    return std::visit(
        [&](auto& d) -> auto& { return d[offset]; }, data_);
  }
  value_type& back() {
    return std::visit(
        [&](auto& d) -> auto& { return d[size_]; }, data_);
  }
  const value_type& back() const {
    return std::visit(
        [&](auto& d) -> auto& { return d[size_]; }, data_);
  }

  // == insert individual values
  // ==
  void push_back(value_type&& v) { emplace_back(std::move(v)); }

  template <typename... ArgTs>
  value_type& emplace_back(ArgTs&&... v) {
    auto n = size_;
    ++size_;
    if (is_stack() && size_ < N) {
      return (stack()[n] = T{std::forward<ArgTs>(v)...});
    } else if (is_stack()) {
      convert_to_heap();
      return heap().emplace_back(std::forward<ArgTs>(v)...);
    } else {
      return heap().emplace_back(std::forward<ArgTs>(v)...);
    }
  }

  // == range insertion
  // ==
  template <class InputIt>
  void insert(value_type* location, InputIt first, InputIt last) {
    if (location == end()) {
      while (first != last) {
        emplace_back(*first);
        ++first;
      }
    } else {
      if (is_stack())
        convert_to_heap();
      auto& vec = heap();
      auto iter = vec.begin() + std::distance(begin(), location);
      vec.insert(iter, first, last);
    }
  }

  // == preallocation
  // ==
  void resize(size_t size) {
    if (is_stack() && size < N) {
      size_t first = std::min(size, size_);
      size_t last = std::max(size, size_);
      auto& arr = stack();
      while (first != last) {
        arr[first] = T{};
        ++first;
      }
    } else if (is_stack()) {
      convert_to_heap();
      heap().resize(size);
    } else {
      heap().resize(size);
    }
    size_ = size;
  }

 private:
  bool is_stack() { return std::holds_alternative<stack_type>(data_); }
  stack_type& stack() { return std::get<stack_type>(data_); }
  heap_type& heap() { return std::get<heap_type>(data_); }

  void convert_to_heap() {
    auto& s = stack();
    heap_type vec{std::make_move_iterator(s.begin()),
                  std::make_move_iterator(s.end())};
    data_ = std::move(vec);
  }

 private:
  size_t size_{0};
  std::variant<stack_type, heap_type> data_;
};

}  // namespace darr
