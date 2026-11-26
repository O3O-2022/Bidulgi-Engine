// Multiplayer (round 7): the transport over loopback in one process (M-1).
#include "bdg_test.h"
#include "Net/NetCodec.h"
#include "Net/Transport.h"
#include <algorithm>
#include <cmath>
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
  c2.Close(false); // vanishes like a crash: no goodbye
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

TEST_CASE("net codec: bits, var-ints, strings, quantized positions and rotations, overflow is safe (round 7 M-2)") {
  BitWriter w;
  w.Bits(5, 3);
  w.Bool(true);
  w.VarUint(300);
  w.VarInt(-12345);
  w.Float(3.25f);
  w.String("비둘기 net");
  w.Bits(0xABCDE, 20);
  BitReader r(w.Data());
  CHECK(r.Bits(3) == 5 && r.Bool() && r.VarUint() == 300 && r.VarInt() == -12345 && r.Float() == 3.25f && r.String() == "비둘기 net" &&
        r.Bits(20) == 0xABCDE && !r.Overflowed());
  r.Bits(32);
  r.Bits(32);
  CHECK(r.Overflowed()); // past the end: flagged, no crash
  // positions: 1 mm steps; rotations: smallest three within a few thousandths
  CHECK(std::fabs(DequantizePosition(QuantizePosition(123.4567f)) - 123.4567f) <= 0.0005f);
  float worst = 0;
  for (int i = 0; i < 200; ++i) {
    const glm::vec3 axis = glm::normalize(glm::vec3(std::sin(i * 1.3f), std::cos(i * 0.7f), std::sin(i * 2.1f) + 0.1f));
    const glm::quat q = glm::angleAxis(i * 0.37f, axis);
    const glm::quat u = UnpackQuat(PackQuat(q));
    worst = std::max(worst, 1.f - std::fabs(glm::dot(q, u)));
  }
  CHECK(worst < 2e-5f); // about 0.5 degrees at the worst
}

TEST_CASE("net codec: transform and blob deltas are small and exact (round 7 M-2)") {
  NetTransform a;
  a.position = {10, 2, -5};
  a.rotation = glm::angleAxis(0.5f, glm::vec3(0, 1, 0));
  NetTransform b = a;
  b.position.x += 0.02f; // a small step: only the position, as a small number
  BitWriter full, delta, none;
  WriteTransform(full, b);
  WriteTransformDelta(delta, a, b);
  WriteTransformDelta(none, a, a);
  CHECK(delta.Data().size() < full.Data().size() && none.Data().size() == 1);
  BitReader rd(delta.Data());
  const NetTransform got = ReadTransformDelta(rd, a);
  const NetTransform want = Quantized(b);
  CHECK(glm::length(got.position - want.position) < 1e-6f && std::fabs(glm::dot(got.rotation, want.rotation)) > 0.99999f);
  BitReader rf(full.Data());
  CHECK(glm::length(ReadTransform(rf).position - want.position) < 1e-6f);
  // blobs: a few bytes changed -> a few bytes sent; a new length -> the whole blob
  std::vector<uint8_t> base(400), now;
  for (size_t i = 0; i < base.size(); ++i) base[i] = (uint8_t)(i * 7);
  now = base;
  now[10] = 1; now[11] = 2; now[300] = 9;
  std::vector<uint8_t> d = DeltaBytes(base, now), out;
  CHECK(d.size() < 20 && ApplyDeltaBytes(base, d, out) && out == now);
  CHECK(DeltaBytes(base, base).size() == 1);
  now.push_back(5);
  d = DeltaBytes(base, now);
  CHECK(ApplyDeltaBytes(base, d, out) && out == now);
  CHECK(!ApplyDeltaBytes(base, {3, 250, 10}, out)); // damaged
}

TEST_CASE("net snapshots: interpolation between server states, a short extrapolation, then hold (round 7 M-2)") {
  SnapshotBuffer buf;
  auto at = [](float x) { NetTransform t; t.position = {x, 0, 0}; return t; };
  buf.Push(0.10, at(1));
  buf.Push(0.20, at(2));
  buf.Push(0.15, at(1.5f)); // late but in order after sorting
  buf.Push(0.30, at(4));
  CHECK(buf.Size() == 4);
  CHECK(std::fabs(buf.Sample(0.125).position.x - 1.25f) < 1e-4f);
  CHECK(std::fabs(buf.Sample(0.25).position.x - 3.f) < 1e-4f);
  CHECK(buf.Sample(0.0).position.x == 1.f);
  CHECK(std::fabs(buf.Sample(0.35, 0.25).position.x - 5.f) < 1e-3f); // keeps going 20 m/s for a little
  CHECK(std::fabs(buf.Sample(2.0, 0.25).position.x - 9.f) < 1e-3f);  // then holds (0.25 s at most)
}

TEST_CASE("net prediction: the owner moves at once, the server's answer corrects it, pending moves are kept (round 7 M-2)") {
  // The client walks 0.1 m per tick along x; the server applies the moves but a wall stops it at x = 0.95.
  Prediction p;
  p.Reset({0, 0, 0});
  std::vector<glm::vec3> serverAt;
  glm::vec3 server(0);
  const int latencyTicks = 4;
  float lastError = 0;
  for (int tick = 1; tick <= 30; ++tick) {
    p.Add({0.1f, 0, 0});
    server.x = std::min(server.x + 0.1f, 0.95f); // the server applies move `tick` (the message took no time)
    serverAt.push_back(server);
    if (tick > latencyTicks) { // its answer arrives `latencyTicks` later
      const uint32_t ack = (uint32_t)(tick - latencyTicks);
      lastError = p.Reconcile(ack, serverAt[ack - 1]);
    }
    if (tick == 8) CHECK(p.Pending() == (size_t)latencyTicks);
  }
  // after reconciling, the predicted position = the server's + the moves it has not seen yet
  CHECK(std::fabs(p.Position().x - (0.95f + latencyTicks * 0.1f)) < 1e-4f);
  CHECK(lastError < 0.11f); // each answer moves it back by at most one blocked step
  CHECK(p.Reconcile(p.LastSeq(), {0.95f, 0, 0}) > 0.3f && p.Pending() == 0 && p.Position().x == 0.95f);
}
