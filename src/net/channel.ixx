module;

// TODO[@zmeadows][P0]: add top-level comment

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
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
concept ProtocolFor = requires(P p, IO& io) {
  { p.on_read(io) } -> std::same_as<void>;
};

export namespace tskv::net {

template <class Proto>
class ChannelIO;

template <class Proto>
concept Protocol = ProtocolFor<Proto, ChannelIO<Proto>>;

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
    return socket_state_ == SocketState::Running;
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

  // server-specific behavior (will be simple echo at first)
  void handle_read_event() noexcept
  {
    auto out_span = tx_buf_.writable_span();

    const std::size_t byte_count = rx_buf_.read_into(out_span);

    tx_buf_.commit(byte_count);
  }

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
  }

  std::size_t flush_tx_buffer()
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

  std::size_t fill_rx_buffer()
  {
    if (!can_read()) {
      return 0;
    }

    std::size_t bytes_received = 0;

    while (true) {
      TSKV_INVARIANT(!rx_buf_.full(), "no backpressure handling implemented yet");

      std::span<std::byte> recv_span = rx_buf_.writable_span();

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
          case EAGAIN: {
            return bytes_received;
          }
          case EINTR: {
            continue;
          }
          default: {
            socket_state_ = SocketState::Aborting;
            handle_error_event();
            return bytes_received;
          }
        }
      }
    }
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

    if (event_mask & (EPOLLERR | EPOLLHUP)) {
      socket_state_ = SocketState::Aborting;
      handle_error_event();
      return;
    }

    if ((event_mask & EPOLLIN) && fill_rx_buffer() > 0) {
      handle_read_event();
    }

    if ((event_mask & EPOLLOUT) && socket_state_ != SocketState::Aborting) {
      (void)flush_tx_buffer();
    }

    if (event_mask & EPOLLRDHUP) {
      TSKV_INVARIANT(socket_state_ == SocketState::Draining, "Should have hit EOF on recv");
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

struct ChannelPool {
private:
  struct Chunk {
    static constexpr std::size_t CHUNK_SIZE = 256;
    static_assert(CHUNK_SIZE <= std::numeric_limits<std::uint16_t>::max(),
      "CHUNK_SIZE too large for uint16_t indices");

    Channel slots_[CHUNK_SIZE]{};

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

    [[nodiscard]] Channel* acquire() noexcept
    {
      TSKV_INVARIANT(free_top_ > 0, "acquire with full Chunk");
      const std::uint16_t next_free_idx = free_stack_[--free_top_];
      return &slots_[next_free_idx];
    }

    void release(Channel* cptr) noexcept
    {
      const std::uint16_t idx = static_cast<std::uint16_t>(cptr - slots_);
      TSKV_INVARIANT(idx < CHUNK_SIZE, "Channel doesn't live in this Chunk");
      TSKV_INVARIANT(free_top_ < CHUNK_SIZE, "free_top_ overflow");
      free_stack_[free_top_++] = idx;
    }
  };

  struct Handle {
    Chunk*   chunk   = nullptr;
    Channel* channel = nullptr;
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

  [[nodiscard]] Channel* lookup(int fd) const noexcept
  {
    if (auto it = active_.find(fd); it != active_.end()) [[likely]] {
      return it->second.channel;
    }
    return nullptr;
  }

  // CONTRACT: returned Channel* only valid between acquire(fd) and release(fd)
  [[nodiscard]] Channel* acquire(int fd)
  {
    assert(fd != -1 && "INVALID ARGS: poisoned socket file descriptor (fd = -1)");

    if (nonfull_chunks_.empty()) [[unlikely]] {
      allocate_new_chunk();
    }

    Chunk* chunk = nonfull_chunks_.back();

    Channel* channel = chunk->acquire();

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
};

} // namespace tskv::net
