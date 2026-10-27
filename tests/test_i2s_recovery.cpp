#define private public
#include "../src/platform/I2sBuffer.h"
#undef private
#include "../src/platform/PcmRecovery.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace giga_bredr::detail;
static unsigned checks = 0;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL line%d %s\n", __LINE__, #x);                       \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
struct Fixture {
  std::vector<int32_t> memory, out;
  I2sRing ring;
  PcmRecovery policy;
  uint32_t capacity, half, next = 1, expected = 1, at = 0;
  uint64_t real = 0, zero = 0;
  Fixture(uint32_t c, uint32_t h)
      : memory(c * 2), out(h * 4 + 4, 0x51515151), ring(memory.data(), c),
        capacity(c), half(h) {
    CHECK(policy.begin(1, c, h));
  }
  void produce(uint32_t n) {
    std::vector<int32_t> p(n * 2);
    for (uint32_t i = 0; i < n; ++i) {
      p[2 * i] = int32_t(next++);
      p[2 * i + 1] = -p[2 * i];
    }
    CHECK(ring.push(p.data(), n) == n);
  }
  void verify(uint32_t requested, uint32_t count) {
    CHECK(count <= requested);
    for (uint32_t i = 0; i < count; ++i) {
      CHECK(out[i * 2] == int32_t(expected++));
      CHECK(out[i * 2 + 1] == -out[i * 2]);
    }
    for (uint32_t i = count * 2; i < requested * 2; ++i)
      CHECK(out[i] == 0);
    CHECK(out[requested * 2] == 0x51515151);
    real += count;
    zero += requested - count;
  }
  void start() {
    produce(policy.snapshot().threshold);
    CHECK(ring.prime(out.data(), half));
    verify(half * 2, half * 2);
    std::fill(out.begin(), out.end(), 0x51515151);
  }
  uint32_t tick() {
    std::fill(out.begin(), out.end(), 0x51515151);
    auto n = uint32_t(policy.refill(ring, out.data(), at++));
    verify(half, n);
    return n;
  }
};
void recovery_counterexample() {
  Fixture f(3840, 192);
  f.start();
  CHECK(f.ring.queued() == 1920);
  for (unsigned i = 0; i < 10; ++i)
    CHECK(f.tick() == 192);
  CHECK(f.tick() == 0);
  CHECK(f.policy.snapshot().entries == 1);
  for (unsigned i = 0; i < 20; ++i) {
    f.produce(192);
    auto n = f.tick();
    CHECK(n == (i < 11 ? 0u : 192u));
    if (i < 11)
      CHECK(f.ring.queued() == 192 * (i + 1));
  }
  CHECK(f.ring.queued() == 2112);
  auto s = f.policy.snapshot();
  CHECK(s.resumes == 1 && s.entries == 1 && s.hold_refills == 11 &&
        s.hold_frames == 2112 && s.starved_frames == 192 &&
        s.maximum_hold_frames == 2112 && s.current_hold_frames == 0);
  CHECK(f.zero == s.hold_frames + s.starved_frames);
  CHECK(f.tick() == 192);
  f.produce(384);
  CHECK(f.tick() == 192);
  CHECK(f.policy.snapshot().entries == 1);
  CHECK(f.ring.queued() == 2112);
}
void partial_order_and_threshold() {
  Fixture f(16, 2);
  f.start();
  for (int i = 0; i < 4; ++i)
    CHECK(f.tick() == 2);
  f.produce(1);
  CHECK(f.tick() == 1);
  CHECK(f.policy.snapshot().starved_frames == 1);
  f.produce(1);
  CHECK(f.tick() == 0 && f.ring.queued() == 1);
  f.produce(10);
  CHECK(f.tick() == 0 && f.ring.queued() == 11);
  f.produce(1);
  CHECK(f.tick() == 2 && f.ring.queued() == 10);
  auto s = f.policy.snapshot();
  CHECK(s.record_count == 2 && s.records[0].kind == 1 &&
        s.records[0].real == 1 && s.records[0].silent == 1);
  CHECK(s.records[1].kind == 2 && s.records[1].queued == 12);
  CHECK(f.zero == s.starved_frames + s.hold_frames);
}
void zero_values_and_stop() {
  int32_t mem[32] = {}, pcm[24] = {}, out[12] = {};
  I2sRing ring(mem, 16);
  PcmRecovery p;
  CHECK(p.begin(1, 16, 2));
  CHECK(ring.push(pcm, 12) == 12 && ring.prime(out, 2));
  CHECK(p.refill(ring, out, 0) == 2);
  CHECK(p.snapshot().entries == 0);
  while (ring.queued())
    p.refill(ring, out, 1);
  CHECK(p.refill(ring, out, 2) == 0);
  CHECK(ring.push(pcm, 1) == 1);
  CHECK(p.refill(ring, out, 3) == 0);
  auto before = p.snapshot();
  p.end(4, uint32_t(ring.queued()));
  auto end = p.snapshot();
  CHECK(!end.active && !end.recovering && end.entries == before.entries &&
        end.hold_frames == before.hold_frames);
  CHECK(end.records[end.record_count - 1].kind == 3 &&
        end.records[end.record_count - 1].queued == 1);
  for (auto &x : out)
    x = 73;
  CHECK(p.refill(ring, out, 5) == 0 && ring.queued() == 1);
  for (auto x : out)
    CHECK(x == 73);
  ring.reset();
  CHECK(p.begin(2, 16, 2));
  auto reset = p.snapshot();
  CHECK(reset.epoch == 2 && reset.active && !reset.recovering &&
        reset.entries == 0 && reset.hold_frames == 0 &&
        reset.record_count == 0);
  CHECK(!p.begin(0, 16, 2));
  CHECK(!p.begin(1, 3, 2));
  CHECK(!p.begin(1, 16, 0));
  CHECK(!p.snapshot().active);
  CHECK(p.refill(ring, nullptr, 0) == 0);
}
void geometry_wrap_and_records() {
  const uint32_t rates[] = {8000,  11025, 16000, 22050, 24000,  32000,
                            44100, 48000, 88200, 96000, 176400, 192000};
  for (auto rate : rates)
    for (uint16_t ms : {uint16_t(2), uint16_t(20), uint16_t(100)}) {
      I2sBufferGeometry g;
      CHECK(i2sBufferGeometry(rate, ms, g));
      Fixture f(g.ring_frames, g.half_frames);
      f.start();
      CHECK(f.policy.snapshot().threshold <= g.ring_frames);
      while (f.ring.queued())
        f.tick();
      if (!f.policy.snapshot().recovering)
        f.tick();
      f.at = 0xfffffff0u;
      for (unsigned cycle = 0; cycle < 30; ++cycle) {
        auto needed = f.policy.snapshot().threshold - uint32_t(f.ring.queued());
        f.produce(needed);
        CHECK(f.tick() == f.half);
        for (unsigned k = 0; k < 5; ++k) {
          f.produce(f.half);
          CHECK(f.tick() == f.half);
        }
        while (f.ring.queued())
          f.tick();
        if (!f.policy.snapshot().recovering)
          CHECK(f.tick() == 0);
      }
      auto s = f.policy.snapshot();
      CHECK(s.record_count == 8 && s.records_not_retained > 0 &&
            s.resumes == 30);
      CHECK(f.zero == s.starved_frames + s.hold_frames);
      CHECK(f.ring.queued() == 0);
    }
  // Explicit tiny and non-power-of-two cursor wrapping, including held
  // residual.
  for (auto capacity : {4u, 6u, 14u, 18u}) {
    Fixture f(capacity, 2);
    f.start();
    for (unsigned k = 0; k < 100; ++k) {
      while (f.ring.queued())
        f.tick();
      if (!f.policy.snapshot().recovering)
        f.tick();
      f.produce(f.policy.snapshot().threshold - 1);
      auto q = f.ring.queued();
      CHECK(f.tick() == 0 && f.ring.queued() == q);
      f.produce(1);
      CHECK(f.tick() == 2);
    }
    auto s = f.policy.snapshot();
    CHECK(s.resumes == 100 && s.record_count == 8 &&
          s.records_not_retained > 0);
    CHECK(f.zero == s.starved_frames + s.hold_frames);
  }
}
void queue_counter_wrap() {
  int32_t memory[28] = {}, out[12] = {};
  I2sRing ring(memory, 14);
  ring.read_ = ring.write_ = UINT32_MAX - 7;
  PcmRecovery policy;
  CHECK(policy.begin(7, 14, 2));
  std::vector<int32_t> expected;
  unsigned next = 1, consumed = 0;
  auto push = [&](unsigned n) {
    std::vector<int32_t> data(n * 2);
    for (unsigned i = 0; i < n; ++i) {
      int32_t word =
          (next & 1) ? INT32_MIN + int32_t(next) : INT32_MAX - int32_t(next);
      ++next;
      data[2 * i] = word;
      data[2 * i + 1] = ~word;
    }
    CHECK(ring.push(data.data(), n) == n);
    expected.insert(expected.end(), data.begin(), data.end());
  };
  auto verify = [&](unsigned real) {
    for (unsigned i = 0; i < real * 2; ++i)
      CHECK(out[i] == expected[consumed++]);
  };
  push(11);
  CHECK(ring.prime(out, 2));
  verify(4);
  CHECK(ring.queued() == 7 && ring.write_ == 3);
  for (unsigned i = 0; i < 3; ++i) {
    CHECK(policy.refill(ring, out, i) == 2);
    verify(2);
  }
  CHECK(policy.refill(ring, out, 3) == 1);
  verify(1);
  CHECK(out[2] == 0 && out[3] == 0 && ring.queued() == 0 &&
        policy.snapshot().recovering);
  push(10);
  auto r = ring.read_;
  auto pos = ring.readPos_;
  CHECK(policy.refill(ring, out, UINT32_MAX) == 0 && ring.read_ == r &&
        ring.readPos_ == pos && ring.queued() == 10);
  push(1);
  CHECK(policy.refill(ring, out, 0) == 2);
  verify(2);
  CHECK(ring.queued() == 9 && policy.snapshot().resumes == 1 &&
        policy.snapshot().threshold == 11);
  auto s = policy.snapshot();
  CHECK(s.starved_frames == 1 && s.hold_frames == 2);
  CHECK(s.records[0].kind == 1 && s.records[1].kind == 2 &&
        s.records[1].at_us == 0);
  policy.end(1, uint32_t(ring.queued()));
  CHECK(!policy.snapshot().active && !policy.snapshot().recovering);
}
int main() {
  recovery_counterexample();
  partial_order_and_threshold();
  zero_values_and_stop();
  geometry_wrap_and_records();
  queue_counter_wrap();
  printf("PASS actual PcmRecovery %u checks sizeof_ring=%zu snapshot=%zu "
         "policy=%zu; "
         "partial/sampleorder/holdthreshold/jitter/geometry/wrap/stop/"
         "zero-value/reset/telemetry\n",
         checks, sizeof(I2sRing), sizeof(PcmRecoverySnapshot),
         sizeof(PcmRecovery));
}
