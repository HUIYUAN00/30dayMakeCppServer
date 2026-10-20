/**
 * @file Connection.cpp
 */
#include "Connection.h"

#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include "Buffer.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"

namespace {
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
}  // namespace

Connection::Connection(int fd, EventLoop *loop) : loop_(loop) {
  socket_ = std::make_unique<Socket>();
  socket_->set_fd(fd);
  if (loop_ != nullptr) {
    channel_ = std::make_unique<Channel>(fd, loop_);
    // 写事件可能在 onConnect 阶段就被触发，因此构造时即注册写回调
    channel_->set_write_callback(std::bind(&Connection::HandleWrite, this));
  }
  read_buf_ = std::make_unique<Buffer>();
  send_buf_ = std::make_unique<Buffer>();
  Touch();
}

Connection::~Connection() = default;

RC Connection::Read() {
  if (state_.load() != State::Connected) {
    return RC_CONNECTION_ERROR;
  }
  Touch();
  if (socket_->IsNonBlocking()) {
    return ReadNonBlocking();
  }
  return ReadBlocking();
}

RC Connection::Write() {
  if (state_.load() != State::Connected) {
    return RC_CONNECTION_ERROR;
  }

  if (send_offset_ >= send_buf_->Size()) {
    send_buf_->Clear();
    send_offset_ = 0;
    if (channel_) {
      channel_->DisableWrite();
    }
    return RC_SUCCESS;
  }

  RC rc = socket_->IsNonBlocking() ? WriteNonBlocking() : WriteBlocking();

  if (send_offset_ >= send_buf_->Size()) {
    send_buf_->Clear();
    send_offset_ = 0;
    if (channel_) {
      channel_->DisableWrite();
    }
  } else if (channel_) {
    // 内核发送缓冲区已满，剩余数据等待 EPOLLOUT 继续发送
    channel_->EnableWrite();
  }
  return rc;
}

RC Connection::ReadNonBlocking() {
  int sockfd = socket_->fd();
  char buf[4096];
  // ET 模式下必须循环读取，直到 EAGAIN / EWOULDBLOCK
  while (true) {
    memset(buf, 0, sizeof(buf));
    ssize_t bytes_read = read(sockfd, buf, sizeof(buf));
    if (bytes_read > 0) {
      read_buf_->Append(buf, static_cast<int>(bytes_read));
      Touch();
      if (read_buf_->Size() > kMaxReadBufferSize) {
        state_.store(State::Closed);
        break;
      }
    } else if (bytes_read == -1 && errno == EINTR) {
      continue;
    } else if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    } else if (bytes_read == 0) {
      // EOF：对端关闭连接
      state_.store(State::Closed);
      break;
    } else {
      perror("read error");
      state_.store(State::Closed);
      break;
    }
  }
  return RC_SUCCESS;
}

RC Connection::WriteNonBlocking() {
  int sockfd = socket_->fd();
  const std::string &data = send_buf_->buf();
  while (send_offset_ < data.size()) {
    ssize_t bytes_write =
        send(sockfd, data.data() + send_offset_, data.size() - send_offset_, kSendFlags);
    if (bytes_write > 0) {
      send_offset_ += static_cast<size_t>(bytes_write);
      Touch();
    } else if (bytes_write == -1 && errno == EINTR) {
      continue;
    } else if (bytes_write == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    } else {
      if (errno != EPIPE && errno != ECONNRESET) {
        perror("write error");
      }
      state_.store(State::Closed);
      return RC_CONNECTION_ERROR;
    }
  }
  return RC_SUCCESS;
}

RC Connection::ReadBlocking() {
  int sockfd = socket_->fd();
  char buf[4096];
  ssize_t bytes_read = read(sockfd, buf, sizeof(buf));
  if (bytes_read > 0) {
    read_buf_->Append(buf, static_cast<int>(bytes_read));
    Touch();
    if (read_buf_->Size() > kMaxReadBufferSize) {
      state_.store(State::Closed);
    }
  } else if (bytes_read == 0) {
    state_.store(State::Closed);
  } else if (bytes_read == -1 && errno != EINTR) {
    perror("read error");
    state_.store(State::Closed);
  }
  return RC_SUCCESS;
}

RC Connection::WriteBlocking() {
  int sockfd = socket_->fd();
  const std::string &data = send_buf_->buf();
  while (send_offset_ < data.size()) {
    ssize_t bytes_write =
        send(sockfd, data.data() + send_offset_, data.size() - send_offset_, kSendFlags);
    if (bytes_write > 0) {
      send_offset_ += static_cast<size_t>(bytes_write);
      Touch();
    } else if (bytes_write == -1 && errno == EINTR) {
      continue;
    } else {
      if (errno != EPIPE && errno != ECONNRESET) {
        perror("write error");
      }
      state_.store(State::Closed);
      return RC_CONNECTION_ERROR;
    }
  }
  return RC_SUCCESS;
}

RC Connection::Send(const std::string &msg) {
  if (state_.load() != State::Connected) {
    return RC_CONNECTION_ERROR;
  }
  if (send_buf_->Size() + msg.size() > kMaxSendBufferSize) {
    Close();
    return RC_CONNECTION_ERROR;
  }

  send_buf_->Append(msg);
  Touch();
  RC rc = Write();
  if (state_.load() == State::Closed) {
    ScheduleDelete();
  }
  return rc;
}

void Connection::Business() {
  if (state_.load() != State::Connected) {
    return;
  }
  Touch();

  Read();
  if (state_.load() == State::Connected && on_recv_) {
    on_recv_(this);
  }

  // 连接关闭后延迟到本轮事件分发结束再删除，避免在回调栈上销毁自身
  if (state_.load() == State::Closed) {
    ScheduleDelete();
  }
}

void Connection::HandleWrite() {
  if (state_.load() != State::Connected) {
    return;
  }
  Touch();
  Write();
  if (state_.load() == State::Closed) {
    ScheduleDelete();
  }
}

void Connection::ScheduleDelete() {
  if (!delete_enabled_.load()) {
    // onConnect 尚未返回，此时不能异步销毁自身
    return;
  }
  bool expected = false;
  if (!delete_scheduled_.compare_exchange_strong(expected, true)) {
    return;
  }

  auto callback = delete_connection_;
  int fd = socket_->fd();
  if (!callback) {
    return;
  }

  if (loop_ != nullptr) {
    // 同时携带 this，避免 fd 被复用后误删新连接
    loop_->QueueInLoop([callback, fd, this] { callback(fd, this); });
  } else {
    callback(fd, this);
  }
}

void Connection::Close() {
  if (state_.exchange(State::Closed) == State::Closed) {
    return;
  }
  if (channel_) {
    // 先注销事件，等待延迟删除真正释放 fd
    channel_->DisableAll();
  }
  ScheduleDelete();
}

void Connection::EnableDeletion() {
  delete_enabled_.store(true);
  if (state_.load() == State::Closed) {
    ScheduleDelete();
  }
}

void Connection::Touch() {
  last_active_ticks_.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                           std::memory_order_relaxed);
}

bool Connection::idleExpired(int idle_seconds) const {
  if (idle_seconds <= 0) {
    return false;
  }
  using namespace std::chrono;
  auto last = steady_clock::time_point(steady_clock::duration(last_active_ticks_.load(std::memory_order_relaxed)));
  return steady_clock::now() - last > seconds(idle_seconds);
}

int Connection::fd() const { return socket_->fd(); }

EventLoop *Connection::loop() const { return loop_; }

void Connection::set_delete_connection(std::function<void(int, Connection *)> const &fn) {
  delete_connection_ = fn;
}

void Connection::set_on_recv(std::function<void(Connection *)> const &fn) {
  on_recv_ = fn;
  if (channel_ && on_recv_ && state_.load() == State::Connected) {
    // 必须先设置回调再注册读事件，否则 ET 边沿可能丢失
    channel_->set_read_callback(std::bind(&Connection::Business, this));
    channel_->EnableRead();
    channel_->EnableET();
  }
}

Connection::State Connection::state() const { return state_.load(); }

Socket *Connection::socket() const { return socket_.get(); }

Buffer *Connection::read_buf() { return read_buf_.get(); }

Buffer *Connection::send_buf() { return send_buf_.get(); }
