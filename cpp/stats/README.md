StatsD Stats
=============

Implements easy-to-use stats which register themselves on
construction, buffer their updates and then emit on a
schedule.

The goal is to have stats that are easy at point-of-use,
and cost the minimum amount at runtime.  Each stat update
is lock free

Create some stats.  These are often static or thread local,
but can be any long-lived object:  even in a map:

    // these three emit two stats: "requests" and "requests.total"
    // Users can use filtering with "requests" to look at the type,
    // or look at "requests.total" to see total of all request types
    darr::stats::Counter f1_requests{"requests#req_type:f1"};
    darr::stats::Counter r1_requests{"requests#req_type:r1"};
    darr::stats::Counter n1_requests{"requests#req_type:n1"};

Use those stats from your code:

    // the stat update is one atomic increment
    void some_method() {
      ++f1_requests;
    }

Write a client for the stats system.  With StatsD this is where
you'd send a UDP packet.

    struct TestClient : stats::Client {
      virtual void count(std::string_view name, uint64_t value) {
        std::cout << "C:" << name << " " << value << "\n";
      }
      [snip]
    };

Then start sending stats data to the client on a schedule:

    // keep this emitter alive for the life of the app
    auto emitter = stats::start_publishing(
      client, std::chrono::seconds(7));

There is a global lock in stats, which is acquired on construction
and destruction of a stats object, and well a during emit.  Use of
the stats themselves is an atomic op.  High volume stats can be
`thread_local` to shard the memory access

