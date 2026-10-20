/**
 * @file TcpServer.h
 * @brief 主从 Reactor 多线程 TCP 服务器
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common.h"

class TcpServer {
 public:
  DISALLOW_COPY_AND_MOVE(TcpServer);
  explicit TcpServer(const char *ip = "0.0.0.0", uint16_t port = 1234);
  ~TcpServer();

  void Start();
  void Stop();

  // 0 表示不限制 / 不启用空闲超时
  void setMaxConnections(size_t max_connections);
  void setIdleTimeout(int seconds);

  RC NewConnection(int fd);
  RC DeleteConnection(int fd, Connection *conn);

  void onConnect(std::function<void(Connection *)> fn);
  void onRecv(std::function<void(Connection *)> fn);

 private:
  void SweepIdleConnections(EventLoop *owner);

  std::unique_ptr<EventLoop> main_reactor_;
  std::unique_ptr<Acceptor> acceptor_;

  std::unordered_map<int, std::unique_ptr<Connection>> connections_;
  std::mutex connections_mutex_;
  std::vector<std::unique_ptr<EventLoop>> sub_reactors_;

  std::unique_ptr<ThreadPool> thread_pool_;

  std::function<void(Connection *)> on_connect_;
  std::function<void(Connection *)> on_recv_;

  size_t max_connections_{10000};
  int idle_timeout_seconds_{300};
};
