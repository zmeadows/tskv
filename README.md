# ⏳︎ tskv — Time-Series Key-Value Store

**TL;DR:** Single-node, crash-safe time-series KV store with a non-blocking TCP server (Linux **epoll**) and LSM-style storage: **Write-Ahead Log (WAL) → memtable → immutable SSTables**, plus a background compaction worker. Built with **C++23 modules**; no third-party libraries.

---

## ◎ Goals
- Demonstrate disciplined systems design in modern C++.
- Show clear **durability** boundaries (WAL append / optional sync) and **read-after-write** visibility.
- Keep **backpressure** and buffers **bounded** for predictable latency.
- Favor **correctness + measurable performance** over feature breadth.

## ⛶ Architecture
- **Write path:** append to **WAL** → (optional `fdatasync`) → apply to **memtable** → periodic **flush** to **SSTable** (immutable, sorted).
- **Read path:** **memtable** first → then newest-to-oldest **SSTables**; per-file **Bloom filter** to skip negatives; index to jump to the right block.
- **Compaction:** merge overlapping SSTables, keep newest versions, drop obsolete ones; install via **manifest** with durable rename.

## ⚑ Roadmap (high-level)
- [ ] v0.1 — Bootstrap: README, roadmap, PR template
- [ ] v0.2 — Non-blocking TCP + epoll echo; clean shutdown
- [ ] v0.3 — Framing: header + length; PING/PONG
- [ ] v0.4 — Connection state: RX/TX rings; backpressure cap
- [ ] v0.5 — Engine queues: SPSC/MPSC; dispatcher
- [ ] v0.6 — WAL v1: append+CRC; sync policy flag
- [ ] v0.7 — Recovery: replay WAL; torn-tail safe
- [ ] v0.8 — Memtable v0: std::map; PUT/GET end-to-end
- [ ] v0.9 — SSTable v1: writer/reader; mmap; footer
- [ ] v0.10 — Manifest: live tables; durable rename
- [ ] v0.11 — Wire-through: GET/PUT via SST path
- [ ] v0.12 — Bloom filters: per-SST; bits/key tuning
- [ ] v0.13 — Memtable v1: skiplist + iterator
- [ ] v0.14 — SCAN RPC: streaming RESP; writev batches
- [ ] v0.15 — Concurrency: N I/O, M engine; fairness
- [ ] v0.16 — Metrics: counters + p50/p95/p99 endpoint
- [ ] v0.17 — Compaction v1: merge + manifest install
- [ ] v0.18 — Chaos tests: disk-full; kill-9 loops
- [ ] v0.19 — Perf pass: micro/macro benches; notes
- [ ] v1.0 — Polish: docs, demo.sh, ASan/UBSan; release

## ∷ C++ Module Layout
- `tskv.common.*` — logging, metrics, ring buffers, fs helpers
- `tskv.net.*` — socket (non-blocking), reactor (**epoll**, edge-triggered), connection, rpc
- `tskv.kv.*` — engine, wal, memtable, sstable, manifest, compaction, filters

## ∑ Metrics (planned)
- **net:** connections_open, rx_bytes_total, tx_bytes_total, backpressure_events_total
- **rpc:** put_total, get_total, scan_total, errors_total
- **wal/sstable:** appends_total, fsync_total, files_total, bloom_negative_total
- **latency:** p50/p95/p99 for GET & PUT
