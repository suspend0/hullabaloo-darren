#pragma once

#include <string>

namespace darr {

// This awkward class modeled on protobuf generated objects
class Person {
 public:
  const std::string& first_name() const { return first_name_; }
  std::string* mutable_first_name() { return &first_name_; }

  const std::string& last_name() const { return last_name_; }
  std::string* mutable_last_name() { return &last_name_; }

  uint32_t age() const { return age_; }
  void set_age(uint32_t v) { age_ = v; }

 private:
  std::string first_name_;
  std::string last_name_;
  uint32_t age_{2};  // default age ;)
};


/*
 * Parses a string of the form "/first/last/age" where age
 */
bool parse_person(Person&, std::string_view src);

}  // namespace darr
