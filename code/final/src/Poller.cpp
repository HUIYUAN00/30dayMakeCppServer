/**
 * @file Poller.cpp
 * @author 冯岳松 (yuesong-feng@foxmail.com)
 * @brief
 * @version 0.1
 * @date 2022-01-04
 *
 * @copyright Copyright (冯岳松) 2022
 *
 */
#include "Poller.h"

#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Channel.h"
#include "Socket.h"
#include "common.h"

#define MAX_EVENTS 1000

#ifdef OS_LINUX

Poller::Poller() {
  fd_ = epoll_create1(0);
  ErrorIf(fd_ == -1, "epoll create error");
  events_ = new epoll_event[MAX_EVENTS];
  memset(events_, 0, sizeof(*events_) * MAX_EVENTS);
}

Poller::~Poller() {
  if (fd_ != -1) {
    close(fd_);
    fd_ = -1;
  }
  delete[] events_;
}

std::vector<Channel *> Poller::Poll(long timeout) const {
  std::vector<Channel *> active_channels;
  int nfds = 0;
  do {
    nfds = epoll_wait(fd_, events_, MAX_EVENTS, static_cast<int>(timeout));
  } while (nfds == -1 && errno == EINTR);
  ErrorIf(nfds == -1, "epoll wait error");
  for (int i = 0; i < nfds; ++i) {
    Channel *ch = (Channel *)events_[i].data.ptr;
    int events = events_[i].events;
    ch->clear_ready_events();
    int readable = EPOLLIN | EPOLLERR | EPOLLHUP;
#ifdef EPOLLRDHUP
    readable |= EPOLLRDHUP;
#endif
    if (events & readable) {
      ch->set_ready_event(Channel::READ_EVENT);
    }
    if (events & EPOLLOUT) {
      ch->set_ready_event(Channel::WRITE_EVENT);
    }
    if (events & EPOLLET) {
      ch->set_ready_event(Channel::ET);
    }
    active_channels.push_back(ch);
  }
  return active_channels;
}

RC Poller::UpdateChannel(Channel *ch) const {
  int sockfd = ch->fd();
  struct epoll_event ev {};
  ev.data.ptr = ch;
  if (ch->listen_events() & Channel::READ_EVENT) {
    ev.events |= EPOLLIN | EPOLLPRI;
#ifdef EPOLLRDHUP
    ev.events |= EPOLLRDHUP;
#endif
  }
  if (ch->listen_events() & Channel::WRITE_EVENT) {
    ev.events |= EPOLLOUT;
  }
  if (ch->listen_events() & Channel::ET) {
    ev.events |= EPOLLET;
  }
  if (!ch->exist()) {
    if (epoll_ctl(fd_, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
      fprintf(stderr, "epoll add error: fd=%d listen=%d errno=%d(%s)\n", sockfd,
              static_cast<int>(ch->listen_events()), errno, strerror(errno));
      exit(EXIT_FAILURE);
    }
    ch->set_exist();
  } else if (epoll_ctl(fd_, EPOLL_CTL_MOD, sockfd, &ev) == -1) {
    fprintf(stderr, "epoll modify error: fd=%d listen=%d errno=%d(%s)\n", sockfd,
            static_cast<int>(ch->listen_events()), errno, strerror(errno));
    exit(EXIT_FAILURE);
  }
  return RC_SUCCESS;
}

RC Poller::DeleteChannel(Channel *ch) const {
  if (!ch->exist()) {
    return RC_SUCCESS;
  }
  int sockfd = ch->fd();
  if (epoll_ctl(fd_, EPOLL_CTL_DEL, sockfd, nullptr) == -1 && errno != ENOENT) {
    perror("epoll delete error");
    return RC_POLLER_ERROR;
  }
  ch->set_exist(false);
  return RC_SUCCESS;
}

#endif

#ifdef OS_MACOS

Poller::Poller() {
  fd_ = kqueue();
  assert(fd_ != -1);
  events_ = new struct kevent[MAX_EVENTS];
  memset(events_, 0, sizeof(*events_) * MAX_EVENTS);
}

Poller::~Poller() {
  if (fd_ != -1) {
    close(fd_);
    fd_ = -1;
  }
}

std::vector<Channel *> Poller::Poll(long timeout) const {
  std::vector<Channel *> active_channels;
  struct timespec ts;
  memset(&ts, 0, sizeof(ts));
  if (timeout != -1) {
    ts.tv_sec = timeout / 1000;
    ts.tv_nsec = (timeout % 1000) * 1000 * 1000;
  }
  int nfds = 0;
  do {
    if (timeout == -1) {
      nfds = kevent(fd_, NULL, 0, events_, MAX_EVENTS, NULL);
    } else {
      nfds = kevent(fd_, NULL, 0, events_, MAX_EVENTS, &ts);
    }
  } while (nfds == -1 && errno == EINTR);
  if (nfds == -1) {
    perror("kevent wait error");
    return active_channels;
  }

  for (int i = 0; i < nfds; ++i) {
    Channel *ch = (Channel *)events_[i].udata;
    int events = events_[i].filter;
    ch->clear_ready_events();
    if (events == EVFILT_READ) {
      ch->set_ready_event(ch->READ_EVENT | ch->ET);
    }
    if (events == EVFILT_WRITE) {
      ch->set_ready_event(ch->WRITE_EVENT | ch->ET);
    }
    if (events_[i].flags & (EV_EOF | EV_ERROR)) {
      ch->set_ready_event(ch->READ_EVENT);
    }
    active_channels.push_back(ch);
  }
  return active_channels;
}

RC Poller::UpdateChannel(Channel *ch) const {
  int fd = ch->fd();

  // kqueue 的 EV_ADD 不会删除不再关注的事件，先显式删除读/写过滤器
  struct kevent del[2];
  EV_SET(&del[0], fd, EVFILT_READ, EV_DELETE, 0, 0, ch);
  EV_SET(&del[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, ch);
  kevent(fd_, del, 2, NULL, 0, NULL);

  struct kevent ev[2];
  memset(ev, 0, sizeof(*ev) * 2);
  int n = 0;
  int op = EV_ADD;
  if (ch->listen_events() & ch->ET) {
    op |= EV_CLEAR;
  }
  if (ch->listen_events() & ch->READ_EVENT) {
    EV_SET(&ev[n++], fd, EVFILT_READ, op, 0, 0, ch);
  }
  if (ch->listen_events() & ch->WRITE_EVENT) {
    EV_SET(&ev[n++], fd, EVFILT_WRITE, op, 0, 0, ch);
  }
  if (n == 0) {
    return RC_SUCCESS;
  }
  int r = kevent(fd_, ev, n, NULL, 0, NULL);
  if (r == -1) {
    perror("kqueue add event error");
    return RC_POLLER_ERROR;
  }
  return RC_SUCCESS;
}

RC Poller::DeleteChannel(Channel *ch) const {
  struct kevent ev[2];
  int n = 0;
  int fd = ch->fd();
  if (ch->listen_events() & ch->READ_EVENT) {
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, ch);
  }
  if (ch->listen_events() & ch->WRITE_EVENT) {
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, ch);
  }
  int r = kevent(fd_, ev, n, NULL, 0, NULL);
  if (r == -1) {
    perror("kqueue delete event error");
    return RC_POLLER_ERROR;
  }
  return RC_SUCCESS;
}
#endif
