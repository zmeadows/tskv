module;

// TODO[@zmeadows][P0]: add top-level comment

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <numeric>
#include <print>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "tskv/common/logging.hpp"

export module tskv.net.connection;

import tskv.common.buffer;
import tskv.common.logging;
namespace tc = tskv::common;

inline std::size_t ceil_div(std::size_t x, std::size_t y)
{
  assert(y != 0 && "INVALID ARGS: divide by zero (y)");
  return x / y + (x % y != 0);
}

export namespace tskv::net {

struct Connection {
private:
  static constexpr auto TX_BUF_SIZE = 2048;
  static constexpr auto RX_BUF_SIZE = 2048;

  tc::SimpleBuffer<TX_BUF_SIZE> tx_buf_{};
  tc::SimpleBuffer<RX_BUF_SIZE> rx_buf_{};

  int  fd_         = -1;
  bool tx_enabled_ = false;
  bool rx_enabled_ = false;

  void reset(int new_fd) noexcept
  {
    fd_ = new_fd;
    tx_buf_.clear();
    rx_buf_.clear();
    tx_enabled_ = true;
    rx_enabled_ = true;
    // TODO[@zmeadows][P2]: in debug mode, zero or tag buffers?
  }

public:
  void attach(int client_fd) noexcept { reset(client_fd); }

  inline bool closed() { return fd_ < 0; }

  void disconnect()
  {
    close(fd_);
    reset(-1);
  }

  std::size_t echo()
  {
    assert(tx_buf_.empty());

    { // 1) Receive client data
      auto recv_span = rx_buf_.writable_span();

      ssize_t recv_count = 0;
      if (recv_count = recv(fd_, recv_span.data(), recv_span.size(), 0); recv_count == -1) {
        TSKV_LOG_INFO("recv");
      }

      if (recv_count == 0) {
        disconnect();
        return 0;
      }

      rx_buf_.commit(recv_count);
    }

    // 2) Dump receive buffer into transmit buffer
    tx_buf_.commit(rx_buf_.read(tx_buf_.writable_span()));

    { // 3) Send bytes back to client
      std::size_t bytes_echoed = 0;

      while (!tx_buf_.empty()) {
        auto tx_span = tx_buf_.readable_span();

        const ssize_t bytes_sent = send(fd_, tx_span.data(), tx_span.size(), 0);

        if (bytes_sent == -1) {
          if (errno == EINTR) {
            continue;
          }
          else if (errno == EPIPE) {
            std::println("client disappeared while sending");
            disconnect();
            return bytes_echoed;
          }
          else {
            TSKV_LOG_CRITICAL("send");
          }
        }

        bytes_echoed += bytes_sent;
        tx_buf_.consume(bytes_sent);
      }

      return bytes_echoed;
    }
  };
};

struct ConnectionPool {
private:
  struct Chunk {
    static constexpr std::size_t CHUNK_SIZE = 256;
    static_assert(CHUNK_SIZE <= std::numeric_limits<std::uint16_t>::max(),
      "CHUNK_SIZE too large for uint16_t indices");

    Connection slots_[CHUNK_SIZE]{};

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

    [[nodiscard]] Connection* acquire() noexcept
    {
      TSKV_INVARIANT(free_top_ > 0, "acquire with full Chunk");
      const std::uint16_t next_free_idx = free_stack_[--free_top_];
      return &slots_[next_free_idx];
    }

    void release(Connection* cptr) noexcept
    {
      const std::uint16_t idx = static_cast<std::uint16_t>(cptr - slots_);
      TSKV_INVARIANT(idx < CHUNK_SIZE, "Connection doesn't live in this Chunk");
      TSKV_INVARIANT(free_top_ < CHUNK_SIZE, "free_top_ overflow");
      free_stack_[free_top_++] = idx;
    }
  };

  struct Handle {
    Chunk*      chunk      = nullptr;
    Connection* connection = nullptr;
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
  ConnectionPool(std::size_t initial_capacity = Chunk::CHUNK_SIZE)
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

  ConnectionPool(const ConnectionPool&)            = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

  ~ConnectionPool()
  {
    // TODO[@zmeadows][P0]: manually release every connection
    assert(active_.empty() && "BUG: destroying ConnectionPool with active connections");
  }

  [[nodiscard]] Connection* lookup(int fd) const noexcept
  {
    if (auto it = active_.find(fd); it != active_.end()) [[likely]] {
      return it->second.connection;
    }
    return nullptr;
  }

  // CONTRACT: returned Connection* only valid between acquire(fd) and release(fd)
  [[nodiscard]] Connection* acquire(int fd)
  {
    assert(fd != -1 && "INVALID ARGS: poisoned socket file descriptor (fd = -1)");

    if (nonfull_chunks_.empty()) [[unlikely]] {
      allocate_new_chunk();
    }

    Chunk* chunk = nonfull_chunks_.back();

    Connection* connection = chunk->acquire();

    try { // emplace can throw
      auto [it, inserted] = active_.emplace(fd, Handle{chunk, connection});

      if (!inserted) [[unlikely]] {
        chunk->release(connection);
        assert(inserted && "INVALID ARGS: fd already present in pool");
        return nullptr;
      }
    }
    catch (const std::exception&) {
      chunk->release(connection);
      throw;
    }

    // TODO[@zmeadows][P0]: attach connection

    if (chunk->full()) [[unlikely]] {
      nonfull_chunks_.pop_back();
    }

    return connection;
  }

  // CONTRACT: associated Connection* for fd never used again after release(fd)
  void release(int fd) noexcept
  {
    auto it = active_.find(fd);

    if (it == active_.end()) [[unlikely]] {
      assert(false && "INVALID ARGS: release called with unknown fd");
      return;
    }

    const auto& [chunk, connection] = it->second;

    // TODO[@zmeadows][P0]: reset connection

    const bool was_full = chunk->full();
    chunk->release(connection);

    if (was_full) [[unlikely]] {
      nonfull_chunks_.push_back(chunk);
    }

    active_.erase(it);
  }
};

} // namespace tskv::net
