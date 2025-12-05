#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <chrono>
#include "ws_client.hpp"
#include "session.hpp"
#include "ws_server_session.hpp"
#include "router.hpp"
#include "test_client.hpp"
#include "test_server.hpp"

struct TestHarness {
  // Replace these with your real test server/client.
  // Provide operations to: start server, force-close, capture heartbeats, etc.

  TestHarness(boost::asio::io_context& io, std::string host, unsigned short port);
  std::function<void()> server_start      = []{};
  std::function<void()> server_force_close;
  std::function<void()> server_close_on_accept = []{};
  std::function<void()> server_close_after_first_msg = []{};
  std::function<void()> clear_telemetry   = []{};
  std::function<int()>  heartbeat_count   = []{ return 0; };
  std::function<void()> client_start      = []{};
  std::function<void()> client_force_close;
  std::function<void()> client_send_ping  = []{}; // or any trivial frame
  std::function<void()> client_enable_heartbeats;
  std::function<void()> trigger_boot      = []{}; // cause Session to send BootNotification
  std::function<void()> accept_boot       = []{}; // simulate CSMS sending BootAccepted
  bool is_client_online() const;
// private:
  TestClient client_;
  TestServer server_;
};