#pragma once

#include "marshalling.h"
#include "small_vector.h"

namespace darr {

template <typename T, size_t N = 16>
using Vector = small_vector<T, N>;

struct IPAddress {
  using ipv4_type = std::array<uint8_t, 4>;
  using ipv6_type = std::array<uint8_t, 16>;
  std::variant<ipv4_type, ipv6_type> data;
};

struct SomeRequest : marshalling::SerializedType<SomeRequest> {
  // --- Nested Types --- --- --- ---
  struct Query {
    StringPtr method;
    StringPtr type;
    StringPtr prefix;
  };
  struct Requirements {
    StringPtr worker_id;
    StringPtr external_settings;
  };
  struct Provider {
    uint32_t id;
    StringPtr name;
  };
  struct Header {
    StringPtr name, value;
  };
  struct Location {
    IPAddress ip_address;
    uint32_t market, country, region, state, asn;
    StringPtr market_iso, country_iso, region_code, state_code;
  };

  // --- Data --- --- --- ---
  Query query;
  Requirements requirements;
  Location location;
  Vector<Provider> providers;
  Vector<Header> headers;

  // --- Serialization --- --- --- ---
  template <typename Archive>
  void marshal(Archive& ar) {
    ar(query, requirements, location, providers, headers, strings_);
  }
  template <typename Archive>
  void marshal(Archive& ar) const {
    ar(query, requirements, location, providers, headers, strings_);
  }
};

// -- Response
struct SomeResponse : marshalling::SerializedType<SomeResponse> {
  // --- Nested Types --- --- --- ---
  struct InitResponse {
    Vector<StringPtr> providers;
    Vector<StringPtr> origins;
    bool is_special;
    template <typename Archive>
    void marshal(Archive& ar) {
      ar(providers, origins, is_special);
    }
    template <typename Archive>
    void marshal(Archive& ar) const {
      ar(providers, origins, is_special);
    }
  };
  struct RequestResponse {
    uint32_t ttl{};
    uint32_t code{};
  };
  struct Answer {
    StringPtr answer;
    bool ok;
  };

  // --- Data --- --- --- ---
  uint32_t exec_time_micros{0};
  StringPtr reason_code;
  StringPtr exception;
  InitResponse init_response;
  RequestResponse response;
  Vector<Answer> answers;
  Vector<StringPtr> reason_log;

  // --- Serialization --- --- --- ---
  template <typename Archive>
  void marshal(Archive& ar) {
    ar(exec_time_micros,        //
       reason_code, exception,  //
       init_response, answers,  //
       reason_log,              //
       strings_);
  }
  template <typename Archive>
  void marshal(Archive& ar) const {
    ar(exec_time_micros,        //
       reason_code, exception,  //
       init_response, answers,  //
       reason_log,              //
       strings_);
  }
};

namespace marshalling {
template <typename StreamT>
StreamT& operator<<(StreamT& os,
                    const SerializedType<::darr::SomeRequest>::StringPtr& str) {
  os << *str;
  return os;
}
template <typename StreamT>
StreamT& operator<<(
    StreamT& os, const SerializedType<::darr::SomeResponse>::StringPtr& str) {
  os << *str;
  return os;
}

}  // namespace marshalling
}  // namespace darr
