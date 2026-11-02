#pragma once

#include "app/poll_source.h"
#include "dbus/idle/screensaver_service.h"

class ScreenSaverPollSource final : public PollSource {
public:
  explicit ScreenSaverPollSource(ScreenSaverService& service) : m_service(service) {}

  [[nodiscard]] int pollTimeoutMs() const override {
    if (m_service.hasPendingEvents()) {
      return 0;
    }
    const int timeout = m_service.getPollData().getPollTimeout();
    return timeout;
  }

  void dispatch(const std::vector<pollfd>& /*fds*/, std::size_t /*startIdx*/) override {
    m_service.processPendingRequests();
  }

protected:
  void doAddPollFds(std::vector<pollfd>& fds) override {
    const auto pollData = m_service.getPollData();
    fds.push_back({.fd = pollData.fd, .events = pollData.events, .revents = 0});
    // sdbus-c++ 1.4 PollData has only fd (no eventFd).
  }

private:
  ScreenSaverService& m_service;
};
