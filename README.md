# ⏳︎ tskv — Time-Series Key-Value Store (C++23)

**TL;DR:** Single-node, crash-safe time-series KV store with a non-blocking TCP server (Linux **epoll**) and LSM-style storage: **Write-Ahead Log (WAL) → memtable → immutable SSTables**, plus a background compaction worker. Built with **C++23 modules**; no third-party libraries.

---

## ◎ Goals
- Demonstrate modern **C++23 modules** and disciplined systems design.
- Show clear **durability** boundaries (WAL append / optional sync) and **read-after-write** visibility.
- Keep **backpressure** and buffers **bounded** for predictable latency.
- Favor **correctness + measurable performance** over feature breadth.

## ⛶ Architecture (60 seconds)
- **Write path:** append to **WAL** → (optional `fdatasync`) → apply to **memtable** → periodic **flush** to **SSTable** (immutable, sorted).
- **Read path:** **memtable** first → then newest→oldest **SSTables**; per-file **Bloom filter** to skip negatives; index to jump to the right block.
- **Compaction:** merge overlapping SSTables, keep newest versions, drop obsolete ones; install via **manifest** with durable rename.

## ∷ C++ Module Layout
- `tskv.common.*` — logging, metrics, ring buffers, fs helpers
- `tskv.net.*` — socket (non-blocking), reactor (**epoll**, edge-triggered), connection, rpc
- `tskv.kv.*` — engine, wal, memtable, sstable, manifest, compaction, filters

## ⚑ Roadmap (high-level)
- [ ] v0.2 — Reactor bring-up: non-blocking TCP + epoll echo, clean shutdown
- [ ] v0.5 — Durability MVP: WAL + recovery; PUT acknowledges per policy
- [ ] v0.8 — End-to-end GET/PUT via memtable + first SSTable
- [ ] v1.0 — Compaction + docs/demo + metrics baseline

## ∑ Metrics (planned)
- **net:** connections_open, rx_bytes_total, tx_bytes_total, backpressure_events_total
- **rpc:** put_total, get_total, scan_total, errors_total
- **wal/sstable:** appends_total, fsync_total, files_total, bloom_negative_total
- **latency:** p50/p95/p99 for GET & PUT

---

**Notes:**
- **epoll** = Linux readiness API for scalable non-blocking I/O.
- **WAL** = Write-Ahead Log (append-only, for durability before applying in memory).
- **SSTable** = immutable, on-disk sorted table with index (+ optional Bloom filter).

