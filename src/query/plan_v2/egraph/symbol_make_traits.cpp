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

#include "query/plan_v2/egraph/symbol_make_traits.hpp"

namespace memgraph::query::plan::v2 {

auto symbol_make_traits<symbol::Once>::make(storage_type &s) -> pcore::LoweredNode {
  return {.children = {}, .disambiguator = s.counter++};
}

auto symbol_make_traits<symbol::Symbol>::make(storage_type &s, int32_t pos, std::string_view name)
    -> pcore::LoweredNode {
  s.store.try_emplace(pos, std::string{name});
  return {.children = {}, .disambiguator = static_cast<uint64_t>(pos)};
}

auto symbol_make_traits<symbol::Literal>::make(storage_type &s, storage::ExternalPropertyValue const &value)
    -> pcore::LoweredNode {
  auto [it, inserted] = s.store.try_emplace(value, s.info.size());
  if (inserted) s.info.push_back(&it->first);
  return {.children = {}, .disambiguator = it->second};
}

auto symbol_make_traits<symbol::ParamLookup>::make(storage_type & /*s*/, int32_t pos) -> pcore::LoweredNode {
  return {.children = {}, .disambiguator = static_cast<uint64_t>(pos)};
}

auto symbol_make_traits<symbol::Bind>::make(storage_type & /*s*/, pcore::EClassId input, pcore::EClassId sym,
                                            pcore::EClassId expr) -> pcore::LoweredNode {
  return {.children = utils::small_vector{input, sym, expr}, .disambiguator = std::nullopt};
}

auto symbol_make_traits<symbol::Identifier>::make(storage_type & /*s*/, pcore::EClassId sym) -> pcore::LoweredNode {
  return {.children = utils::small_vector{sym}, .disambiguator = std::nullopt};
}

auto symbol_make_traits<symbol::Output>::make(storage_type & /*s*/, utils::small_vector<pcore::EClassId> children)
    -> pcore::LoweredNode {
  return {.children = std::move(children), .disambiguator = std::nullopt};
}

auto symbol_make_traits<symbol::NamedOutput>::make(storage_type &s, std::string_view name, pcore::EClassId sym,
                                                   pcore::EClassId expr) -> pcore::LoweredNode {
  auto [it, inserted] = s.store.try_emplace(std::string{name}, s.info.size());
  if (inserted) s.info.emplace_back(it->first);
  return {.children = utils::small_vector{sym, expr}, .disambiguator = it->second};
}

auto symbol_make_traits<symbol::Function>::storage_type::intern(std::string_view name) -> uint64_t {
  auto [it, inserted] = store.try_emplace(std::string{name}, info.size());
  if (inserted) {
    info.push_back(FunctionInfo{.name = it->first, .kind = BuiltinKindFor(name)});
  }
  return it->second;
}

auto symbol_make_traits<symbol::Function>::make(storage_type &s, std::string_view name,
                                                utils::small_vector<pcore::EClassId> args) -> pcore::LoweredNode {
  return {.children = std::move(args), .disambiguator = s.intern(name)};
}

auto symbol_make_traits<symbol::Unwind>::make(storage_type & /*s*/, pcore::EClassId input, pcore::EClassId sym,
                                              pcore::EClassId list_expr) -> pcore::LoweredNode {
  return {.children = utils::small_vector{input, sym, list_expr}, .disambiguator = std::nullopt};
}

auto symbol_make_traits<symbol::Subquery>::make(storage_type & /*s*/, utils::small_vector<pcore::EClassId> children)
    -> pcore::LoweredNode {
  return {.children = std::move(children), .disambiguator = std::nullopt};
}

}  // namespace memgraph::query::plan::v2
