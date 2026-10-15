#pragma once
#include <stdint.h>
#include <string.h>

namespace program {
constexpr uint8_t MAX_ROWS = 16;
constexpr uint32_t MIN_MS = 100;
constexpr uint32_t MAX_MS = 604800000UL; // Seven days per ON or OFF phase.
constexpr uint32_t MAX_REPEATS = 1000000UL;
enum class Mode : uint8_t { Manual, Daily, Cycle };
struct Window { uint32_t on, off; }; // Local seconds since midnight; [on,off).
struct Step { uint32_t onMs, offMs; };
struct Config {
  Mode mode;
  uint8_t dailyCount, stepCount;
  bool pwm;
  uint16_t frequency; // 40..20000 Hz; zero frequency is represented by PWM disabled.
  uint8_t duty; // 1..99 percent; disabled PWM means steady DC switching.
  uint32_t repeats; // Zero means repeat the complete row sequence forever.
  Window daily[MAX_ROWS];
  Step steps[MAX_ROWS];
};
struct Runtime {
  bool manualOn, running;
  uint64_t anchorMs, offsetMs;
};
struct Progress {
  bool requested, finished;
  uint8_t row;
  uint64_t completed, remainingMs, elapsedMs;
};
constexpr uint64_t resumeOffset(uint64_t saved,uint64_t gap,bool countOutage,uint32_t restartAfterSeconds){
  if(restartAfterSeconds && gap>static_cast<uint64_t>(restartAfterSeconds)*1000ULL)return 0;
  return saved+(countOutage?gap:0);
}
constexpr Config defaults() {
  Config c = {};
  c.mode = Mode::Manual; c.dailyCount = c.stepCount = 1;
  c.frequency = 50; c.duty = 50;
  c.daily[0] = {28800, 72000}; c.steps[0] = {60000, 60000};
  return c;
}
constexpr bool inside(uint32_t second, Window w) {
  if (second >= 86400 || w.on >= 86400 || w.off >= 86400 || w.on == w.off) return false;
  return w.on < w.off ? second >= w.on && second < w.off : second >= w.on || second < w.off;
}
constexpr bool overlap(Window a, Window b) {
  // Each circular interval has positive length. An overlap contains a start.
  return inside(a.on, b) || inside(b.on, a);
}
constexpr uint64_t period(const Config &c) {
  uint64_t result = 0;
  for (uint8_t i = 0; i < c.stepCount && i < MAX_ROWS; ++i)
    result += static_cast<uint64_t>(c.steps[i].onMs) + c.steps[i].offMs;
  return result;
}
constexpr bool valid(const Config &c) {
  if (static_cast<uint8_t>(c.mode) > 2 || c.dailyCount < 1 || c.dailyCount > MAX_ROWS ||
      c.stepCount < 1 || c.stepCount > MAX_ROWS || c.repeats > MAX_REPEATS ||
      c.frequency < 40 || c.frequency > 20000 || c.duty < 1 || c.duty > 99) return false;
  for (uint8_t i = 0; i < c.dailyCount; ++i) {
    const Window w = c.daily[i];
    if (w.on >= 86400 || w.off >= 86400 || w.on == w.off) return false;
    for (uint8_t j = 0; j < i; ++j) if (overlap(w, c.daily[j])) return false;
  }
  for (uint8_t i = 0; i < c.stepCount; ++i) {
    const Step s = c.steps[i];
    if (s.onMs < MIN_MS || s.offMs < MIN_MS || s.onMs > MAX_MS || s.offMs > MAX_MS ||
        s.onMs % 100 || s.offMs % 100) return false;
  }
  return true;
}
constexpr Progress evaluate(const Config &c, const Runtime &r, uint64_t nowMs,
                         bool clockValid, uint32_t daySecond) {
  Progress p = {};
  if (!valid(c)) return p;
  if (c.mode == Mode::Manual) { p.requested = r.manualOn; return p; }
  if (c.mode == Mode::Daily) {
    if (clockValid) for (uint8_t i = 0; i < c.dailyCount; ++i)
      if (inside(daySecond, c.daily[i])) { p.requested = true; p.row = i; }
    return p;
  }
  if (!r.running || nowMs < r.anchorMs) return p;
  const uint64_t elapsed = r.offsetMs + nowMs - r.anchorMs;
  const uint64_t length = period(c);
  p.elapsedMs = elapsed; p.completed = elapsed / length;
  if (c.repeats && p.completed >= c.repeats) {
    p.finished = true; p.completed = c.repeats; p.elapsedMs = length * c.repeats;
    return p; // Finite programs always finish OFF.
  }
  uint64_t phase = elapsed % length;
  for (uint8_t i = 0; i < c.stepCount; ++i) {
    const uint64_t on = c.steps[i].onMs, duration = on + c.steps[i].offMs;
    if (phase < duration) {
      p.row = i; p.requested = phase < on;
      p.remainingMs = p.requested ? on - phase : duration - phase;
      return p;
    }
    phase -= duration;
  }
  return p;
}
constexpr void limited(const bool requested[2], const bool old[2], bool armed,
                    uint8_t limit, bool result[2]) {
  result[0] = result[1] = false;
  if (!armed) return;
  uint8_t n = 0;
  for (uint8_t i = 0; i < 2 && n < limit; ++i)
    if (requested[i] && old[i]) { result[i] = true; ++n; }
  for (uint8_t i = 0; i < 2 && n < limit; ++i)
    if (requested[i] && !result[i]) { result[i] = true; ++n; }
}
inline bool selfTest() {
  Config c = defaults(); c.mode = Mode::Daily; c.dailyCount = 2;
  c.daily[0] = {25200,28800}; c.daily[1] = {32400,46800};
  Runtime r = {}; r.running = true; r.anchorMs = 1000;
  for (uint32_t second = 0; second < 86400; ++second) {
    const bool expected = (second >= 25200 && second < 28800) || (second >= 32400 && second < 46800);
    if (evaluate(c,r,1000,true,second).requested != expected) return false;
  }
  if (evaluate(c,r,1000,false,25200).requested) return false;
  c.daily[1] = {86399,1}; if (!valid(c) || !inside(0,c.daily[1]) || inside(1,c.daily[1])) return false;
  c.daily[1] = {27000,30000}; if (valid(c)) return false;
  c = defaults(); c.mode = Mode::Cycle; c.stepCount = 2; c.repeats = 2;
  c.steps[0] = {100,200}; c.steps[1] = {300,400};
  if (!valid(c) || period(c) != 1000) return false;
  if (!evaluate(c,r,1099,false,0).requested || evaluate(c,r,1100,false,0).requested) return false;
  if (!evaluate(c,r,1300,false,0).requested || evaluate(c,r,1600,false,0).requested) return false;
  if (evaluate(c,r,2999,false,0).finished || !evaluate(c,r,3000,false,0).finished) return false;
  r.offsetMs = 600; if (evaluate(c,r,1000,false,0).requested) return false;
  c.repeats = 0; r.anchorMs = 4294967290ULL; r.offsetMs = 0;
  if (!evaluate(c,r,r.anchorMs+5000000000000ULL,false,0).requested) return false;
  c.steps[0].onMs = 50; if (valid(c)) return false;
  for (uint8_t a = 0; a < 4; ++a) for (uint8_t b = 0; b < 4; ++b) {
    bool q[2] = {bool(a&1),bool(a&2)}, o[2] = {bool(b&1),bool(b&2)}, result[2];
    limited(q,o,false,2,result); if (result[0] || result[1]) return false;
    limited(q,o,true,1,result);
    if ((result[0] && result[1]) || (result[0] && !q[0]) || (result[1] && !q[1])) return false;
    if (a && !result[0] && !result[1]) return false;
    limited(q,o,true,2,result);if(result[0]!=q[0]||result[1]!=q[1])return false;
  }
  return true;
}
}
