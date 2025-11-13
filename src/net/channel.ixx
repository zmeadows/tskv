module;

// TODO[@zmeadows][P0]: add top-level comment for v0.2

#include <cassert>
#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <netdb.h>
#include <numeric>
#include <span>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tskv/common/attributes.hpp"
#include "tskv/common/logging.hpp"

export module tskv.net.channel;

import tskv.common.buffer;
import tskv.common.logging;
import tskv.common.metrics;

namespace tc      = tskv::common;
namespace metrics = tskv::common::metrics;

inline std::size_t ceil_div(std::size_t x, std::size_t y)
{
  assert(y != 0 && "INVALID ARGS: divide by zero (y)");
  return x / y + (x % y != 0);
}

template <class P, class IO>
concept ProtocolFor = requires(P p, IO& io, int ec) {
  { p.on_read(io) } -> std::same_as<void>;
  { p.on_error(io, ec) } -> std::same_as<void>;
  { p.on_close(io) } -> std::same_as<void>;
};

export namespace tskv::net {

enum class SendResult : std::uint8_t { Full, Partial, Closed };

template <class Proto>
class ChannelIO;

template <class Proto>
concept Protocol = ProtocolFor<Proto, ChannelIO<Proto>>;

// TODO[@zmeadows][P0]: re-organize public/private data/method order
template <Protocol Proto>
struct Channel {
private:
  static constexpr auto TX_BUF_SIZE = 4096;
  static constexpr auto RX_BUF_SIZE = 4096;

  tc::SimpleBuffer<TX_BUF_SIZE> tx_buf_{};
  tc::SimpleBuffer<RX_BUF_SIZE> rx_buf_{};

  enum class SocketState : std::uint8_t {
    Running, // normal
    Draining, // EOF, flushing TX
    Aborting, // dropping immediately
    Closed
  };

  [[nodiscard]] inline bool can_read() const noexcept
  {
    return socket_state_ == SocketState::Running && !rx_buf_.full();
  }

  [[nodiscard]] inline bool can_write() const noexcept
  {
    const bool valid_state =
      socket_state_ == SocketState::Running || socket_state_ == SocketState::Draining;
    const bool have_data = !tx_buf_.empty();
    return valid_state && have_data;
  }

  int fd_ = -1;

  SocketState socket_state_ = SocketState::Closed;

  Proto proto_;

  friend class ChannelIO<Proto>; // allow IO façade to access internals

  TSKV_COLD_PATH void handle_error_event() noexcept
  {
    int       err = 0;
    socklen_t len = sizeof(err);
    const int opc = getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len);

    if (opc != 0 || err == 0) {
      return;
    }

    std::error_code ec(err, std::generic_category());
    TSKV_LOG_WARN(ec.message());

    metrics::inc_counter<"net.socket_error.total">();

    switch (err) {
      case ECONNRESET:
        metrics::inc_counter<"net.socket_error.econnreset">();
        break;
      case ETIMEDOUT:
        metrics::inc_counter<"net.socket_error.etimedout">();
        break;
      case EPIPE:
        metrics::inc_counter<"net.socket_error.epipe">();
        break;
      case ENETDOWN:
        metrics::inc_counter<"net.socket_error.enetdown">();
        break;
      default:
        metrics::inc_counter<"net.socket_error.other">();
        break;
    }

    ChannelIO<Proto> io(*this);
    proto_.on_error(io, err);
  }

  std::size_t try_flush_tx_buffer()
  {
    if (!can_write()) {
      return 0;
    }

    std::size_t bytes_sent = 0;

    while (true) {
      std::span<const std::byte> tx_span = tx_buf_.readable_span();

      const ssize_t send_rc = send(fd_, tx_span.data(), tx_span.size(), 0);

      if (send_rc > 0) {
        tx_buf_.consume(send_rc);
        bytes_sent += send_rc;
        if (tx_buf_.empty()) {
          return bytes_sent;
        }
      }
      else if (send_rc == 0) {
        return bytes_sent;
      }
      else if (send_rc == -1) {
        switch (errno) {
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
          case EWOULDBLOCK:
#endif
          case EAGAIN: {
            return bytes_sent;
          }
          case EINTR: {
            continue;
          }
          default: {
            socket_state_ = SocketState::Aborting;
            handle_error_event();
            return bytes_sent;
          }
        }
      }

      return 0;
    }
  }

  std::size_t try_fill_rx_buffer()
  {
    if (!can_read()) {
      return 0;
    }

    std::size_t bytes_received = 0;

    while (!rx_buf_.full()) {
      std::span<std::byte> recv_span = rx_buf_.writable_span();
      assert(!recv_span.empty());

      const ssize_t recv_rc = recv(fd_, recv_span.data(), recv_span.size(), 0);

      if (recv_rc > 0) {
        rx_buf_.commit(recv_rc);
        bytes_received += recv_rc;
      }
      else if (recv_rc == 0) {
        socket_state_ = SocketState::Draining;
        return bytes_received;
      }
      else if (recv_rc == -1) {
        switch (errno) {
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
          case EWOULDBLOCK:
#endif
          case EAGAIN:
            return bytes_received;
          case EINTR:
            continue;
          default: {
            socket_state_ = SocketState::Aborting;
            handle_error_event();
            return bytes_received;
          }
        }
      }
    }

    std::unreachable();
  }

  [[nodiscard]] TSKV_INLINE std::span<const std::byte> rx_span() const noexcept
  {
    return rx_buf_.readable_span();
  }

  TSKV_INLINE void rx_consume(std::size_t nbytes) noexcept { rx_buf_.consume(nbytes); }

  [[nodiscard]] std::pair<std::size_t, SendResult> tx_send(std::span<const std::byte> data) noexcept
  {
    if (socket_state_ == SocketState::Closed) [[unlikely]] {
      return {0, SendResult::Closed};
    }

    const std::size_t bytes_queued = tx_buf_.write_from(data);

    return {bytes_queued, bytes_queued == data.size() ? SendResult::Full : SendResult::Partial};
  }

public:
  // CONTRACT: fd is a valid/open socket file descriptor
  void attach(int client_fd) noexcept
  {
    fd_ = client_fd;
    tx_buf_.clear();
    rx_buf_.clear();
    socket_state_ = SocketState::Running;
  }

  void detach() noexcept
  {
    fd_ = -1;
    tx_buf_.clear();
    rx_buf_.clear();
    socket_state_ = SocketState::Closed;
  }

  void notify_close() noexcept
  {
    ChannelIO<Proto> io(*this);
    proto_.on_close(io);
  }

  void begin_shutdown() noexcept
  {
    if (socket_state_ == SocketState::Running) {
      socket_state_ = SocketState::Draining;

      // TODO[@zmeadows][P0]: why *not* do this?
      // stop reading at kernel level; we already guard reads via can_read()
      // ::shutdown(fd_, SHUT_RD);
    }
  }

  [[nodiscard]] inline int fd() const noexcept { return fd_; }

  [[nodiscard]] inline bool should_close() const noexcept
  {
    if (socket_state_ == SocketState::Aborting)
      return true;
    if (socket_state_ == SocketState::Draining && tx_buf_.empty())
      return true;
    return false;
  }

  void handle_events(std::uint32_t event_mask)
  {
    assert(socket_state_ != SocketState::Closed && "handling events on closed socket");

    if (event_mask & EPOLLERR) {
      socket_state_ = SocketState::Aborting;
      handle_error_event();
      return;
    }

    ChannelIO<Proto> io(*this);

    // Drain readable side fully, respecting ET semantics
    if (event_mask & (EPOLLIN | EPOLLHUP | EPOLLRDHUP)) {
      for (;;) {
        // 1) Pull everything we can (until EAGAIN or buffer full)
        const std::size_t nrecv = try_fill_rx_buffer();
        metrics::add_counter<"net.bytes_received">(nrecv);

        // 2) If the rx buffer has data to process, let the protocol process/consume it
        const std::size_t rx_used_before = rx_buf_.used_space();
        if (!rx_buf_.empty()) {
          proto_.on_read(io); // expected to rx_consume()
        }
        const bool proto_consumed = rx_buf_.used_space() < rx_used_before;

        // 3) Try to flush any responses as we go (good for backpressure)
        if (can_write()) {
          (void)try_flush_tx_buffer();
        }

        // 4) Stop when we made no forward progress
        const bool rx_blocked = nrecv == 0; // either EAGAIN or not allowed to read
        if (rx_blocked && !proto_consumed) {
          break;
        }
      }
    }

    // Pure-write wakeup from epoll
    if (event_mask & EPOLLOUT) {
      const std::size_t nsent = try_flush_tx_buffer();
      metrics::add_counter<"net.bytes_sent">(nsent);
    }

    if (event_mask & (EPOLLHUP | EPOLLRDHUP) && socket_state_ == SocketState::Running) {
      // Peer won’t send more. If we already hit recv() == 0, try_fill_rx_buffer()
      // may have already set Draining; otherwise, do it here.
      socket_state_ = SocketState::Draining;
    }
  }

  [[nodiscard]] std::uint32_t desired_events() const noexcept
  {
    std::uint32_t mask{0};
    if (can_read())
      mask |= EPOLLIN;
    if (can_write())
      mask |= EPOLLOUT;
    return mask;
  }
};

template <class Proto>
class ChannelIO {
public:
  ChannelIO() = delete;
  explicit ChannelIO(Channel<Proto>& ch) : ch_(ch) {}

  [[nodiscard]] TSKV_INLINE std::span<const std::byte> rx_span() const noexcept
  {
    return ch_.rx_span();
  }

  [[nodiscard]] TSKV_INLINE std::pair<std::size_t, SendResult> tx_send(
    std::span<const std::byte> data) noexcept
  {
    return ch_.tx_send(data);
  }

  TSKV_INLINE void rx_consume(std::size_t nbytes) noexcept { ch_.rx_consume(nbytes); }

private:
  Channel<Proto>& ch_;
};

struct EchoProtocol {
  void on_read(ChannelIO<EchoProtocol>& io)
  {
    auto rx_bytes = io.rx_span();

    const auto [bytes_sent, result] = io.tx_send(rx_bytes);
    if (bytes_sent > 0) [[likely]] {
      io.rx_consume(bytes_sent);
    }
  }

  void on_error(ChannelIO<EchoProtocol>&, int) {}
  void on_close(ChannelIO<EchoProtocol>&) {}
};

template <Protocol Proto>
struct ChannelPool {
private:
  struct Chunk {
    static constexpr std::size_t CHUNK_SIZE = 256;
    static_assert(CHUNK_SIZE <= std::numeric_limits<std::uint16_t>::max(),
      "CHUNK_SIZE too large for uint16_t indices");

    Channel<Proto> slots_[CHUNK_SIZE]{};

    // free_stack_[0..free_top_-1] contains indices of currently free slots.
    std::uint16_t free_stack_[CHUNK_SIZE]{};
    std::uint16_t free_top_ = 0;

    Chunk()
    {
      std::iota(free_stack_, free_stack_ + CHUNK_SIZE, 0);
      free_top_ = CHUNK_SIZE;
    }

    Chunk(const Chunk&)            = delete;
    Chunk& operator=(const Chunk&) = delete;

    [[nodiscard]] inline bool full() const noexcept { return free_top_ == 0; }

    [[nodiscard]] Channel<Proto>* acquire() noexcept
    {
      TSKV_INVARIANT(free_top_ > 0, "acquire with full Chunk");
      const std::uint16_t next_free_idx = free_stack_[--free_top_];
      return &slots_[next_free_idx];
    }

    void release(Channel<Proto>* cptr) noexcept
    {
      const std::uint16_t idx = static_cast<std::uint16_t>(cptr - slots_);
      TSKV_INVARIANT(idx < CHUNK_SIZE, "Channel doesn't live in this Chunk");
      TSKV_INVARIANT(free_top_ < CHUNK_SIZE, "free_top_ overflow");
      free_stack_[free_top_++] = idx;
    }
  };

  struct Handle {
    Chunk*          chunk   = nullptr;
    Channel<Proto>* channel = nullptr;
  };

  void allocate_new_chunk()
  {
    chunks_.emplace_back(std::make_unique<Chunk>());
    nonfull_chunks_.push_back(chunks_.back().get());
  }

  std::vector<std::unique_ptr<Chunk>> chunks_;
  std::vector<Chunk*>                 nonfull_chunks_;
  std::unordered_map<int, Handle>     active_;

public:
  ChannelPool(std::size_t initial_capacity = Chunk::CHUNK_SIZE)
  {
    active_.max_load_factor(0.8);
    active_.reserve(5 * initial_capacity / 4);

    if (initial_capacity > 0) {
      const std::size_t chunks_required = ceil_div(initial_capacity, Chunk::CHUNK_SIZE);
      chunks_.reserve(chunks_required);
      nonfull_chunks_.reserve(chunks_required);

      for (std::size_t ichunk = 0; ichunk < chunks_required; ++ichunk) {
        allocate_new_chunk();
      }
    }
  }

  ChannelPool(const ChannelPool&)            = delete;
  ChannelPool& operator=(const ChannelPool&) = delete;

  ~ChannelPool() { TSKV_INVARIANT(active_.empty(), "destroyed ChannelPool with active channels"); }

  [[nodiscard]] Channel<Proto>* lookup(int fd) const noexcept
  {
    if (auto it = active_.find(fd); it != active_.end()) [[likely]] {
      return it->second.channel;
    }
    return nullptr;
  }

  [[nodiscard]] std::size_t empty() const noexcept { return active_.empty(); }

  // CONTRACT: returned Channel* only valid between acquire(fd) and release(fd)
  [[nodiscard]] Channel<Proto>* acquire(int fd)
  {
    assert(fd != -1 && "INVALID ARGS: poisoned socket file descriptor (fd = -1)");

    if (nonfull_chunks_.empty()) [[unlikely]] {
      allocate_new_chunk();
    }

    Chunk* chunk = nonfull_chunks_.back();

    Channel<Proto>* channel = chunk->acquire();

    try { // emplace can throw
      auto [it, inserted] = active_.emplace(fd, Handle{chunk, channel});

      if (!inserted) [[unlikely]] {
        chunk->release(channel);
        assert(inserted && "INVALID ARGS: fd already present in pool");
        return nullptr;
      }
    }
    catch (const std::exception&) {
      chunk->release(channel);
      throw;
    }

    if (chunk->full()) [[unlikely]] {
      nonfull_chunks_.pop_back();
    }

    return channel;
  }

  // CONTRACT: associated Channel* for fd never used again after release(fd)
  void release(int fd) noexcept
  {
    auto it = active_.find(fd);

    if (it == active_.end()) [[unlikely]] {
      assert(false && "INVALID ARGS: release called with unknown fd");
      return;
    }

    const auto& [chunk, channel] = it->second;

    const bool was_full = chunk->full();
    chunk->release(channel);

    if (was_full) [[unlikely]] {
      nonfull_chunks_.push_back(chunk);
    }

    active_.erase(it);
  }

  template <typename Fn>
  void for_each(Fn&& fn)
  {
    for (auto& [_, handle] : active_) {
      fn(handle.channel);
    }
  }
};
} // namespace tskv::net
