#include <charconv>
#include <chrono>
#include <optional>

#include "parser.h"

/*
 * This class implements a static parser for delimited text
 * which allows grammars to be expressively defined.  All
 * calls are static and can be optimized and inlined by
 * the compiler.
 *
 * The following defines a parser which parses slash-delimited
 * list of strings (like a URL) into an object.
 *
 *     PathParse p = {path};
 *     start_parse(p, person, "/")        //
 *         / &Person::mutable_first_name  //
 *         / &Person::mutable_last_name   //
 *         / optional(&Person::set_age)   //
 *         ;
 *
 * It works by implementing `operator/()` for a few method
 * signatures, and then doing string-to-object conversion
 * to call the object's setters.
 *
 * For example "/ &Person::mutable_last_name" will call
 *
 *    PathContext<Person> operator/(
 *      PathContext<Person>& ctx,      // our object
 *      std::string* (Person::*)()     // function pointer
 *    )
 *
 * And then it's just recursively defined from there.  It's
 * probably best to start at the bottom of the file and
 * work upwards.
 */

namespace {

//
// --- CONVERTERS ----------------
//
// Functions which convert raw text to
//
template <typename T>
std::optional<T> try_to(std::string_view);

template <>
std::optional<std::string> try_to(std::string_view segment) {
  return std::string{segment};
}
template <>
std::optional<uint32_t> try_to(std::string_view segment) {
  std::optional<uint32_t> opt{};
  uint32_t v;
  if (auto r = std::from_chars(segment.begin(), segment.end(), v);
      r.ec == std::errc()) {
    opt = v;
  }
  return opt;
}

//
// --- UTIL ----------------
//
bool remove_prefix(std::string_view& tgt, const std::string_view prefix) {
  if (tgt.size() >= prefix.size() &&
      tgt.compare(0, prefix.size(), prefix) == 0) {
    tgt.remove_prefix(prefix.size());
    return true;
  }
  return false;
}

std::string_view split_step(std::string_view& tgt, char delimiter) {
  auto make_sv = [](const char* b, const char* e) {
    size_t sz = std::distance(b, e);
    return std::string_view{b, sz};
  };

  auto it = std::find(tgt.begin(), tgt.end(), delimiter);
  auto result = make_sv(tgt.begin(), it);

  if (it != tgt.end())
    it = std::next(it);
  tgt = make_sv(it, tgt.end());

  return result;
}

//
// --- TYPES -----------------
//

// PathParse: holds the path as it's consumed
struct PathParse {
  std::string_view path;
  bool ok{true};
};

// ParseContext: binds the parse to a target object
template <typename TargetT>
struct ParseContext {
  PathParse& parse;
  TargetT& target;
};

// IntegralField / StringField: a pointer to a field
template <typename T, typename V,
          typename std::enable_if<std::is_integral<V>::value, int>::type = 0>
using IntegralFn = void (T::*)(V);
template <typename T, typename V,
          typename std::enable_if<std::is_integral<V>::value, int>::type = 0>
struct IntegralField {
  IntegralField(void (T::*fn)(V), bool req) : setter{fn}, required{req} {}
  IntegralFn<T, V> setter;
  bool required{true};
};

// IntegralField / StringField: a pointer to a field
template <typename T>
using StringFn = std::string* (T::*)();
template <typename T>
struct StringField {
  StringField(std::string* (T::*fn)(), bool req) : getter{fn}, required{req} {}
  StringFn<T> getter;
  bool required{true};
};

//
// --- FACTORIES ----------------
//

template <typename T>
ParseContext<T> parse_into(PathParse& parse, T& target) {
  return {parse, target};
}
template <typename T, size_t N>
ParseContext<T> start_parse(PathParse& parse, T& target,
                            const char (&prefix)[N]) {
  parse.ok = remove_prefix(parse.path, prefix);
  return parse_into(parse, target);
}
template <typename T, typename V>
auto required(IntegralFn<T, V> setter) {
  return IntegralField<T, V>(setter, true);
}
template <typename DefT>
auto required(DefT&& t) {
  return std::forward<DefT>(t);  // required is default
}
template <typename T, typename V>
auto optional(IntegralFn<T, V> setter) {
  return IntegralField<T, V>(setter, false);
}
template <typename T>
auto optional(StringFn<T> getter) {
  return StringField<T>(getter, false);
}

//
// --- PARSERS ----------------
// These take a segment of the path and set the field
// in the protobuf object
// ---
//

#define CALL_MEMBER_FN(object, ptrToMember) ((object).*(ptrToMember))
template <typename T, typename V>
void parse_segment(ParseContext<T>& ctx, IntegralField<T, V> fld,
                   std::string_view segment) {
  if (ctx.parse.ok && !segment.empty()) {
    if (auto v = try_to<V>(segment)) {
      CALL_MEMBER_FN(ctx.target, fld.setter)(*v);
    } else {
      ctx.parse.ok = false;
    }
  } else if (fld.required) {
    ctx.parse.ok = false;
  }
}

template <typename T>
void parse_segment(ParseContext<T>& ctx, StringField<T>& fld,
                   std::string_view segment) {
  if (ctx.parse.ok && !segment.empty()) {
    auto* str = CALL_MEMBER_FN(ctx.target, fld.getter)();
    str->assign(segment.begin(), segment.end());
  } else if (fld.required) {
    ctx.parse.ok = false;
  }
}
#undef CALL_MEMBER_FN

//
// --- OPERATORS ------------------
//

template <typename T, typename FieldT1, typename FieldT2>
ParseContext<T>& operator/(ParseContext<T>& ctx,
                           std::pair<FieldT1, FieldT2> pair) {
  auto s = split_step(ctx.parse.path, '/');  // pull "a,b" from "a,b/c/d"
  parse_segment(ctx, pair.first, split_step(s, ','));  // pull "a" from "a,b"
  parse_segment(ctx, pair.second, s);
  return ctx;
}

template <typename T, typename FieldT>
ParseContext<T>& operator/(ParseContext<T>& ctx, FieldT&& fld) {
  parse_segment(ctx, fld, split_step(ctx.parse.path, '/'));
  return ctx;
}

template <typename T, typename V>
ParseContext<T>& operator/(ParseContext<T>& ctx, IntegralFn<T, V> setter) {
  return ctx / IntegralField<T, V>{setter, true};  // required by default
}

template <typename T>
ParseContext<T>& operator/(ParseContext<T>& ctx, StringFn<T> getter) {
  return ctx / StringField<T>{getter, true};  // required by default
}

template <typename T, typename SetterT>
ParseContext<T>& operator/(ParseContext<T>&& ctx, SetterT&& setter) {
  return ctx / std::forward<SetterT>(setter);
}
}  // namespace

namespace darr {

bool parse_person(Person& person, std::string_view path) {
  PathParse p = {path};
  start_parse(p, person, "/")        //
      / &Person::mutable_first_name  //
      / &Person::mutable_last_name   //
      / optional(&Person::set_age)   //
      ;

  return p.ok;
}

}  // namespace darr
