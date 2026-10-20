cmake_minimum_required(VERSION 3.16)
project(pine_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

add_library(pine_core STATIC
  src/Acceptor.cpp
  src/Buffer.cpp
  src/Channel.cpp
  src/Connection.cpp
  src/EventLoop.cpp
  src/Poller.cpp
  src/Socket.cpp
  src/TcpServer.cpp
  src/ThreadPool.cpp
)
target_include_directories(pine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pine_core PUBLIC Threads::Threads)

if(APPLE)
  target_compile_definitions(pine_core PUBLIC OS_MACOS)
else()
  target_compile_definitions(pine_core PUBLIC OS_LINUX)
endif()

add_executable(server apps/server.cpp)
target_link_libraries(server PRIVATE pine_core)

add_executable(client apps/client.cpp)
target_link_libraries(client PRIVATE pine_core)
