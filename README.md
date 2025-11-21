# ⏳︎ tskv — Time-Window Time-Series Key-Value Cache

**TL;DR:** `tskv` is a single-node, in-memory **time-window cache** for time-series data, with:

- A simple binary protocol over a non-blocking TCP server (Linux **epoll**)
- A bounded sliding retention window (e.g., "last N minutes")
- Time-partitioned in-memory segments for efficient expiration
- Modern **C++23** with modules
- No third-party libraries

Stage-0 (`v1.0`) focuses on a small, understandable core: a hot time window with clear semantics and basic observability, leaving WAL, multi-threading, and advanced features for future versions.

---

## ◎ Goals

- Provide a compact, readable example of a **time-window time-series cache**:
  - Bounded memory via a fixed retention window
  - Explicit "visible data" contract: recent data only
- Demonstrate disciplined systems design in **modern C++**:
  - C++23 modules
  - Non-blocking I/O with **epoll**
- Favor **correctness + clear invariants** over feature breadth:
  - Simple, explicit write and read paths
  - Straightforward retention / expiration logic
- Keep latency and resource usage **predictable**:
  - No unbounded growth from infinite history
  - Easy-to-reason-about hot path

---

## ⛶ Architecture (Stage-0)

### Data model

- Keys are time-series identifiers (e.g. `cpu.user`, `service=api,host=foo`).
- Each write unit is a tuple `(series_id, timestamp, value)`.
- The store maintains only data within a **sliding time window**:
  - `timestamp >= now - WINDOW`
  - Older data is considered expired and is eventually dropped.

### In-memory layout

- Data is stored in **time-partitioned segments**:
  - Each segment covers a fixed time slice (e.g. 1–10 seconds).
  - Segments are organized in time order (oldest → newest).
  - New writes go to the current "tail" segment.
- A periodic retention pass drops the oldest segments whose time range is fully outside the configured window.

This keeps memory and data size bounded by `WINDOW`, not by total insert volume.

### Network path

- Single-node server using **non-blocking TCP** and **epoll**.
- Simple length-prefixed framing for requests and responses.
- Initial RPCs:
  - `PING / PONG` for connectivity checks.
  - `PUT_TS_AT(series_id, timestamp, value)` to append a point.
  - `GET_TS_LATEST(series_id)` to fetch the latest point in-window.
  - `GET_TS_RANGE(series_id, from_ts, to_ts)` to read points for a series over a time range (clipped to the window).

---

## ⚑ Roadmap

### Implemented

- [x] **v0.1 — Bootstrap**
  - README, minimal CLI stub, basic project layout
  - PR template and basic coding conventions

- [x] **v0.2 — Non-blocking TCP**
  - Non-blocking server with **epoll**
  - Basic echo handler for manual testing
  - Clean shutdown path

### Planned to v1.0 (Stage-0)

- [ ] **v0.3 — Framing + basic RPCs**
  - Length-prefixed request/response framing
  - `PING` / `PONG` and error responses
  - Skeleton handlers for time-series commands

- [ ] **v0.4 — In-memory window store v0**
  - Global `WINDOW` config (e.g. last N minutes)
  - Single in-memory container for `(series_id, timestamp, value)`
  - Simple, periodic cleanup of expired entries
  - `PUT_TS_AT` + `GET_TS_LATEST` end-to-end

- [ ] **v0.5 — Time-partitioned segments**
  - Replace the single container with fixed-duration segments
  - Append writes to the current segment
  - Drop whole segments when they fall completely out of window
  - Basic `GET_TS_RANGE(series_id, from_ts, to_ts)` over segments

- [ ] **v0.6 — Window-aware introspection**
  - `WINDOW_INFO` RPC:
    - Window size, number of segments
    - Approximate memory usage
  - Debug dump of segments and series counts

- [ ] **v0.7 — Metrics**
  - Simple counters and gauges:
    - `ts_put_total`, `ts_get_latest_total`, `ts_range_total`, `ts_errors_total`
    - `window_segments`, `window_series_approx`, `window_points_approx`
  - Text or simple binary metrics endpoint/command

- [ ] **v0.8 — Indexing pass (per-segment)**
  - Optional per-segment index:
    - `series_id -> offsets`
  - Speed up `GET_TS_LATEST` and `GET_TS_RANGE` without scanning all entries
  - Microbenchmarks for lookup vs. scan

- [ ] **v0.9 — Reliability + perf pass**
  - Basic property tests for window semantics:
    - Writes with timestamps < `now - WINDOW` are never visible
    - Writes with timestamps in the window remain visible
  - Simple load generator for PUT/GET/GET_TS_RANGE
  - First round of notes on throughput/latency

- [ ] **v1.0 — Stage-0 release**
  - Minimal but complete time-window cache:
    - Protocol, window store, segments, indexing, basic metrics
  - Documentation:
    - Architecture overview
    - Wire protocol reference
    - Example usage script (`demo.sh`)
  - Sanitizers in CI (ASan/UBSan) and a small test suite

---

## ∷ C++ Module Layout (Stage-0)

- `tskv.common.*`
  - Logging, basic metrics types, time helpers
  - Small ring buffers / utility containers
- `tskv.net.*`
  - Socket wrapper (non-blocking)
  - Reactor (**epoll**, edge-triggered)
  - Connection and RPC framing
- `tskv.window.*`
  - In-memory segments
  - Window management (retention, expiration)
  - Query execution (latest / range)
  - Simple per-segment indexing

---

## ∑ Metrics (Stage-0, planned)

- **net:**
  - `net_connections_open`
  - `net_rx_bytes_total`
  - `net_tx_bytes_total`

- **rpc:**
  - `rpc_ping_total`
  - `ts_X_total` (for all RPCs `X`)
  - `rpc_errors_total`

- **window:**
  - `window_segments`
  - `window_points_approx`
  - `window_series_approx`

- **latency (sample-based, coarse):**
  - p50 / p95 / p99 for:
    - `PUT_TS_AT`
    - `GET_TS_LATEST`
    - `GET_TS_RANGE`

---

## After v1.0 (ideas, not committed)

These are potential extensions beyond the stage-0 scope:

- **Durability for the hot window**
  - WAL segments per time-partitioned segment
  - Startup replay to reconstruct the last N minutes after a crash

- **Multi-threading**
  - Split I/O and engine into separate threads
  - Shard data across multiple engine threads by series id

 **Protocol extensions**
  - `PUT_TS_BATCH` for multiple irregular/sparse timestamps
  - `PUT_TS_STEPS` for multiple regularly spaced timestamps
  - `GET_TS_AT` for exact-timestamp lookups

- **Richer time-series semantics**
  - Per-series TTL overrides
  - Server-side aggregates:
    - Windowed `SUM` / `AVG` / `MIN` / `MAX` RPCs

- **Advanced observability**
  - More detailed metrics (per-series or per-connection)
  - Debug RPCs to inspect segment contents, hot keys, etc.

- **Replication / HA experiments**
  - Simple follower replication for the time window
  - Eventually-consistent read replicas

Stage-0 (`v1.0`) stays deliberately small: a single-node, in-memory time-window cache with a clear contract and straightforward implementation. Everything else can grow out of that foundation.
