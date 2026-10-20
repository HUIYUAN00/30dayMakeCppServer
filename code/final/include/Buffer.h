/**
 * @file Buffer.h
 * @author 冯岳松 (yuesong-feng@foxmail.com)
 * @brief
 * @version 0.1
 * @date 2022-01-04
 *
 * @copyright Copyright (冯岳松) 2022
 *
 */
#pragma once
#include <memory>
#include <string>
#include "common.h"

class Buffer {
 public:
  DISALLOW_COPY_AND_MOVE(Buffer);
  Buffer() = default;
  ~Buffer() = default;

  const std::string &buf() const;
  const char* c_str() const;
  void set_buf(const char *buf);
  void set_buf(const std::string &buf);

  size_t Size() const;
  void Append(const char *_str, int _size);
  void Append(const std::string &str);
  void Retrieve(size_t len);
  void Clear();

 private:
  std::string buf_;
};
