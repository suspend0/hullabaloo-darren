Serializable objects for IPC use cases
============

This implements a pattern for object serialization which
which allows objects to be expressively defined in code.

This is a technology demonstration, not a library.

There's no code generation step like exist in protobuf
or flatbuffers, and all calls are static and can be
optimized and inlined by the compiler.

The core constraints are that the system is built around
the trait `std::is_trivially_copyable`, and it uses a
`thread_local` instance to store strings.

The use cases for this are pretty restricted, but if your
client and server are tightly coupled and you don't want
a code generation step in your build or a dependency on
an external library, then this can be a reasonable solution.

The code uses template meta programming to select a
serialization strategy based on if the type is
trivially copyable, is a container of trivially
copyable types.  If neither, the user can provide
a `marshal` method.

There are a number of libraries which use a similar
pattern to this:

  - https://github.com/eyalz800/serializer
  - https://yasli.bitbucket.io/
  - https://uscilab.github.io/cereal/

