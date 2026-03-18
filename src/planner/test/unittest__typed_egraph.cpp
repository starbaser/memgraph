// Copyright 2026 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "utils/small_vector.hpp"

import memgraph.planner.core.egraph;
import memgraph.planner.core.typed_egraph;

namespace memgraph::planner::core {
namespace {

/// Synthetic symbol set covering the three trait shapes the protocol must
/// support: nullary leaf with no storage, leaf that interns user data via
/// a disambiguator, and an N-ary node with children.
enum class ToyOp : std::uint8_t {
  Once,    // nullary, no storage, no disambiguator
  Symbol,  // leaf, interns (name) -> id used as disambiguator
  Add,     // binary, no storage, no disambiguator
};

struct ToyAnalysis {};

template <ToyOp S>
struct toy_traits;

template <>
struct toy_traits<ToyOp::Once> {
  struct storage_type {};

  static auto make(storage_type & /*s*/) -> LoweredNode { return {.children = {}, .disambiguator = std::nullopt}; }
};

template <>
struct toy_traits<ToyOp::Symbol> {
  struct storage_type {
    std::map<std::string, std::uint64_t> by_name;
    std::uint64_t next_id = 0;
  };

  static auto make(storage_type &s, std::string_view name) -> LoweredNode {
    auto [it, inserted] = s.by_name.try_emplace(std::string{name}, s.next_id);
    if (inserted) {
      ++s.next_id;
    }
    return {.children = {}, .disambiguator = it->second};
  }
};

template <>
struct toy_traits<ToyOp::Add> {
  struct storage_type {};

  static auto make(storage_type & /*s*/, EClassId lhs, EClassId rhs) -> LoweredNode {
    return {.children = utils::small_vector<EClassId>{lhs, rhs}, .disambiguator = std::nullopt};
  }
};

using ToySeq = SymbolSequence<ToyOp, ToyOp::Once, ToyOp::Symbol, ToyOp::Add>;
using ToyEGraph = TypedEGraph<ToyOp, ToyAnalysis, ToySeq, toy_traits>;

TEST(TypedEGraph, MakeLeafWithoutDisambiguator) {
  ToyEGraph eg;
  auto const a = eg.Make<ToyOp::Once>();
  auto const b = eg.Make<ToyOp::Once>();
  // Same leaf with no disambiguator hash-conses to the same e-class.
  EXPECT_EQ(a, b);
  EXPECT_EQ(eg.core().num_classes(), 1u);
}

TEST(TypedEGraph, MakeLeafInternsByName) {
  ToyEGraph eg;
  auto const x1 = eg.Make<ToyOp::Symbol>(std::string_view{"x"});
  auto const x2 = eg.Make<ToyOp::Symbol>(std::string_view{"x"});
  auto const y = eg.Make<ToyOp::Symbol>(std::string_view{"y"});
  EXPECT_EQ(x1, x2);
  EXPECT_NE(x1, y);
  EXPECT_EQ(eg.core().num_classes(), 2u);
}

TEST(TypedEGraph, MakeBinaryNodeWithChildren) {
  ToyEGraph eg;
  auto const x = eg.Make<ToyOp::Symbol>(std::string_view{"x"});
  auto const y = eg.Make<ToyOp::Symbol>(std::string_view{"y"});
  auto const xy_1 = eg.Make<ToyOp::Add>(x, y);
  auto const xy_2 = eg.Make<ToyOp::Add>(x, y);
  EXPECT_EQ(xy_1, xy_2);
  // x, y, Add(x,y): three classes.
  EXPECT_EQ(eg.core().num_classes(), 3u);
}

TEST(TypedEGraph, StorageAccessReadsInternedNames) {
  ToyEGraph eg;
  eg.Make<ToyOp::Symbol>(std::string_view{"alice"});
  eg.Make<ToyOp::Symbol>(std::string_view{"bob"});

  auto const &store = eg.storage<ToyOp::Symbol>();
  EXPECT_EQ(store.next_id, 2u);
  EXPECT_TRUE(store.by_name.contains("alice"));
  EXPECT_TRUE(store.by_name.contains("bob"));
}

TEST(TypedEGraph, CoreAccessorReachesUnderlyingEGraph) {
  ToyEGraph eg;
  eg.Make<ToyOp::Once>();
  EGraph<ToyOp, ToyAnalysis> const &core = eg.core();
  EXPECT_EQ(core.num_classes(), 1u);
  EXPECT_EQ(core.num_nodes(), 1u);
}

// Compile-time check: a traits type whose make() returns the wrong type
// must be rejected by SymbolMakeTraits at the constraint.
struct BadTraits {
  struct storage_type {};

  static auto make(storage_type &) -> int { return 0; }
};

static_assert(!SymbolMakeTraits<BadTraits>, "make() returning non-LoweredNode should fail the concept");

// Compile-time check: a well-formed nullary trait satisfies the concept.
static_assert(SymbolMakeTraits<toy_traits<ToyOp::Once>>);

}  // namespace
}  // namespace memgraph::planner::core
