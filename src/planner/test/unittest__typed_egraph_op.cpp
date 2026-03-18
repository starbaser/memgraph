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

#include <vector>

#include <gtest/gtest.h>

#include "test_support/op_make_traits.hpp"

namespace memgraph::planner::core::test {
namespace {

TEST(TypedEGraph_Op, NullaryLeafHashCons) {
  TypedTestEGraph eg;
  auto const a1 = eg.Make<Op::A>();
  auto const a2 = eg.Make<Op::A>();
  auto const b = eg.Make<Op::B>();
  EXPECT_EQ(a1, a2);
  EXPECT_NE(a1, b);
  EXPECT_EQ(eg.core().num_classes(), 2u);
}

TEST(TypedEGraph_Op, UnaryNode) {
  TypedTestEGraph eg;
  auto const x = eg.Make<Op::Var>();
  auto const neg1 = eg.Make<Op::Neg>(x);
  auto const neg2 = eg.Make<Op::Neg>(x);
  EXPECT_EQ(neg1, neg2);
  EXPECT_EQ(eg.core().num_classes(), 2u);
}

TEST(TypedEGraph_Op, BinaryNode) {
  TypedTestEGraph eg;
  auto const x = eg.Make<Op::Var>();
  auto const y = eg.Make<Op::Const>();
  auto const sum_1 = eg.Make<Op::Add>(x, y);
  auto const sum_2 = eg.Make<Op::Add>(x, y);
  // Add is not commutative at the structural level.
  auto const sum_swapped = eg.Make<Op::Add>(y, x);
  EXPECT_EQ(sum_1, sum_2);
  EXPECT_NE(sum_1, sum_swapped);
  EXPECT_EQ(eg.core().num_classes(), 4u);
}

TEST(TypedEGraph_Op, TernaryNode) {
  TypedTestEGraph eg;
  auto const a = eg.Make<Op::A>();
  auto const b = eg.Make<Op::B>();
  auto const c = eg.Make<Op::C>();
  auto const bind_1 = eg.Make<Op::Bind>(a, b, c);
  auto const bind_2 = eg.Make<Op::Bind>(a, b, c);
  EXPECT_EQ(bind_1, bind_2);
  EXPECT_EQ(eg.core().num_classes(), 4u);
}

TEST(TypedEGraph_Op, NaryNodeFromVector) {
  TypedTestEGraph eg;
  auto const a = eg.Make<Op::A>();
  auto const b = eg.Make<Op::B>();
  auto const c = eg.Make<Op::C>();
  auto const f_1 = eg.Make<Op::F>(std::vector<EClassId>{a, b, c});
  auto const f_2 = eg.Make<Op::F>(std::vector<EClassId>{a, b, c});
  auto const f_diff_order = eg.Make<Op::F>(std::vector<EClassId>{c, b, a});
  EXPECT_EQ(f_1, f_2);
  EXPECT_NE(f_1, f_diff_order);
}

TEST(TypedEGraph_Op, EquivalentTreesUnderRawEmplace) {
  // Sanity: TypedEGraph::Make and raw core().emplace agree on hash-consing.
  TypedTestEGraph eg;
  auto const x = eg.Make<Op::Var>();
  auto const y = eg.Make<Op::Const>();
  auto const sum_typed = eg.Make<Op::Add>(x, y);

  auto const sum_raw = eg.core().emplace(Op::Add, utils::small_vector<EClassId>{x, y}).eclass_id;
  EXPECT_EQ(sum_typed, sum_raw);
}

}  // namespace
}  // namespace memgraph::planner::core::test
