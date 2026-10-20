#include <cstdint>
#include <iostream>
#include <string>

#include "pine.h"

int main(int argc, char *argv[]) {
  const char *ip = argc > 1 ? argv[1] : "127.0.0.1";
  uint16_t port = argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : 1234;

  Socket sock;
  if (sock.Create() != RC_SUCCESS || sock.Connect(ip, port) != RC_SUCCESS) {
    std::cerr << "connect to " << ip << ":" << port << " failed" << std::endl;
    return 1;
  }

  Connection conn(sock.Release(), nullptr);
  std::string line;
  while (conn.state() == Connection::State::Connected && std::getline(std::cin, line)) {
    conn.Send(line + "\n");
    if (conn.state() != Connection::State::Connected) {
      break;
    }
    conn.Read();
    Buffer *buf = conn.read_buf();
    std::cout << buf->buf() << std::flush;
    buf->Clear();
  }
  return 0;
}
