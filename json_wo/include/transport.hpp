#include <string>
#include <functional>
#pragma once
struct Transport {
  virtual void start() = 0;
  virtual void send(std::string text) = 0;
  virtual void on_message(std::function<void(std::string_view)> cb) = 0;
  virtual void on_close(std::function<void()> cb) = 0;
  virtual void close() = 0;
  virtual ~Transport() = default;
};