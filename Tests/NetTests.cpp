// Multiplayer (round 7): the transport over loopback in one process (M-1).
#include "bdg_test.h"
#include "Net/Transport.h"
#include <chrono>
#include <cstring>
#include <thread>

using namespace bdg::net;

namespace {
double Now() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
// Polls both ends until `done` or `seconds` pass.
template <class F>
bool Pump(Transport& a, Transport& b, std::vector<Event>& ea, std::vector<Event>& eb, double seconds, F done) {
  const double end = Now() + seconds;
  while (Now() < end) {
    a.Poll(Now(), ea);
    b.Poll(Now(), eb);
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return done();
}
size_t Count(const std::vector<Event>& v, Event::Type t) {
  size_t n = 0;
  for (const Event& e : v) n += e.type == t;
  return n;
}
} // namespace

TEST_CASE("net transport: connect, reliable order under loss, unreliable, latency, stats, disconnect, timeout (round 7 M-1)") {
  Transport server, client;
  std::string err;
  CHECK(server.Listen(0, 4, &err, "127.0.0.1"));
  CHECK(server.Port() != 0 && server.IsServer());
  CHECK(client.Connect("127.0.0.1", server.Port(), &err));
  std::vector<Event> es, ec;
  CHECK(Pump(server, client, es, ec, 3, [&] { return Count(es, Event::Type::Connected) == 1 && Count(ec, Event::Type::Connected) == 1; }));
  CHECK(server.Peers().size() == 1 && client.Connected());
  const uint32_t peer = server.Peers().empty() ? 0 : server.Peers()[0];

  // reliable, ordered, with 25 % of the datagrams lost both ways: every message arrives, in order
  server.sim.lossPercent = client.sim.lossPercent = 25;
  es.clear();
  for (uint32_t i = 0; i < 200; ++i) client.Send(1, Channel::Reliable, &i, sizeof i);
  std::vector<uint32_t> got;
  Pump(server, client, es, ec, 10, [&] {
    for (Event& e : es)
      if (e.type == Event::Type::Received && e.channel == Channel::Reliable && e.data.size() == 4) {
        uint32_t v;
        std::memcpy(&v, e.data.data(), 4);
        got.push_back(v);
      }
    es.clear();
    return got.size() >= 200;
  });
  bool ordered = got.size() == 200;
  for (uint32_t i = 0; ordered && i < 200; ++i) ordered = got[i] == i;
  CHECK(ordered);
  CHECK(server.Stats(peer).simDropped > 0 || client.Stats(1).simDropped > 0);

  // unreliable with 40 % loss: some arrive, some do not (none twice)
  server.sim.lossPercent = client.sim.lossPercent = 40;
  size_t unreliable = 0;
  for (uint32_t i = 0; i < 200; ++i) {
    client.Send(1, Channel::Unreliable, &i, sizeof i);
    client.Poll(Now(), ec); // send them one by one (one datagram each)
  }
  Pump(server, client, es, ec, 1.0, [&] { return false; });
  for (const Event& e : es) unreliable += e.type == Event::Type::Received && e.channel == Channel::Unreliable;
  CHECK(unreliable > 40 && unreliable < 190);
  server.sim.lossPercent = client.sim.lossPercent = 0;
  es.clear();

  // latency: 200 ms round trip -> a message takes about 100 ms one way
  server.sim.latencyMs = 200;
  const double sentAt = Now();
  const uint32_t marker = 777;
  client.Send(1, Channel::Reliable, &marker, sizeof marker);
  double arrived = -1;
  Pump(server, client, es, ec, 3, [&] {
    for (const Event& e : es)
      if (e.type == Event::Type::Received && e.data.size() == 4 && std::memcmp(e.data.data(), &marker, 4) == 0 && arrived < 0) arrived = Now();
    return arrived >= 0;
  });
  CHECK(arrived - sentAt >= 0.095 && arrived - sentAt < 1.0);
  server.sim.latencyMs = 0;

  // stats
  Pump(server, client, es, ec, 1.2, [&] { return false; });
  const PeerStats st = client.Stats(1);
  CHECK(st.rttMs >= 0 && st.bytesSent > 800 && st.bytesReceived > 0 && client.Stats(1).sendKbps >= 0);

  // a polite disconnect
  es.clear();
  ec.clear();
  client.Disconnect();
  bool politely = false;
  CHECK(Pump(server, client, es, ec, 3, [&] {
    for (const Event& e : es)
      if (e.type == Event::Type::Disconnected) politely = !e.timedOut;
    return Count(es, Event::Type::Disconnected) == 1;
  }));
  CHECK(politely && server.Peers().empty());

  // a client that vanishes (no goodbye): the server notices within its timeout (set before the client joins)
  Transport s2, c2;
  s2.timeoutMs = 600;
  CHECK(s2.Listen(0, 2, &err, "127.0.0.1"));
  CHECK(c2.Connect("127.0.0.1", s2.Port(), &err));
  std::vector<Event> e2s, e2c;
  CHECK(Pump(s2, c2, e2s, e2c, 3, [&] { return Count(e2s, Event::Type::Connected) == 1; }));
  c2.Close();
  bool timedOut = false;
  std::vector<Event> none;
  Transport idle;
  CHECK(Pump(s2, idle, e2s, none, 5, [&] {
    for (const Event& e : e2s)
      if (e.type == Event::Type::Disconnected) timedOut = e.timedOut;
    return Count(e2s, Event::Type::Disconnected) == 1;
  }));
  CHECK(timedOut);
  CHECK(!client.Connect("no.such.host.invalid", 1, &err) && !err.empty());
}
