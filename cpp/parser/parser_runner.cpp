#include "parser.h"

#include <iostream>

namespace darr {
void run_parse(std::string_view txt) {
  Person p = {};
  bool ok = parse_person(p, txt);
  std::cout << txt << "\n"                                  //
            << "     ok: " << std::boolalpha << ok << "\n"  //
            << "  first: " << p.first_name() << "\n"        //
            << "   last: " << p.last_name() << "\n"         //
            << "    age: " << p.age() << "\n";
}
void run() {
  // These will succeed b/c all three fields are set
  run_parse("/Christopher/Robin/5");
  run_parse("/Tigger/Tiger/44");
  // This will also succeed b/c the age is optional
  run_parse("/Tyler/Robin");
  // This will fail b/c last name is a required field
  run_parse("/Piglet");
  // This will fail to parse b/c "Poo" is not an int
  run_parse("/Winnie/the/Poo");
}
}  // namespace darr

int main() {
  std::cout << "starting\n";

  darr::run();
}
