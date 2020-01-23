#pragma once

#include <sys/uio.h>  // iovec
#include <string_view>

#include "small_vector.h"

//
// Derive a struct from SerializedType and define structs
// within it. Classes which derive from SerializedType
// should define a `marshal()` template method to
// enumerate its fields
//
//     struct Foo : SerializedType<Foo> {
//        // types
//        struct Bar { int a, StringPtr q };
//
//        // fields
//        Bar b;
//        std::vector<int> v;
//        StringPtr s;
//
//        // method to serialize
//        template<typename Archive>
//        void marshal(Archive& ar) {
//          ar(b, v, s, strings_);
//        }
//     };
//
// The magic here is the base type SerializedType has
// a member type StringPtr, and these two communicate
// via a `thread_local` pointer.
//
//     Foo foo;
//     foo.s = "hello";
//     foo.b.q = "there";
//
// The assignment to `foo.s` uses a thread_local to find
// SerializedType::strings_ and copy into it, recording
// the offset in the StringPtr instance.
//
// Our examples declare types nested (Foo::Bar) but any
// trivially-copyable type will work.
//
// Everything else is pretty normal, if heavy on the
// templates.  See method write_item() for use.
//

namespace darr {
namespace marshalling {

// --- --- TYPE MECHANICS --- --- --- --- --- --- --- ---
namespace {
enum { MARSHALLING_MAX_FIELDS = 32 };

// these are the types of supported classes
struct strategy {
  struct marshall_method {};
  struct trivially_copyable {};
  struct trivially_copyable_container {};
};

// checks if a type is a container of trivially-copyable types
template <typename T, typename _ = void>
struct is_container_of_trivially_copyable : std::false_type {};
template <typename... Ts>
struct has_member_helper {};
template <typename T>
struct is_container_of_trivially_copyable<
    T, std::conditional_t<
           !std::is_trivially_copyable<typename T::value_type>::value,
           has_member_helper<typename T::value_type,
                             decltype(std::declval<T>().size()),
                             decltype(std::declval<T>().data())>,
           void>> : public std::true_type {};

// assigns types to their proper strategy.  If the type is
// trivially copyable, we copy it, otherwise we assume it has
// a method we can use.
template <typename T>
struct strategy_lookup
    : std::conditional<
          std::is_trivially_copyable<T>::value, strategy::trivially_copyable,
          std::conditional_t<is_container_of_trivially_copyable<T>::value,
                             strategy::trivially_copyable_container,
                             strategy::marshall_method>> {};
}  // namespace

// --- --- STREAMS --- --- --- --- --- --- --- ---
// These are adaptor classes to read or write
// data to sink
// ---

class Stream {
 public:
  // We have a four byte header the top 2 bytes are a tag and
  // the bottom two are the size of the subsequent frame
  static constexpr size_t HEADER_SIZE = 4;
  static constexpr uint32_t TAG = 0xDEAD0000;
  static constexpr uint32_t MASK = 0xFFFF0000;

  static size_t remaining_bytes(const unsigned char* data, const size_t len) {
    if (auto frame_len = frame_size(data, len);
        frame_len > 0 && size_t(frame_len) <= len) {
      return 0;  // meaning we have all bytes we need
    } else if (frame_len > 0) {
      return frame_len - len;  // we need this many more bytes
    } else {
      return HEADER_SIZE;  // we need at least header more
    }
  }

  static ssize_t frame_size(const unsigned char* data, const size_t len) {
    if (len >= HEADER_SIZE) {
      uint32_t header;
      memcpy(&header, data, HEADER_SIZE);
      if ((header & MASK) == TAG) {
        auto item_size = (header & ~MASK);
        return item_size + HEADER_SIZE;
      } else {
        throw std::out_of_range("missing stream boundary tag");
      }
    }
    return -1;
  }
};

class IOVecOutputStream {
 public:
  small_vector<iovec, MARSHALLING_MAX_FIELDS> vecs;

  IOVecOutputStream() {
    static_assert(sizeof(size_header) == Stream::HEADER_SIZE, "");
    vecs.push_back({&size_header, sizeof(size_header)});
  }

  void write_size(uint16_t val) {
    sizes.emplace_back();
    sizes.back() = val;
    write(&sizes.back(), sizeof(val));
  }
  void write(const void* item, size_t size) {
    auto* ptr = const_cast<void*>(item);
    vecs.push_back({ptr, size});
    size_header += size;
  }

 private:
  uint32_t size_header{Stream::TAG};
  // Since we return a pointer to these fields in `vecs`,
  // they can't move, so we can't use heap.
  small_vector<uint16_t, MARSHALLING_MAX_FIELDS> sizes{};
};

template <typename Container>
struct ContainerOutputStream {
  Container& data;
  ContainerOutputStream(Container& c) : data{c} {}
  void write_size(uint16_t val) { write(&val, sizeof(val)); }
  void write(const void* item, size_t size) {
    auto* ptr = static_cast<const uint8_t*>(item);
    data.insert(data.end(), ptr, ptr + size);
  }
};

struct RangeOutputStream {
  uint8_t* ptr;
  uint8_t* end;
  RangeOutputStream(uint8_t* buf, size_t len) : ptr{buf}, end{buf + len} {}
  void write_size(uint16_t val) { write(&val, sizeof(val)); }
  void write(const void* item, size_t size) {
    std::memcpy(ptr, item, size);
    ptr += size;
    assert(ptr < end);
  }
};

template <typename Container>
struct ContainerInputStream {
  typename Container::const_iterator cursor;
  typename Container::const_iterator end;
  ContainerInputStream(const Container& c)
      : cursor{std::begin(c)}, end{std::end(c)} {}
  void read(void* item, size_t size) {
    if (size > (size_t)std::distance(cursor, end))
      throw std::out_of_range("read buffer too small");
    auto* ptr = &(*cursor);
    std::advance(cursor, size);
    std::memcpy(item, ptr, size);
  }
};

// used to reset an object to a default state so we
// can reuse the field list passed to the archiver
struct Clear {
  void read(void* item, size_t size) { std::memset(item, 0, size); }
};

// used to calculate how big this object will be on the wire
struct ByteSize {
  size_t bytes{0};
  void write_size(uint16_t val) { write(&val, sizeof(val)); }
  void write(const void*, size_t size) { bytes += size; }
};

// --- --- SERIALIZERS --- --- --- --- --- --- --- ---
// These take the object-form of something and convert
// it to the byte form
// ---

template <typename T>
struct ItemSerializer {
  template <typename StreamT>
  void store(const T& item, StreamT& buf) {
    buf.write(&item, sizeof(T));
  }
  template <typename StreamT>
  void load(T& item, StreamT& buf) {
    buf.read(&item, sizeof(T));
  }
};
template <typename T>
struct ArraySerializer {
  template <typename StreamT>
  void store(const T& arr, StreamT& buf) {
    uint16_t size = arr.size();
    buf.write_size(size);
    buf.write(arr.data(), size * sizeof(typename T::value_type));
  }
  template <typename StreamT>
  void load(T& arr, StreamT& buf) {
    uint16_t size;
    buf.read(&size, sizeof(uint16_t));
    arr.resize(size);
    buf.read(arr.data(), size * sizeof(typename T::value_type));
  }
};

// --- --- ARCHIVER --- --- --- --- --- --- --- ---
// Dispatches a list of objects to their proper
// serializers
// ---

template <typename StreamT>
class ArchiveWriter {
 public:
  ArchiveWriter(StreamT& s) : stream_{s} {}
  template <typename T, typename... Types>
  void operator()(T& head, Types&... tail) {
    static_assert(sizeof...(tail) + 1 < MARSHALLING_MAX_FIELDS,
                  "too many fields");
    serialize(head, tail...);
  }

 private:
  void serialize() {}  // terminates the recursion
  template <typename T, typename... Types>
  void serialize(T& head, Types&... tail) {
    using Strategy = typename strategy_lookup<T>::type;
    save(Strategy{}, head);
    serialize(tail...);
  }
  template <typename T>
  void save(strategy::trivially_copyable, T& item) {
    ItemSerializer<T>{}.store(item, stream_);
  }
  template <typename T>
  void save(strategy::trivially_copyable_container, T& item) {
    ArraySerializer<T>{}.store(item, stream_);
  }
  template <typename T>
  void save(strategy::marshall_method, T& item) {
    item.marshal(*this);
  }

 private:
  StreamT& stream_;
};

template <typename StreamT>
class ArchiveReader {
 public:
  ArchiveReader(StreamT& s) : stream_{s} {}
  template <typename T, typename... Types>
  void operator()(T& head, Types&... tail) {
    deserialize(head, tail...);
  }

 private:
  void deserialize() {}
  template <typename T, typename... Types>
  void deserialize(T& head, Types&... tail) {
    using Strategy = typename strategy_lookup<T>::type;
    load(Strategy{}, head);
    deserialize(tail...);
  }
  template <typename T>
  void load(strategy::trivially_copyable, T& item) {
    ItemSerializer<T>{}.load(item, stream_);
  }
  template <typename T>
  void load(strategy::trivially_copyable_container, T& item) {
    ArraySerializer<T>{}.load(item, stream_);
  }
  template <typename T>
  void load(strategy::marshall_method, T& item) {
    item.marshal(*this);
  }

 private:
  StreamT& stream_;
};

// --- --- CONVENIENCE --- --- --- --- --- --- --- ---
// Utility methods that make the use of these classes
// a little easier in the common case
// ---
//
template <typename Item>
size_t byte_size(const Item& item) {
  ByteSize size;
  ArchiveWriter<ByteSize> ar{size};
  ar(item);
  return size.bytes;
}

template <typename Item, typename Container,
          typename _ = typename Container::value_type>
ssize_t write_item(Container& c, Item& item) {
  using StreamT = ContainerOutputStream<Container>;
  auto sz = c.size();
  StreamT s{c};
  ArchiveWriter<StreamT> ar{s};
  ar(item);
  return c.size() - sz;
}

template <typename Item>
ssize_t write_item(uint8_t* buf, size_t len, Item& item) {
  RangeOutputStream io{buf, len};
  ArchiveWriter<decltype(io)> ar{io};
  ar(item);
  return io.ptr - buf;
}

// note the result of this method is a cursor into the passed
// container, so if you don't pass an lvalue be careful about
// using the result
template <typename Item, typename Container>
auto read_item(const Container& c, Item& item) {
  ContainerInputStream<Container> s{c};
  ArchiveReader<ContainerInputStream<Container>> ar{s};
  ar(item);
  return s.cursor;
}

// --- --- BASE TYPE --- --- --- --- --- --- --- ---
// A base class to make the magic work
// ---

template <typename Derived>
class SerializedType {
  using StringStorage = small_vector<char, 512>;

 public:
  // Stores an offset into `strings_` member variable
  class StringPtr {
   public:
    StringPtr() = default;
    StringPtr(const StringPtr&) = default;
    StringPtr(std::string_view v) { *this = v; }

    explicit operator bool() const { return offset_; }
    const char* operator*() const {
      auto& data = *thread_local_strings_;
      return &data.at(offset_);
    }

    StringPtr operator=(std::string_view v) {
      assert(offset_ == 0);  // Cannot reassign StringPtr
      auto& data = *thread_local_strings_;
      offset_ = data.size();
      data.insert(data.end(), v.begin(), v.end());
      data.push_back('\0');
      return *this;
    }
    bool operator==(std::string_view v) const { return **this == v; }
    bool operator!=(std::string_view v) const { return **this != v; }
    friend bool operator==(std::string_view v, StringPtr s) { return s == v; }
    friend bool operator!=(std::string_view v, StringPtr s) { return s != v; }

   private:
    uint16_t offset_{};
  };

 public:
  SerializedType() {
        // Only one request allowed on the stack"
    assert(thread_local_strings_ == nullptr);
    thread_local_strings_ = &strings_;
    // Add this null terminator so a default-constructed
    // StringPtr will resolve to the empty string
    strings_.push_back('\0');
  }
  SerializedType(const SerializedType<Derived>&) = delete;
  ~SerializedType() { thread_local_strings_ = nullptr; }

  void clear() {
    Clear clr;
    ArchiveReader<Clear> ar{clr};
    ar(static_cast<Derived&>(*this));
    // Add this null terminator so a default-constructed
    // StringPtr will resolve to the empty string
    strings_.push_back('\0');
  }

 protected:
  StringStorage strings_;

 private:
  // this is a pointer to strings_, just above, and it is used
  // to associate StringPtr with this instance
  inline static thread_local StringStorage* thread_local_strings_{nullptr};

 private:
  // Declaring operator new and delete as deleted is not spec compliant.
  // Therefore declare them private instead to disable dynamic alloc
  void* operator new(size_t size);
  void* operator new[](size_t size);
  void operator delete(void*, size_t);
  void operator delete[](void*, size_t);
};

}  // namespace marshalling
}  // namespace darr
