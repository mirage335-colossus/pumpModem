// Slewed keyed phase through a high-Q transmit antenna: what the slew costs the receiver and what
// it spares the transmitter.
//
// Theoretical calculation, 28 November 2026 (Fable-5.2 pass). Assumptions:
//  - The antenna is one single-tuned series circuit of -3 dB bandwidth B, resonant at the centre of
//    the positions, driven by a constant-amplitude voltage source. Its current envelope y follows
//    dy/dt = (-pi*B + j*2*pi*fr) * y + pi*B * x, with x the drive envelope and fr the resonance, both
//    in the lattice's baseband frame. The step is exact for a drive held at its mid-step value.
//  - Sixteen positions 0.78125 Hz apart (lattice scale j = 3, 1.28 s cell). Every tone gets a random
//    position and a random keyed phase of 0, 90, 180 or 270 degrees.
//  - The keyed phase of a tone is reached by a linear phase ramp over the first S seconds of the
//    tone, the shorter way round. A half turn goes either in a random (keyed) direction, or in the
//    direction that moves the instantaneous frequency towards the resonance.
//  - "Power" is the cycle-averaged power the source delivers, Re(x * conj(y)), in units of the power
//    delivered on resonance in the steady state. Negative values are power returned to the source.
//    The load seen by the source is taken as R * x / y, and the reflection coefficient against R as
//    (x - y) / (x + y); both are indicators, not a transmitter model.
//  - No transmitter, matching network or voltage limit is modelled, and nothing was measured.
//
// Build: g++ -O3 -march=native -std=c++20 -fopenmp phase_slew.cpp -o /tmp/phase_slew
// Run:   /tmp/phase_slew > results/phase_slew.md

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using cd = std::complex<double>;
static const double PI = 3.14159265358979323846;
static const double FS = 6400.0;          // envelope steps per second
static const double DELTA = 0.78125;      // position spacing, Hz
static const int POS = 16;
static const int CELL = 8192;             // steps per 1.28 s cell

struct Res {
  double cohKnown, cohStep, cohBlank;     // coherent sum over tones, one phase for all positions
  double ptKnown, ptStep;                 // mean energy per tone
  double maxAngle;                        // largest angle between drive and current, degrees
  double pMin;                            // lowest delivered power, in units of the matched power
  double returned;                        // returned energy per tone, in seconds of matched power
  double gammaMax;                        // largest |reflection coefficient|
  double timeAbove;                       // share of time with the angle above 30 degrees
};

static Res run(double B, int cells, double S, int tones, int towardCentre) {
  const double dt = 1.0 / FS, a = PI * B, fr = 0.5 * (POS - 1) * DELTA;
  const cd lam(-a, 2 * PI * fr);
  const cd E = std::exp(lam * dt), G = (a / (-lam)) * (cd(1, 0) - E);
  const int N = cells * CELL, warm = 4;
  std::mt19937_64 rng(20261128);
  std::uniform_int_distribution<int> upos(0, POS - 1), uq(0, 3), ub(0, 1);
  cd y(0, 0), sKnown(0, 0), sStep(0, 0), sBlank(0, 0);
  double eKnown = 0, eStep = 0, maxAngle = 0, pMin = 1e9, ret = 0, gmax = 0, above = 0;
  int qPrev = 0;
  long counted = 0;
  for (int k = 0; k < tones + warm; ++k) {
    const int p = upos(rng), q = uq(rng), b = ub(rng);
    int dq = (q - qPrev + 4) % 4;                      // quarter turns to add
    double d = dq == 0 ? 0 : dq == 1 ? PI / 2 : dq == 3 ? -PI / 2 : (b ? PI : -PI);
    if (dq == 2 && towardCentre) d = (p * DELTA > fr) ? -PI : PI;
    const double ph0 = qPrev * PI / 2, ph1 = q * PI / 2, w = 2 * PI * p * DELTA;
    cd cKnown(0, 0), cStep(0, 0), cBlank(0, 0);
    for (int n = 0; n < N; ++n) {
      const double t = (n + 0.5) * dt;
      const double s = S > 0 ? std::min(1.0, t / S) : 1.0;
      const cd carrier = std::polar(1.0, w * t);
      const cd x = carrier * std::polar(1.0, ph0 + d * s);
      const cd xs = carrier * std::polar(1.0, ph1);     // the receiver's reference for a plain step
      const cd yNext = E * y + G * x;
      const cd ym = 0.5 * (y + yNext);
      y = yNext;
      if (k < warm) continue;
      cKnown += ym * std::conj(x);
      cStep += ym * std::conj(xs);
      if (t >= S) cBlank += ym * std::conj(xs);
      const cd v = x * std::conj(ym);
      const double pw = v.real(), ang = std::fabs(std::atan2(v.imag(), v.real())) * 180 / PI;
      maxAngle = std::max(maxAngle, ang);
      pMin = std::min(pMin, pw);
      if (pw < 0) ret += -pw * dt;
      if (ang > 30) above += 1;
      const double den = std::abs(x + ym);
      gmax = std::max(gmax, den > 1e-9 ? std::abs(x - ym) / den : 1e9);
      ++counted;
    }
    qPrev = q;
    if (k < warm) continue;
    cKnown /= N; cStep /= N; cBlank /= N;
    sKnown += cKnown; sStep += cStep; sBlank += cBlank;
    eKnown += std::norm(cKnown); eStep += std::norm(cStep);
  }
  auto db = [](double g) { return -10 * std::log10(g); };
  Res r;
  r.cohKnown = db(std::norm(sKnown) / (double(tones) * tones));
  r.cohStep = db(std::norm(sStep) / (double(tones) * tones));
  r.cohBlank = db(std::norm(sBlank) / (double(tones) * tones));
  r.ptKnown = db(eKnown / tones);
  r.ptStep = db(eStep / tones);
  r.maxAngle = maxAngle; r.pMin = pMin; r.returned = ret / tones; r.gammaMax = gmax;
  r.timeAbove = above / double(counted);
  return r;
}

int main() {
  const double Bs[] = {24, 57, 86, 174};
  const double Ss[] = {0, 0.02, 0.04, 0.08, 0.16};
  const int cellsList[] = {8, 1};
  const double P0 = 10000.0;
  struct Job { int ci, bi, si, mode; Res r; };
  std::vector<Job> jobs;
  for (int ci = 0; ci < 2; ++ci)
    for (int bi = 0; bi < 4; ++bi)
      for (int si = 0; si < 5; ++si)
        for (int mode = 0; mode < 2; ++mode) jobs.push_back({ci, bi, si, mode, {}});
#pragma omp parallel for schedule(dynamic)
  for (size_t i = 0; i < jobs.size(); ++i) {
    Job& j = jobs[i];
    const int cells = cellsList[j.ci];
    j.r = run(Bs[j.bi], cells, Ss[j.si], cells == 8 ? 1000 : 8000, j.mode);
  }
  std::printf("# Slewed keyed phase through a high-Q antenna\n\n");
  std::printf("Theoretical: single-tuned antenna, constant-amplitude drive, sixteen positions 0.78125 Hz apart\n");
  std::printf("centred on the resonance, random positions and keyed quarter turns, linear phase ramp of length S\n");
  std::printf("at the start of each tone. 1,000 tones of 10.24 s or 8,000 tones of 1.28 s per row. Power figures\n");
  std::printf("are for 10 kW delivered on resonance. Assumptions are in the head of `phase_slew.cpp`.\n\n");
  std::printf("Static reference, steady tone on an outer position (5.86 Hz from resonance): angle between drive and\n");
  std::printf("current, and standing-wave ratio against the resonant resistance:\n\n");
  std::printf("| Antenna | Angle | VSWR |\n| ---: | ---: | ---: |\n");
  for (double B : Bs) {
    const double u = 2 * 0.5 * (POS - 1) * DELTA / B, g = u / std::sqrt(4 + u * u);
    std::printf("| %.0f Hz | %.1f deg | %.2f |\n", B, std::atan(u) * 180 / PI, (1 + g) / (1 - g));
  }
  for (int ci = 0; ci < 2; ++ci) {
    const int cells = cellsList[ci];
    std::printf("\n## Tone of %d cell%s = %.2f s\n\n", cells, cells > 1 ? "s" : "", cells * 1.28);
    std::printf("### Receiver: loss in dB\n\n");
    std::printf("Coherent sum over tones with one phase for all positions, for a receiver whose reference follows\n");
    std::printf("the ramp, one that assumes a plain step at the start of the tone, and one that leaves the ramp\n");
    std::printf("interval out; then the mean energy per tone for the first two.\n\n");
    std::printf("| Antenna | Ramp S | Coherent, ramp known | Coherent, step assumed | Coherent, ramp left out | Per tone, ramp known | Per tone, step assumed |\n");
    std::printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
    for (const Job& j : jobs)
      if (j.ci == ci && j.mode == 0)
        std::printf("| %.0f Hz | %.0f ms | %.3f | %.3f | %.3f | %.3f | %.3f |\n", Bs[j.bi], Ss[j.si] * 1000,
                    j.r.cohKnown, j.r.cohStep, j.r.cohBlank, j.r.ptKnown, j.r.ptStep);
    std::printf("\n### Transmitter\n\n");
    std::printf("Largest angle between drive and antenna current, lowest delivered power (negative: returned to\n");
    std::printf("the source), energy returned per tone on average, largest standing-wave ratio against the\n");
    std::printf("resonant resistance (none where the current opposes the drive), and the share of time with the angle above 30 degrees. \"Keyed\" and\n");
    std::printf("\"inward\" are the two rules for the direction of a half turn.\n\n");
    std::printf("| Antenna | Ramp S | Half turn | Largest angle | Lowest power | Returned per tone | Largest VSWR | Time above 30 deg |\n");
    std::printf("| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |\n");
    for (const Job& j : jobs)
      if (j.ci == ci) {
        char vs[32];
        if (j.r.gammaMax >= 0.999) std::snprintf(vs, sizeof vs, "none: power returned");
        else std::snprintf(vs, sizeof vs, "%.2f", (1 + j.r.gammaMax) / (1 - j.r.gammaMax));
        std::printf("| %.0f Hz | %.0f ms | %s | %.1f deg | %+.2f kW | %.2f J | %s | %.4f%% |\n", Bs[j.bi],
                    Ss[j.si] * 1000, j.mode ? "inward" : "keyed", j.r.maxAngle, j.r.pMin * P0 / 1000,
                    j.r.returned * P0, vs, 100 * j.r.timeAbove);
      }
  }
  std::printf("\n## Closed forms for a plain step\n\n");
  std::printf("After a half turn the delivered power is 1 - 2*exp(-pi*B*t) of the matched power: negative for\n");
  std::printf("ln(2)/(pi*B), returning (1 - ln 2)/(pi*B) seconds of matched power. After a quarter turn it is\n");
  std::printf("1 - exp(-pi*B*t): it falls to zero and nothing is returned.\n\n");
  std::printf("| Antenna | Stored energy at 10 kW | Reverse-power time after a half turn | Energy returned by one half turn |\n");
  std::printf("| ---: | ---: | ---: | ---: |\n");
  for (double B : Bs)
    std::printf("| %.0f Hz | %.1f J | %.2f ms | %.1f J |\n", B, P0 / (2 * PI * B), 1000 * std::log(2.0) / (PI * B),
                P0 * (1 - std::log(2.0)) / (PI * B));
  return 0;
}
