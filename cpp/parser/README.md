Static Parser for Delimited Text
============

This implements a static parser for delimited text
which allows grammars to be expressively defined.  All
alls are static and can be optimized and inlined by
the compiler.

The advantage of this is the speed and that grammars can
be defined in code and can be validated against the imput
text.  The disadvantage is patterns are hard coded, there
is no branching or validation, and it's a DSL that one
must learn.

The following defines a parser which parses slash-delimited
list of strings (like a URL) into an object.

    PathParse p = {path};
    start_parse(p, person, "/")        //
        / &Person::mutable_first_name  //
        / &Person::mutable_last_name   //
        / optional(&Person::set_age)   //
        ;

It works by implementing `operator/()` for a few method
signatures, and then doing string-to-object conversion
to call the object's setters.

For example `/ &Person::mutable_last_name` will call

    PathContext<Person> operator/(
      PathContext<Person>& ctx,      // our object
      std::string* (Person::*)()     // function pointer
    )

And then it's just recursively defined from there.  It's
probably best to start at the bottom of `parser.cc` and
work upwards.

