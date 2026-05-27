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

#include <fmt/format.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace memgraph::logging {

// Per-session log state. Lives on whatever owns the session (today: the
// query::Interpreter). Mutated only by the thread currently executing the
// session's work; read by the wrapper under the RAII guard installed by
// Session::Execute().
//
// The log level itself is global (configured on spdlog's default logger
// via --log_level) — per-session level would require consulting
// per-session state on every log call site, including ones on
// background threads that have no session in scope. The session context
// carries only the tag prefix and per-session toggles for emitters that
// already run on a thread with the guard active (today: the query-trace
// stream; future: slow-query / failed-query loggers).
//
// Performance note: the "[session=...] [user=...] [tx=...]" prefix is composed
// on demand only on the emit path (which is gated by trace_enabled), straight
// into the reusable per-thread assembly buffer. The common trace-off path
// touches none of this — setters just store their field.
class SessionLogContext {
 public:
  // Controls whether structured query-trace events (parse/plan/exec/commit
  // markers in the interpreter) are emitted at all. When true,
  // EmitSessionTraceEvent() emits the event tagged with the session prefix.
  // SET SESSION TRACE ON toggles this via SetTraceEnabled().
  //
  // Emission is at INFO so it respects --log-level normally: operators
  // debugging a session run with --log-level=INFO (or lower) and see the
  // events; at WARN+ the events are filtered like any other INFO line.
  void SetTraceEnabled(bool enabled) noexcept { trace_enabled_.store(enabled, std::memory_order_relaxed); }

  bool trace_enabled() const noexcept { return trace_enabled_.load(std::memory_order_relaxed); }

  // Tag-field setters. Writes happen on the session-owning thread (auth, SET
  // SESSION TRACE, tx begin/commit/abort); the fields are read on the same
  // thread under the ScopedSessionLog guard when the prefix is composed at emit
  // time. Bolt task-pool handoff between messages provides happens-before
  // across thread switches.
  void SetSessionUuid(std::string uuid) { session_uuid_ = std::move(uuid); }

  void SetUser(std::string user) { user_ = std::move(user); }

  void ClearUser() { user_.clear(); }

  void SetTxId(std::string tx_id) { tx_id_ = std::move(tx_id); }

  void ClearTxId() { tx_id_.clear(); }

  std::string_view session_uuid() const noexcept { return session_uuid_; }

  // Compose "[session=..] [user=..] [tx=..]" into `out`, omitting empty fields
  // (no empty tokens, no trailing space). Called only on the emit path, which
  // is gated by trace_enabled, so the trace-off path does zero prefix work.
  void AppendPrefixTo(fmt::memory_buffer &out) const {
    AppendTag(out, "[session=", session_uuid_);
    AppendTag(out, "[user=", user_);
    AppendTag(out, "[tx=", tx_id_);
  }

 private:
  static void AppendTag(fmt::memory_buffer &out, std::string_view label, std::string_view value) {
    if (value.empty()) return;
    if (out.size() != 0) out.push_back(' ');
    out.append(label.data(), label.data() + label.size());
    out.append(value.data(), value.data() + value.size());
    out.push_back(']');
  }

  std::string session_uuid_;
  std::string user_;
  std::string tx_id_;
  std::atomic<bool> trace_enabled_{false};
};

namespace detail {
// Inlined thread_local so every translation unit that includes this header
// shares the same storage without needing an out-of-line definition.
inline thread_local SessionLogContext *current_session_log = nullptr;

// Per-thread reusable buffer for assembling "<prefix> <message>" without
// per-emit heap allocation. Reused across log calls on the same thread.
inline thread_local fmt::memory_buffer log_assembly_buf;
}  // namespace detail

// RAII guard. Construct at the top of Session::Execute() with a pointer to
// the session's SessionLogContext (or nullptr if there isn't one yet, e.g.
// pre-auth). Destructor restores the prior context — supports nested guards
// for correctness, although in normal flow there is only one level.
class ScopedSessionLog {
 public:
  explicit ScopedSessionLog(SessionLogContext *ctx) noexcept : prev_(detail::current_session_log) {
    detail::current_session_log = ctx;
  }

  ~ScopedSessionLog() noexcept { detail::current_session_log = prev_; }

  ScopedSessionLog(const ScopedSessionLog &) = delete;
  ScopedSessionLog &operator=(const ScopedSessionLog &) = delete;
  ScopedSessionLog(ScopedSessionLog &&) = delete;
  ScopedSessionLog &operator=(ScopedSessionLog &&) = delete;

  // Returns the current thread's session context, or nullptr if none active.
  static SessionLogContext *Current() noexcept { return detail::current_session_log; }

 private:
  SessionLogContext *prev_;
};

// True when the current thread has an active session context with SET SESSION
// TRACE ON. Use this to gate construction of *expensive* trace arguments at the
// call site (JSON / plan-tree dumps, etc.) before calling EmitSessionTraceEvent:
// function arguments are evaluated before the gate inside EmitSessionTraceEvent
// runs, so an expensive argument would otherwise be built even when tracing is
// off. For cheap arguments the inner gate is enough — no outer check needed.
inline bool IsSessionTraceEnabled() noexcept {
  auto *ctx = ScopedSessionLog::Current();
  return ctx != nullptr && ctx->trace_enabled();
}

// Emit a structured query-trace event. Gated by the session's trace_enabled
// toggle (SET SESSION TRACE ON) and by --log-level (events emit at INFO).
// Prepends the [session=...] [user=...] [tx=...] tag prefix.
//
// Takes a compile-time-checked format string + arguments (the spdlog/fmt
// idiom) so the fmt::format work runs only after the trace gate passes.
// Do NOT pre-format the argument with fmt::format() at the call site: that
// would pay the formatting (and any allocation) cost even when tracing is off.
template <typename... Args>
void EmitSessionTraceEvent(fmt::format_string<Args...> fmt_str, Args &&...args) {
  auto *ctx = ScopedSessionLog::Current();
  if (ctx == nullptr || !ctx->trace_enabled()) return;
  auto &buf = detail::log_assembly_buf;
  buf.clear();
  ctx->AppendPrefixTo(buf);
  if (buf.size() != 0) buf.push_back(' ');
  fmt::format_to(std::back_inserter(buf), fmt_str, std::forward<Args>(args)...);
  spdlog::default_logger_raw()->log(spdlog::level::info, std::string_view{buf.data(), buf.size()});
}

}  // namespace memgraph::logging
