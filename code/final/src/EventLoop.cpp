/**
 * @file EventLoop.cpp
 */
#include "EventLoop.h"

#include <utility>
#include <vector>

#include "Channel.h"
#include "Poller.h"

EventLoop::EventLoop() : poller_(std::make_unique<Poller>()) {}

EventLoop::~EventLoop() = default;

void EventLoop::Loop() {
  while (!stop_.load()) {
    LoopOnce(100);
  }
}

void EventLoop::LoopOnce(int timeout_ms) {
  for (Channel *active_ch : poller_->Poll(timeout_ms)) {
    active_ch->HandleEvent();
  }
  DoPendingFunctors();
  if (tick_callback_) {
    tick_callback_();
  }
}

void EventLoop::setTickCallback(std::function<void()> fn) { tick_callback_ = std::move(fn); }

void EventLoop::Stop() { stop_.store(true); }

bool EventLoop::stopped() const { return stop_.load(); }

void EventLoop::QueueInLoop(std::function<void()> fn) {
  if (!fn) {
    return;
  }
  std::lock_guard<std::mutex> lock(pending_mutex_);
  pending_functors_.push_back(std::move(fn));
}

void EventLoop::DoPendingFunctors() {
  std::vector<std::function<void()>> functors;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    functors.swap(pending_functors_);
  }
  for (auto &fn : functors) {
    if (fn) {
      fn();
    }
  }
}

void EventLoop::UpdateChannel(Channel *ch) const { poller_->UpdateChannel(ch); }

void EventLoop::DeleteChannel(Channel *ch) const { poller_->DeleteChannel(ch); }
