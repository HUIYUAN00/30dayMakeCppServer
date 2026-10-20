/**
 * @file Acceptor.h
 * @brief 监听 socket：只负责接受新连接
 */
#pragma once

#include <functional>
#include <memory>

#include "common.h"

class Acceptor {
 public:
  DISALLOW_COPY_AND_MOVE(Acceptor);
  Acceptor(EventLoop *loop, const char *ip, uint16_t port);
  ~Acceptor();

  RC AcceptConnection();
  void set_new_connection_callback(std::function<void(int)> const &callback);

 private:
  std::unique_ptr<Socket> socket_;
  std::unique_ptr<Channel> channel_;
  std::function<void(int)> new_connection_callback_;
  int idle_fd_{-1};  // EMFILE 时临时释放一个 fd，避免 accept 忙循环
};
