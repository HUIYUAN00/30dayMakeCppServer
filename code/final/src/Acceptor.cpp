/**
 * @file Acceptor.cpp
 */
#include "Acceptor.h"

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <utility>

#include "Channel.h"
#include "Socket.h"

namespace {
bool IsWouldBlock(int err) { return err == EAGAIN || err == EWOULDBLOCK; }
}  // namespace

Acceptor::Acceptor(EventLoop *loop, const char *ip, uint16_t port) {
  socket_ = std::make_unique<Socket>();

  RC rc = socket_->Create();
  if (rc != RC_SUCCESS) {
    throw std::runtime_error("Acceptor: create socket failed");
  }
  rc = socket_->Bind(ip, port);
  if (rc != RC_SUCCESS) {
    throw std::runtime_error("Acceptor: bind socket failed");
  }
  rc = socket_->Listen();
  if (rc != RC_SUCCESS) {
    throw std::runtime_error("Acceptor: listen socket failed");
  }
  socket_->SetNonBlocking();

  idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);

  channel_ = std::make_unique<Channel>(socket_->fd(), loop);
  std::function<void()> cb = std::bind(&Acceptor::AcceptConnection, this);

  channel_->set_read_callback(cb);
  // 监听 socket 使用水平触发，配合 accept 循环直到 EAGAIN
  channel_->EnableRead();
}

Acceptor::~Acceptor() {
  if (idle_fd_ >= 0) {
    ::close(idle_fd_);
    idle_fd_ = -1;
  }
}

RC Acceptor::AcceptConnection() {
  while (true) {
    int clnt_fd = -1;
    if (socket_->Accept(clnt_fd) != RC_SUCCESS) {
      int err = errno;
      if (IsWouldBlock(err) || err == EINTR) {
        break;
      }
      if (err == EMFILE || err == ENFILE) {
        // 经典 idle fd 方案：释放备用 fd，接受并关闭一个连接，再重新占位
        if (idle_fd_ >= 0) {
          ::close(idle_fd_);
          int tmp_fd = -1;
          if (socket_->Accept(tmp_fd) == RC_SUCCESS) {
            ::close(tmp_fd);
          }
          idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        }
      } else {
        perror("accept error");
      }
      break;
    }

    if (fcntl(clnt_fd, F_SETFL, fcntl(clnt_fd, F_GETFL) | O_NONBLOCK) == -1) {
      perror("set client nonblocking failed");
      ::close(clnt_fd);
      continue;
    }

    if (new_connection_callback_) {
      new_connection_callback_(clnt_fd);
    } else {
      ::close(clnt_fd);
    }
  }
  return RC_SUCCESS;
}

void Acceptor::set_new_connection_callback(std::function<void(int)> const &callback) {
  new_connection_callback_ = callback;
}
