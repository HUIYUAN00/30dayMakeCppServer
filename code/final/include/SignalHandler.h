/**
 * @file SignalHandler.h
 * @brief 简单的信号注册工具：Signal::signal(sig, handler)
 */
#pragma once

#include <csignal>
#include <functional>
#include <map>

inline std::map<int, std::function<void()>> &SignalHandlers() {
  static std::map<int, std::function<void()>> handlers;
  return handlers;
}

inline void SignalHandler(int sig) {
  auto it = SignalHandlers().find(sig);
  if (it != SignalHandlers().end() && it->second) {
    it->second();
  }
}

struct Signal {
  static void signal(int sig, const std::function<void()> &handler) {
    SignalHandlers()[sig] = handler;
    ::signal(sig, SignalHandler);
  }
};
