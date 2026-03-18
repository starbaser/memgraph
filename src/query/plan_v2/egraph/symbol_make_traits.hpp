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

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include "query/plan_v2/egraph/builtin_functions.hpp"
#include "query/plan_v2/egraph/symbol.hpp"
#include "storage/v2/property_value.hpp"
#include "utils/small_vector.hpp"

import memgraph.planner.core.egraph;
import memgraph.planner.core.typed_egraph;

namespace memgraph::query::plan::v2 {

namespace pcore = memgraph::planner::core;

// ========================================================================
// symbol_make_traits - per-symbol lowering for the TypedEGraph protocol.
//
// Each specialisation provides:
//   - storage_type: per-symbol side-data (interning maps, counters, ...);
//     empty struct if none.
//   - static auto make(storage_type&, user_args...) -> pcore::LoweredNode:
//     lowers user arguments to (children, optional disambiguator).
//
// The protocol talks in raw pcore::EClassId; the plan_v2 strong-typed
// `eclass` is converted at the facade boundary in egraph.cpp.
// ========================================================================

template <symbol S>
struct symbol_make_traits;

/// Once: auto-incrementing counter
template <>
struct symbol_make_traits<symbol::Once> {
  struct storage_type {
    uint64_t counter = 0;
  };

  static auto make(storage_type &s) -> pcore::LoweredNode;
};

/// Symbol: position -> name mapping
template <>
struct symbol_make_traits<symbol::Symbol> {
  struct storage_type {
    std::map<int32_t, std::string> store;
  };

  static auto make(storage_type &s, int32_t pos, std::string_view name) -> pcore::LoweredNode;
};

/// Literal: value <-> id mapping.  `store` (value -> id) is the hash-consing
/// direction used by `make`; `info` (id -> value, indexed by disambiguator)
/// is the reverse used by the Builder and BuiltinEstimator.  `info` holds
/// pointers into `store`'s node-stable keys -- no key duplication, valid as
/// long as `store` outlives the reads.  Kept in lockstep by `make`.
template <>
struct symbol_make_traits<symbol::Literal> {
  struct storage_type {
    std::map<storage::ExternalPropertyValue, uint64_t> store;
    std::vector<storage::ExternalPropertyValue const *> info;
  };

  static auto make(storage_type &s, storage::ExternalPropertyValue const &value) -> pcore::LoweredNode;
};

/// ParamLookup: no storage, position IS the disambiguator
template <>
struct symbol_make_traits<symbol::ParamLookup> {
  struct storage_type {};

  static auto make(storage_type &, int32_t pos) -> pcore::LoweredNode;
};

/// Bind: no storage, just children
template <>
struct symbol_make_traits<symbol::Bind> {
  struct storage_type {};

  static auto make(storage_type &, pcore::EClassId input, pcore::EClassId sym, pcore::EClassId expr)
      -> pcore::LoweredNode;
};

/// Identifier: no storage, just child
template <>
struct symbol_make_traits<symbol::Identifier> {
  struct storage_type {};

  static auto make(storage_type &, pcore::EClassId sym) -> pcore::LoweredNode;
};

/// Output: no storage, prepends input to children
template <>
struct symbol_make_traits<symbol::Output> {
  struct storage_type {};

  static auto make(storage_type &, utils::small_vector<pcore::EClassId> children) -> pcore::LoweredNode;
};

/// NamedOutput: name <-> id mapping + children.  `store` (name -> id) is the
/// hash-consing direction; `info` (id -> name, indexed by disambiguator) is
/// the reverse used by the Builder.  `info` holds string_views into `store`'s
/// node-stable keys -- no name duplication, valid as long as `store` outlives
/// the reads.  Kept in lockstep by `make`.
template <>
struct symbol_make_traits<symbol::NamedOutput> {
  struct storage_type {
    std::map<std::string, uint64_t> store;
    std::vector<std::string_view> info;
  };

  static auto make(storage_type &s, std::string_view name, pcore::EClassId sym, pcore::EClassId expr)
      -> pcore::LoweredNode;
};

/// Function: name -> id mapping, with BuiltinKind cached at insertion time
/// so the cost model and estimator can dispatch on an array lookup instead
/// of a per-call string compare.  Children are the argument e-classes; the
/// disambiguator is the function id, namespace-shared between builtins and
/// UDFs.
template <>
struct symbol_make_traits<symbol::Function> {
  struct storage_type {
    /// name -> id, mirroring NamedOutput / Symbol / Literal's `store`
    /// convention so the Builder picks up the same field name across all
    /// interner-backed traits.
    boost::unordered::unordered_flat_map<std::string, uint64_t> store;
    /// id -> FunctionInfo (parallel to `store`'s id range).  Read by the
    /// estimator and the Builder to recover the exact name + cached
    /// BuiltinKind without re-classifying.  Kept in lockstep with `store`
    /// by `intern` - external code must not insert into either directly.
    std::vector<FunctionInfo> info;

    /// Intern a function name, classifying its BuiltinKind on first sight.
    /// Returns the stable id for that name; identical names always return
    /// the same id.  This is the only place `store` and `info` are written,
    /// keeping the parallel-array invariant local to the trait.
    auto intern(std::string_view name) -> uint64_t;
  };

  static auto make(storage_type &s, std::string_view name, utils::small_vector<pcore::EClassId> args)
      -> pcore::LoweredNode;
  // args is the complete children list (just the function arguments).
};

/// Unwind: no storage, mirrors Bind's [input, sym, list_expr] shape so the
/// resolver's alive-Bind dispatch covers Unwind without a second branch.
template <>
struct symbol_make_traits<symbol::Unwind> {
  struct storage_type {};

  static auto make(storage_type &, pcore::EClassId input, pcore::EClassId sym, pcore::EClassId list_expr)
      -> pcore::LoweredNode;
};

/// Subquery: no storage; children are [outer_input, inner_root, exposed_syms...].
/// Variadic to encode the projection set of the inner block as direct e-graph
/// children, so the cost case and resolver don't have to peek into inner_root's
/// enode shape to discover what crosses the scope barrier.
template <>
struct symbol_make_traits<symbol::Subquery> {
  struct storage_type {};

  static auto make(storage_type &, utils::small_vector<pcore::EClassId> children) -> pcore::LoweredNode;
};

/// Binary operator: no storage, just two children
template <symbol S>
  requires(is_binary_op_v<S>)
struct symbol_make_traits<S> {
  struct storage_type {};

  static auto make(storage_type & /*s*/, pcore::EClassId lhs, pcore::EClassId rhs) -> pcore::LoweredNode {
    return {.children = utils::small_vector{lhs, rhs}, .disambiguator = std::nullopt};
  }
};

/// Unary operator: no storage, just one child
template <symbol S>
  requires(is_unary_op_v<S>)
struct symbol_make_traits<S> {
  struct storage_type {};

  static auto make(storage_type & /*s*/, pcore::EClassId operand) -> pcore::LoweredNode {
    return {.children = utils::small_vector{operand}, .disambiguator = std::nullopt};
  }
};

}  // namespace memgraph::query::plan::v2
