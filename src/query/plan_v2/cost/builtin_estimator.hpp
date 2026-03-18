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

#pragma once

#include "query/plan_v2/cost/cardinality_estimator.hpp"
#include "query/plan_v2/egraph/builtin_functions.hpp"

namespace memgraph::query::plan::v2 {

struct egraph;  // public e-graph facade

/// Estimator for built-in Cypher functions.
///
/// `Estimate` is a two-level dispatch:
///   1. on `enode.symbol()` - currently only `Function` is modelled; every
///      other symbol falls through to kDefaultRowEstimate.
///   2. for `Function`, on the cached `BuiltinKind` (set up by
///      symbol_make_traits<symbol::Function>) - each recognised builtin has a
///      dedicated `Estimate<Name>` helper; `Unknown` (UDFs / unrecognised
///      builtins) returns kDefaultRowEstimate.
///
/// Adding a new builtin: add a `BuiltinKind` enumerator, an `Estimate<Name>`
/// helper, and one switch case in `EstimateFunction`.
///
/// The estimator looks up FunctionInfo through the public e-graph facade
/// (which the cost model passes alongside the EGraph for the e-class walk).
/// The constructor takes that facade by reference so the estimator can find
/// the right interner without round-tripping through the cost model.
struct BuiltinEstimator final : CardinalityEstimator {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
  egraph const &facade;

  explicit BuiltinEstimator(egraph const &f);

  auto Estimate(planner::core::ENode<symbol> const &enode, std::span<planner::core::EClassId const> arg_eclasses) const
      -> double override;
};

}  // namespace memgraph::query::plan::v2
