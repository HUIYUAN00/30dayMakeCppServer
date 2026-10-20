/**
 * @file Connection.h
 * @brief 一条 TCP 连接：封装 fd、缓冲区、读写队列与业务回调
 */
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "common.h"

class Connection {
 public:
  enum State {
    Invalid = 0,
    Connecting,
    Connected,
    Closed,
  };
  DISALLOW_COPY_AND_MOVE(Connection);
  Connection(int fd, EventLoop *loop);
  ~Connection();

  void set_delete_connection(std::function<void(int, Connection *)> const &fn);
  void set_on_recv(std::function<void(Connection *)> const &fn);
  State state() const;
  Socket *socket() const;
  Buffer *read_buf();
  Buffer *send_buf();

  RC Read();
  RC Write();
  RC Send(const std::string &msg);

  void Close();
  void HandleWrite();
  // 连接注册完成后再允许真正删除，避免 onConnect 回调期间被异步销毁
  void EnableDeletion();
  bool idleExpired(int idle_seconds) const;
  int fd() const;
  EventLoop *loop() const;

 private:
  void Business();
  void ScheduleDelete();
  void Touch();

  RC ReadNonBlocking();
  RC WriteNonBlocking();
  RC ReadBlocking();
  RC WriteBlocking();

  // 防止单连接无限占用内存
  static constexpr size_t kMaxReadBufferSize = 4 * 1024 * 1024;
  static constexpr size_t kMaxSendBufferSize = 8 * 1024 * 1024;

 private:
  std::unique_ptr<Socket> socket_;
  std::unique_ptr<Channel> channel_;
  EventLoop *loop_{nullptr};

  std::atomic<State> state_{State::Connected};
  std::unique_ptr<Buffer> read_buf_;
  std::unique_ptr<Buffer> send_buf_;
  size_t send_offset_{0};

  std::atomic<bool> delete_scheduled_{false};
  std::atomic<bool> delete_enabled_{false};
  std::atomic<std::chrono::steady_clock::rep> last_active_ticks_{0};

  std::function<void(int, Connection *)> delete_connection_;
  std::function<void(Connection *)> on_recv_;
};
