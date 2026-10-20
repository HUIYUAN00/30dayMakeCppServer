/**
 * @file TcpServer.cpp
 */
#include "TcpServer.h"

#include <unistd.h>
#include <algorithm>
#include <csignal>
#include <iostream>
#include <thread>

#include "Acceptor.h"
#include "Connection.h"
#include "EventLoop.h"
#include "ThreadPool.h"

TcpServer::TcpServer(const char *ip, uint16_t port) {
  // 服务端写已关闭连接时，避免 SIGPIPE 杀死整个进程
  ::signal(SIGPIPE, SIG_IGN);

  main_reactor_ = std::make_unique<EventLoop>();
  acceptor_ = std::make_unique<Acceptor>(main_reactor_.get(), ip, port);

  acceptor_->set_new_connection_callback(
      std::bind(&TcpServer::NewConnection, this, std::placeholders::_1));

  unsigned int size = std::max(1u, std::thread::hardware_concurrency());
  thread_pool_ = std::make_unique<ThreadPool>(size);

  for (unsigned int i = 0; i < size; ++i) {
    auto loop = std::make_unique<EventLoop>();
    EventLoop *raw_loop = loop.get();
    auto last_sweep =
        std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
    // 空闲扫描必须在连接所属的 reactor 线程执行，避免跨线程操作 Channel/Poller
    raw_loop->setTickCallback([this, raw_loop, last_sweep] {
      auto now = std::chrono::steady_clock::now();
      if (now - *last_sweep < std::chrono::seconds(10)) {
        return;
      }
      *last_sweep = now;
      SweepIdleConnections(raw_loop);
    });
    sub_reactors_.push_back(std::move(loop));
  }
}

TcpServer::~TcpServer() {
  Stop();
  // 先等待所有 sub reactor 线程退出，再销毁连接，避免 Channel 访问已销毁的 EventLoop
  thread_pool_.reset();
  connections_.clear();
}

void TcpServer::Start() {
  for (auto &sub_reactor : sub_reactors_) {
    thread_pool_->Add([loop = sub_reactor.get()] { loop->Loop(); });
  }

  while (!main_reactor_->stopped()) {
    main_reactor_->LoopOnce(100);
  }
}

void TcpServer::Stop() {
  main_reactor_->Stop();
  for (auto &sub_reactor : sub_reactors_) {
    sub_reactor->Stop();
  }
}

void TcpServer::setMaxConnections(size_t max_connections) { max_connections_ = max_connections; }

void TcpServer::setIdleTimeout(int seconds) { idle_timeout_seconds_ = seconds; }

RC TcpServer::NewConnection(int fd) {
  if (fd < 0) {
    return RC_CONNECTION_ERROR;
  }

  {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    if (max_connections_ > 0 && connections_.size() >= max_connections_) {
      ::close(fd);
      return RC_CONNECTION_ERROR;
    }
  }

  if (sub_reactors_.empty()) {
    ::close(fd);
    return RC_CONNECTION_ERROR;
  }

  size_t index = static_cast<size_t>(fd) % sub_reactors_.size();
  auto conn = std::make_unique<Connection>(fd, sub_reactors_[index].get());

  conn->set_delete_connection(
      std::bind(&TcpServer::DeleteConnection, this, std::placeholders::_1, std::placeholders::_2));

  Connection *raw_conn = nullptr;
  {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.emplace(fd, std::move(conn));
    if (!it.second) {
      // emplace 失败时临时 Connection 已析构并关闭 fd，不能再 close 一次
      return RC_CONNECTION_ERROR;
    }
    raw_conn = it.first->second.get();
  }

  // 先通知业务层，再注册读事件：避免 sub reactor 立刻读到 EOF 删除连接后，
  // 主线程仍持有悬空指针执行 onConnect。
  if (on_connect_) {
    on_connect_(raw_conn);
  }
  raw_conn->set_on_recv(on_recv_);
  raw_conn->EnableDeletion();
  return RC_SUCCESS;
}

RC TcpServer::DeleteConnection(int fd, Connection *conn) {
  std::lock_guard<std::mutex> lock(connections_mutex_);
  auto it = connections_.find(fd);
  if (it == connections_.end() || it->second.get() != conn) {
    return RC_SUCCESS;
  }
  connections_.erase(it);
  return RC_SUCCESS;
}

void TcpServer::SweepIdleConnections(EventLoop *owner) {
  if (idle_timeout_seconds_ <= 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(connections_mutex_);
  for (auto &entry : connections_) {
    Connection *conn = entry.second.get();
    if (conn->loop() != owner) {
      continue;
    }
    if (conn->idleExpired(idle_timeout_seconds_)) {
      std::cout << "closing idle connection fd=" << entry.first << std::endl;
      conn->Close();
    }
  }
}

void TcpServer::onConnect(std::function<void(Connection *)> fn) { on_connect_ = std::move(fn); }

void TcpServer::onRecv(std::function<void(Connection *)> fn) { on_recv_ = std::move(fn); }
