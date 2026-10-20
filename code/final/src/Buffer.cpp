/**
 * @file Buffer.cpp
 * @author 冯岳松 (yuesong-feng@foxmail.com)
 * @brief
 * @version 0.1
 * @date 2022-01-04
 *
 * @copyright Copyright (冯岳松) 2022
 *
 */
#include "Buffer.h"

const std::string &Buffer::buf() const { return buf_; }

const char *Buffer::c_str() const { return buf_.c_str(); }

void Buffer::set_buf(const char *buf) {
  std::string new_buf(buf);
  buf_.swap(new_buf);
}

void Buffer::set_buf(const std::string &buf) { buf_ = buf; }

size_t Buffer::Size() const { return buf_.size(); }

void Buffer::Append(const char *str, int size) {
  if (str != nullptr && size > 0) {
    buf_.append(str, static_cast<size_t>(size));
  }
}

void Buffer::Append(const std::string &str) { buf_.append(str); }

void Buffer::Retrieve(size_t len) {
  if (len >= buf_.size()) {
    buf_.clear();
  } else {
    buf_.erase(0, len);
  }
}

void Buffer::Clear() { buf_.clear(); }
