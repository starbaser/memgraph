# Copyright 2026 Memgraph Ltd.
#
# Use of this software is governed by the Business Source License
# included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
# License, and you may not use this file except in compliance with the Business Source License.
#
# As of the Change Date specified in that file, in accordance with
# the Business Source License, use of this software will be governed
# by the Apache License, Version 2.0, included in the file
# licenses/APL.txt.

import glob
import os
import re
import sys
import time

import mgclient
import pytest

# The e2e harness starts the instance with --log-file at <build>/e2e/logs/<log_file>
# (the cluster's log_file key in workloads.yaml). This file is copied to
# <build>/tests/e2e/session_trace_log/, so the build dir is three levels up.
# spdlog's daily_file_sink stamps the date into the name (e.g. ..._2026-05-27.log);
# there is one *active* file (older dated files from past runs may linger, so pick
# the newest). This is daily rotation — not per-session files.
_BUILD_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
LOG_GLOB = os.path.join(_BUILD_DIR, "e2e", "logs", "session_trace_log*.log")

SESSION_TAG = re.compile(r"\[session=([^\]]+)\]")


def _active_log_path():
    candidates = glob.glob(LOG_GLOB)
    assert candidates, f"memgraph did not create a log file matching {LOG_GLOB}"
    return max(candidates, key=os.path.getmtime)


def _connect():
    conn = mgclient.connect(host="localhost", port=7687)
    conn.autocommit = True
    return conn


def _run(conn, query):
    cur = conn.cursor()
    cur.execute(query)
    try:
        return cur.fetchall()
    except mgclient.DatabaseError:
        return []


def _enable_trace(conn):
    # The handler returns this session's uuid (result header: "session uuid").
    return _run(conn, "SET SESSION TRACE ON")[0][0]


def _read_appended(log_path, start_offset):
    # Sync logger flushes on every record; small grace for the OS to surface the
    # appended bytes. Reading only from start_offset keeps stale content (the log
    # file is reused across runs) out of the assertions.
    time.sleep(0.2)
    with open(log_path, "r") as f:
        f.seek(start_offset)
        return f.read().splitlines()


def _tags_by_session(lines):
    """Group the [session=<uuid>]-tagged lines by their session uuid."""
    by_session = {}
    for line in lines:
        m = SESSION_TAG.search(line)
        if m:
            by_session.setdefault(m.group(1), []).append(line)
    return by_session


def test_sessions_interleave_into_one_tagged_log():
    """The refactor's thesis: every session writes to ONE log file, disambiguated
    by a [session=<uuid>] tag rather than per-session files. Two traced sessions'
    query-trace events land in the same file, each attributed to its own session;
    a third, untraced session is never tagged."""
    log_path = _active_log_path()
    start_offset = os.path.getsize(log_path)

    a, b, c = _connect(), _connect(), _connect()
    uuid_a = _enable_trace(a)
    uuid_b = _enable_trace(b)
    # c stays untraced.

    _run(a, "RETURN 'marker_alpha'")
    _run(b, "RETURN 'marker_beta'")
    _run(c, "RETURN 'marker_gamma'")

    by_session = _tags_by_session(_read_appended(log_path, start_offset))

    # Both traced sessions appear in the single file; the untraced one does not.
    assert set(by_session) == {uuid_a, uuid_b}, f"unexpected tagged sessions: {set(by_session)}"

    a_lines = "\n".join(by_session[uuid_a])
    b_lines = "\n".join(by_session[uuid_b])

    # Each session's query is attributed to its own tag, never the other's.
    assert "marker_alpha" in a_lines and "marker_alpha" not in b_lines
    assert "marker_beta" in b_lines and "marker_beta" not in a_lines
    # The untraced session's query never reaches the tagged trace stream.
    assert "marker_gamma" not in a_lines and "marker_gamma" not in b_lines


def test_set_session_trace_off_stops_emission():
    """SET SESSION TRACE OFF turns the stream back off: a query run after it
    produces no tagged trace lines for the session."""
    log_path = _active_log_path()
    start_offset = os.path.getsize(log_path)

    conn = _connect()
    uuid = _enable_trace(conn)
    _run(conn, "RETURN 'marker_while_on'")

    _run(conn, "SET SESSION TRACE OFF")
    _run(conn, "RETURN 'marker_while_off'")

    own = "\n".join(_tags_by_session(_read_appended(log_path, start_offset)).get(uuid, []))

    assert "marker_while_on" in own, "a query run while trace was ON should be tagged"
    assert "marker_while_off" not in own, "a query run after SET SESSION TRACE OFF must not be tagged"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-rA"]))
