/**
 * @file EventLoop.h
 * @brief 事件循环：poll + 事件分发 + 延迟任务队列
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "common.h"

class EventLoop {
 public:
  DISALLOW_COPY_AND_MOVE(EventLoop);
  EventLoop();
  ~EventLoop();

  void Loop();
  void LoopOnce(int timeout_ms);
  void Stop();
  bool stopped() const;

  // 把任务延迟到本轮事件分发结束后执行（用于安全销毁连接）
  void QueueInLoop(std::function<void()> fn);

  // 每轮事件分发结束后调用，用于定时维护（如空闲连接扫描）
  void setTickCallback(std::function<void()> fn);

  void UpdateChannel(Channel *ch) const;
  void DeleteChannel(Channel *ch) const;

 private:
  void DoPendingFunctors();

  std::unique_ptr<Poller> poller_;
  std::atomic<bool> stop_{false};

  std::mutex pending_mutex_;
  std::vector<std::function<void()>> pending_functors_;
  std::function<void()> tick_callback_;
};
