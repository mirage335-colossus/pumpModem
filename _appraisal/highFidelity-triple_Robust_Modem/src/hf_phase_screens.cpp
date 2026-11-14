#include "appraisal/hf_phase_screens.hpp"

#include "appraisal/hf_fullwave_kernel.hpp"
#include "search_fft.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <type_traits>

namespace appraisal {
namespace {
using Table = HfPhaseScreensTable;
using Complex = std::complex<double>;
namespace fft = datapump::modem::detail::search_fft;

constexpr double pi = std::numbers::pi;
constexpr double electron_radius_m = 2.8179403205e-15;
constexpr double tecu = 1e16, thickness_m = 3000., ambient_tecu = .018;
constexpr double grid_step_m = 125., witness_step_m = Table::position_step_m / 4.;
constexpr double half_rate = .5 * Table::sample_rate_hz;
constexpr std::uint64_t maximum_samples = Table::horizon_seconds * Table::sample_rate_hz;
constexpr std::size_t nx = 1024, ny = 256, dense_points = 4 * Table::position_intervals + 1;
constexpr std::size_t reference_lag = Table::first_lag_samples() + Table::modelling_delay_samples;
constexpr std::size_t frequency_bins = 256, rf_knots = 9;
constexpr double rf_step = 2 * half_rate / (rf_knots - 1);
constexpr double earliest_samples = Table::total_distance_m / Table::light_speed_mps * Table::sample_rate_hz;
constexpr HfFullwaveKernelDesign fir_design{Table::sample_rate_hz, Table::occupied_half_band_hz,
    earliest_samples, Table::modelling_delay_samples, Table::retained_late_samples};
constexpr std::array witness_offsets{-384., -103.7, 0., 196.5, 384.};
constexpr double mesh_tol = .001, box_tol = .001, slice_tol = .012;
constexpr double frequency_tol = .00005, trajectory_tol = .0005, fir_tol = .0005;
constexpr double power_tol = 5e-11, large_angle_tol = 1e-6, edge_spectrum_tol = 1e-8;
constexpr double retained_energy_tol = 1e-6, retarded_tol = .005, null_floor = .002;
constexpr double relative_tol = .10, null_anchor_tol = 1e-11;
constexpr auto null_window_half_steps = static_cast<std::size_t>(Table::null_window_radius_m / Table::null_window_step_m);
constexpr auto null_window_points = 2 * null_window_half_steps + 1;
static_assert(dense_points * witness_step_m == 2 * Table::position_half_span_m + witness_step_m);
static_assert(null_window_half_steps * Table::null_window_step_m == witness_step_m);
static_assert(Table::first_lag_samples() == 4 && Table::last_lag_samples() == 84);

struct Clump { double x, y, sx, sy, delta; };
struct Slab { double z, ambient; std::array<Clump, 4> clumps; };
using Geometry = std::array<Slab, 2>;

constexpr std::array<Geometry, 2> fields{{
    {{{110000., ambient_tecu, {{{-9000., -1400., 1800., 2600., -.0085},
        {-2600., 2400., 1650., 2300., .0072}, {6200., 900., 2200., 1400., -.0090},
        {14000., -1900., 2700., 1700., .0043}}}},
      {300000., ambient_tecu, {{{-13000., 1800., 2100., 1250., .0068},
        {-4600., -1000., 1350., 1650., -.00857}, {4000., 600., 2600., 2100., -.0082},
        {11000., 2100., 1900., 1600., .0055}}}}}},
    {{{110000., ambient_tecu, {{{-11100., 1650., 2100., 1950., -.0091},
        {-3800., -1100., 1750., 2400., .0061}, {5200., 1400., 1950., 1700., -.0088},
        {13300., 1200., 2150., 2550., .0057}}}},
      {300000., ambient_tecu, {{{-15100., -850., 2400., 1650., .0059},
        {-6600., 950., 1700., 1900., -.0077}, {3200., -600., 2200., 1500., -.0094},
        {9700., -1700., 1650., 2050., .0065}}}}}}
}};

double x_at(double sample, const HfPhaseScreensSpec& spec) {
    return -spec.drift_mps * ((sample - Table::modelling_delay_samples) / Table::sample_rate_hz - spec.closest_seconds);
}

void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("HF phase screen preparation cancelled");
}

std::size_t capacity_bytes(const auto& v) {
    return v.capacity() * sizeof(typename std::decay_t<decltype(v)>::value_type);
}

double signed_index(std::size_t i, std::size_t n) {
    return static_cast<double>(i) - (i < n / 2 ? 0. : static_cast<double>(n));
}

struct Samples {
    std::vector<Complex> v, d; // deembedded RF voltage and derivative per metre
    std::vector<Complex> row_spectrum; // unscaled FFT of the physical row
    std::size_t bytes() const { return capacity_bytes(v) + capacity_bytes(d) + capacity_bytes(row_spectrum); }
};

struct NullSamples {
    struct Point { double x_m = 0.; std::array<Complex, 3> field{}; }; // 4, 8 and 16 slices
    std::array<Point, null_window_points> points{};
    std::size_t count = 0;
};

Complex spectral_row_at(std::span<const Complex> spectrum, double x) {
    // The row starts at the negative box edge. The forward FFT is unscaled
    // and uses negative phase, so reconstruction uses the signed positive phase.
    const double width_m = spectrum.size() * grid_step_m;
    const double phase = 2 * pi * (.5 + x / width_m);
    Complex sum{};
    for (std::size_t k = 0; k < spectrum.size(); ++k)
        sum += spectrum[k] * std::polar(1., phase * signed_index(k, spectrum.size()));
    return sum / static_cast<double>(spectrum.size());
}

// A Gap records identical-position diagonal operators. No FFT is needed
// between homogeneous vacuum/ambient segments without a phase kick.
struct Gap { double vacuum = 0.; std::array<double, 2> plasma{}; };

class FieldSolver {
public:
    FieldSolver(std::size_t width, std::size_t height, double step, const Geometry& slabs,
                double drift_mps, std::stop_token stop)
        : width_(width), height_(height), step_(step), slabs_(slabs), stop_(stop),
          field_(width * height), mask_(field_.size()), column_(height), kx2_(width), ky2_(height),
          line_(width), oversampled_(static_cast<std::size_t>(width * step / witness_step_m)),
          plan_(std::max({width, height, oversampled_.size()}), stop) {
        for (std::size_t i = 0; i < width; ++i)
            kx2_[i] = std::pow(2 * pi * signed_index(i, width) / (width * step), 2);
        for (std::size_t i = 0; i < height; ++i)
            ky2_[i] = std::pow(2 * pi * signed_index(i, height) / (height * step), 2);
        for (std::size_t s = 0; s < slabs_.size(); ++s) {
            delta_[s].resize(field_.size());
            if (std::none_of(slabs_[s].clumps.begin(), slabs_[s].clumps.end(), [](auto p) { return p.delta != 0.; }))
                continue;
            // At physical output time t, slab center is evaluated at
            // t - outgoing nominal flight. Common drift then factors into a
            // translation of the solved field, regardless of slab ordering.
            const double retard_x = drift_mps * (Table::total_distance_m - slabs_[s].z) / Table::light_speed_mps;
            std::vector<double> x_factor(width), y_factor(height);
            for (const auto& p : slabs_[s].clumps) {
                if (p.delta == 0.) continue;
                for (std::size_t i = 0; i < width; ++i) {
                    const double dx = (static_cast<double>(i) - width / 2.) * step + retard_x - p.x;
                    x_factor[i] = p.delta * std::exp(-.5 * std::pow(dx / p.sx, 2));
                }
                for (std::size_t j = 0; j < height; ++j) {
                    const double dy = (static_cast<double>(j) - height / 2.) * step - p.y;
                    y_factor[j] = std::exp(-.5 * std::pow(dy / p.sy, 2));
                }
                for (std::size_t j = 0; j < height; ++j) {
                    cancelled(stop_);
                    for (std::size_t i = 0; i < width; ++i)
                        delta_[s][j * width + i] += x_factor[i] * y_factor[j];
                }
            }
        }
    }

    std::size_t bytes() const {
        return sizeof(*this) + capacity_bytes(field_) + capacity_bytes(mask_) + capacity_bytes(column_) +
            capacity_bytes(kx2_) + capacity_bytes(ky2_) + capacity_bytes(line_) + capacity_bytes(oversampled_) +
            plan_.working_bytes() + capacity_bytes(delta_[0]) + capacity_bytes(delta_[1]) +
            (width_ + height_) * sizeof(double); // density construction vectors
    }

    void run(double offset, unsigned slices, bool perturbations, bool collapse, HfPhaseScreensChecks& out) {
        ++out.field_solves;
        set_frequency(offset);
        std::fill(field_.begin(), field_.end(), Complex{1., 0.});
        if (!perturbations) return;
        double previous_exit = 0.;
        Gap gap;
        bool pristine = true;
        const auto propagate_to_kick = [&] {
            if (!pristine) propagate(gap, out);
            pristine = false;
            gap = {};
        };
        for (std::size_t s = 0; s < slabs_.size(); ++s) {
            const double entry = slabs_[s].z - .5 * thickness_m;
            gap.vacuum += entry - previous_exit;
            previous_exit = entry + thickness_m;
            if (collapse) {
                if (s == 0) {
                    gap.plasma[s] += .5 * thickness_m;
                    propagate_to_kick();
                    for (std::size_t c = 0; c < slabs_.size(); ++c) {
                        make_mask(c, thickness_m);
                        kick();
                    }
                    gap.plasma[s] += .5 * thickness_m;
                } else {
                    gap.plasma[s] += thickness_m;
                }
                continue;
            }
            const auto dz = thickness_m / slices;
            make_mask(s, dz);
            gap.plasma[s] += .5 * dz;
            for (unsigned j = 0; j < slices; ++j) {
                propagate_to_kick();
                kick();
                gap.plasma[s] += j + 1 == slices ? .5 * dz : dz;
            }
        }
        gap.vacuum += Table::total_distance_m - previous_exit;
        propagate(gap, out, true);
    }

    void gaussian_witness(HfPhaseScreensChecks& out) {
        set_frequency(0.);
        std::fill(field_.begin(), field_.end(), Complex{1., 0.});
        make_mask(0, thickness_m);
        kick();
        // 200 km incident / 400 km outgoing spherical geometry has the
        // same normalized one-screen Fresnel integral at D_eff=133 1/3 km.
        propagate({400000. / 3., {}}, out, true);
    }

    Complex spherical_gaussian(double offset, double displacement, unsigned slices, bool ambient_diffraction) {
        set_frequency(offset);
        std::fill(field_.begin(), field_.end(), Complex{1., 0.});
        constexpr double center = 200000., entry = center - .5 * thickness_m;
        const double excess = ambient_diffraction ? 1. / index_[0] - 1. : 0.;
        const auto background = [&](double z) { return z + excess * std::clamp(z - entry, 0., thickness_m); };
        const double b0 = background(center);
        const auto step = [&](double b1, double b2) { return b0 * b0 * (b2 - b1) / (b1 * b2); };
        double previous = 0.;
        HfPhaseScreensChecks metrics;
        std::vector<double> dx(width_), dy(height_);
        const double dz = thickness_m / slices;
        for (unsigned s = 0; s < slices; ++s) {
            const double b = background(entry + (s + .5) * dz);
            if (s > 0) propagate({step(previous, b), {}}, metrics, false, true);
            previous = b;
            const double scale = b / b0;
            for (std::size_t i = 0; i < width_; ++i) {
                const double x = (static_cast<double>(i) - width_ / 2.) * step_ * scale - displacement;
                dx[i] = -.00857 * std::exp(-.5 * std::pow(x / 1600., 2));
            }
            for (std::size_t j = 0; j < height_; ++j) {
                const double y = (static_cast<double>(j) - height_ / 2.) * step_ * scale;
                dy[j] = std::exp(-.5 * std::pow(y / 2400., 2));
                cancelled(stop_);
                for (std::size_t i = 0; i < width_; ++i) delta_[0][j * width_ + i] = dx[i] * dy[j];
            }
            make_mask(0, dz);
            kick();
        }
        propagate({step(previous, background(Table::total_distance_m)), {}}, metrics, true, true);
        return field_[(height_ / 2) * width_ + width_ / 2];
    }

    Samples sample(double impact, bool derivative, bool retain_row_spectrum = false) {
        const auto y = static_cast<std::size_t>(static_cast<long long>(height_ / 2) + std::llround(impact / step_));
        auto row = std::span<const Complex>(field_).subspan(y * width_, width_);
        std::copy(row.begin(), row.end(), line_.begin());
        fft::fft(line_, false, plan_, stop_);
        Samples out;
        const auto pad = [&](std::vector<Complex>& dest, bool slope) {
            std::fill(oversampled_.begin(), oversampled_.end(), Complex{});
            const double scale = static_cast<double>(oversampled_.size()) / width_;
            for (std::size_t i = 0; i < width_; ++i) {
                const double qx = 2 * pi * signed_index(i, width_) / (step_ * width_);
                const auto mapped = i < width_ / 2 ? i : i + oversampled_.size() - width_;
                oversampled_[mapped] = slope ? line_[i] * Complex{0., scale * qx} : line_[i] * scale;
            }
            fft::fft(oversampled_, true, plan_, stop_);
            const std::size_t first = oversampled_.size() / 2 - 2 * Table::position_intervals;
            dest.assign(oversampled_.begin() + first, oversampled_.begin() + first + dense_points);
        };
        pad(out.v, false);
        if (derivative) pad(out.d, true);
        if (retain_row_spectrum) out.row_spectrum = line_;
        return out;
    }

private:
    void set_frequency(double offset) {
        const double wavelength = Table::light_speed_mps / (Table::carrier_hz + offset);
        k_ = 2 * pi / wavelength;
        plasma_per_tecu_ = electron_radius_m * wavelength * wavelength * tecu / (pi * thickness_m);
        for (std::size_t s = 0; s < slabs_.size(); ++s) {
            index_[s] = std::sqrt(1. - plasma_per_tecu_ * slabs_[s].ambient);
            kn_[s] = k_ * index_[s];
        }
    }

    void make_mask(std::size_t slab, double distance) {
        const double background = slabs_[slab].ambient;
        for (std::size_t j = 0; j < height_; ++j) {
            cancelled(stop_);
            for (std::size_t i = j * width_; i < (j + 1) * width_; ++i) {
                const auto d = delta_[slab][i];
                const double density_ratio = (background + d) * plasma_per_tecu_;
                if (!(density_ratio >= 0. && density_ratio <= .1))
                    throw std::invalid_argument("HF phase screens leave the underdense scalar domain");
                const double relative_index = -plasma_per_tecu_ * d / (std::sqrt(1. - density_ratio) + index_[slab]);
                mask_[i] = std::polar(1., k_ * distance * relative_index);
            }
        }
    }

    void kick() {
        for (std::size_t j = 0; j < height_; ++j) {
            cancelled(stop_);
            for (std::size_t i = j * width_; i < (j + 1) * width_; ++i) field_[i] *= mask_[i];
        }
    }

    void propagate(const Gap& gap, HfPhaseScreensChecks& out, bool final = false, bool scaled_paraxial = false) {
        // Forward RF sign exp(-i ωt): the exact residual longitudinal
        // wavenumber is -q²/(k*n+sqrt((k*n)²-q²)), with no cancellation.
        // Only the propagating branch is admitted by this small-angle model.
        for (std::size_t j = 0; j < height_; ++j)
            fft::fft(std::span(field_).subspan(j * width_, width_), false, plan_, stop_);
        const double denominator_nyquist = std::pow(.75 * pi / step_, 2);
        const double angle_wavenumber = std::pow(.10 * std::min({k_, kn_[0], kn_[1]}), 2);
        double total = 0., edge = 0., angular = 0., flux = 0.;
        for (std::size_t i = 0; i < width_; ++i) {
            for (std::size_t j = 0; j < height_; ++j) column_[j] = field_[j * width_ + i];
            fft::fft(column_, false, plan_, stop_);
            for (std::size_t j = 0; j < height_; ++j) {
                const double q2 = kx2_[i] + ky2_[j];
                if (scaled_paraxial) {
                    column_[j] *= std::polar(1., -q2 * gap.vacuum / (2 * k_));
                    continue;
                }
                const double p = std::norm(column_[j]);
                total += p;
                if (kx2_[i] >= denominator_nyquist || ky2_[j] >= denominator_nyquist) edge += p;
                if (q2 >= angle_wavenumber) angular += p;
                const double vacuum_z = std::sqrt(k_ * k_ - q2);
                if (final) flux += p * vacuum_z / k_;
                double distance = gap.vacuum / (k_ + vacuum_z);
                for (std::size_t s = 0; s < kn_.size(); ++s)
                    if (gap.plasma[s] != 0.)
                        distance += gap.plasma[s] / (kn_[s] + std::sqrt(kn_[s] * kn_[s] - q2));
                column_[j] *= std::polar(1., -q2 * distance);
            }
            fft::fft(column_, true, plan_, stop_);
            for (std::size_t j = 0; j < height_; ++j) field_[j * width_ + i] = column_[j];
        }
        for (std::size_t j = 0; j < height_; ++j)
            fft::fft(std::span(field_).subspan(j * width_, width_), true, plan_, stop_);
        if (scaled_paraxial) return; // diagnostic scaled coordinates, not the physical plane flux
        const double n = field_.size();
        const double power = total / (n * n);
        if (!std::isfinite(power) || power <= 0.) throw std::runtime_error("nonfinite phase screen plane power");
        out.maximum_plane_power_error = std::max(out.maximum_plane_power_error, std::abs(power - 1.));
        out.maximum_edge_spectral_fraction = std::max(out.maximum_edge_spectral_fraction, edge / total);
        out.maximum_large_angle_fraction = std::max(out.maximum_large_angle_fraction, angular / total);
        if (final) out.minimum_longitudinal_flux = std::min(out.minimum_longitudinal_flux, flux / (n * n));
    }

    std::size_t width_, height_;
    double step_;
    const Geometry& slabs_;
    std::stop_token stop_;
    std::vector<Complex> field_, mask_, column_;
    std::vector<double> kx2_, ky2_;
    std::vector<Complex> line_, oversampled_;
    fft::DoublePlan plan_;
    std::array<std::vector<double>, 2> delta_;
    double k_ = 0., plasma_per_tecu_ = 0.;
    std::array<double, 2> index_{}, kn_{};
};

double uniform_phase(double offset) {
    const double wavelength = Table::light_speed_mps / (Table::carrier_hz + offset);
    const double r = electron_radius_m * wavelength * wavelength * ambient_tecu * tecu / (pi * thickness_m);
    const double excess = -2 * thickness_m * r / (1. + std::sqrt(1. - r));
    return 2 * pi / wavelength * (Table::total_distance_m + excess);
}

double position(std::size_t dense_ix) { return dense_ix * witness_step_m - Table::position_half_span_m; }

void observe(HfPhaseScreensError& metric, Complex predicted, Complex reference, std::size_t i, double offset) {
    const double e = std::abs(predicted - reference);
    if (!std::isfinite(e)) throw std::runtime_error("nonfinite sampled phase screen response");
    if (e > metric.absolute_error) metric = {e, std::abs(reference), position(i), offset};
}

std::array<double, 4> weights(double u) {
    const double v = 1. - u;
    return {(1. + 2. * u) * v * v, u * v * v, (3. - 2. * u) * u * u, -u * u * v};
}

Complex blend(const std::array<double, 4>& w, Complex lo, Complex dl, Complex hi, Complex dh) {
    return w[0] * lo + w[1] * dl + w[2] * hi + w[3] * dh;
}

std::pair<std::size_t, double> bracket(double x) {
    if (!(std::isfinite(x) && std::abs(x) <= Table::position_half_span_m))
        throw std::invalid_argument("HF phase screens trajectory leaves the nonwrapped table");
    const double p = (x + Table::position_half_span_m) / Table::position_step_m;
    const std::size_t lo = std::min(static_cast<std::size_t>(p), Table::position_intervals - 1);
    return {lo, p - lo};
}

bool same_spec(const HfPhaseScreensSpec& a, const HfPhaseScreensSpec& b) {
    return a.perturbations == b.perturbations && a.field == b.field && a.impact_y_m == b.impact_y_m &&
        a.drift_mps == b.drift_mps && a.closest_seconds == b.closest_seconds;
}

} // namespace

void validate_hf_phase_screens_spec(const HfPhaseScreensSpec& s, std::uint64_t horizon) {
    if (s.field >= fields.size() || !std::isfinite(s.impact_y_m) || std::abs(s.impact_y_m) > 2000. ||
        std::fmod(s.impact_y_m, grid_step_m) != 0. || !(s.drift_mps > 0. && s.drift_mps <= 60.) ||
        !(s.closest_seconds >= 0. && s.closest_seconds <= Table::horizon_seconds) || horizon == 0 ||
        horizon > maximum_samples ||
        std::max(std::abs(x_at(-static_cast<double>(Table::last_lag_samples()), s)),
                 std::abs(x_at(static_cast<double>(horizon + Table::last_lag_samples()), s))) > Table::position_half_span_m)
        throw std::invalid_argument("HF phase screens need a supported field, aligned impact and whole nonwrapped trajectory");
}

HfPhaseScreensTable::HfPhaseScreensTable(const HfPhaseScreensSpec& spec)
    : spec_(spec), knots_(1 + position_intervals) {}

std::size_t HfPhaseScreensTable::workspace_bytes() const noexcept {
    return sizeof(*this) + capacity_bytes(knots_);
}

double HfPhaseScreensTable::retarded_peak_error_bound(double drift_mps) const {
    if (!(std::isfinite(drift_mps) && drift_mps >= 0.)) throw std::invalid_argument("invalid phase screen drift");
    return derivative_flight_bound_ * drift_mps / (position_step_m * sample_rate_hz);
}

Complex HfPhaseScreensTable::coefficient_at_position(std::size_t lag, double x) const {
    if (lag < first_lag_samples() || lag > last_lag_samples()) throw std::invalid_argument("invalid phase screen lag");
    const auto [i, f] = bracket(x);
    lag -= first_lag_samples();
    return blend(weights(f), knots_[i].values[lag], knots_[i].slopes[lag], knots_[i + 1].values[lag], knots_[i + 1].slopes[lag]);
}

Complex HfPhaseScreensTable::coefficient_knot_slope(std::size_t lag, std::size_t knot) const {
    if (lag < first_lag_samples() || lag > last_lag_samples() || knot >= knots_.size())
        throw std::invalid_argument("invalid phase screen lag or knot");
    return knots_[knot].slopes[lag - first_lag_samples()];
}

HfPhaseScreensTableBuild build_hf_phase_screens_table(const HfPhaseScreensSpec& spec, std::stop_token stop) {
    validate_hf_phase_screens_spec(spec, 1);
    cancelled(stop);
    HfPhaseScreensChecks checks;
    checks.deep_slab_qualified = true;
    checks.null_windows_qualified = true;
    auto slabs = fields[spec.field];
    if (!spec.perturbations)
        for (auto& slab : slabs) for (auto& clump : slab.clumps) clump.delta = 0.;

    std::array<Samples, rf_knots> frequency_nodes;
    std::array<Samples, witness_offsets.size()> witnesses;
    std::array<NullSamples, 3> null_samples;
    const auto peak_workspace = [&](std::size_t extra) {
        std::size_t bytes = extra + sizeof(null_samples) + 192u * 1024u; // FIR transient arrays and metadata
        for (const auto& node : frequency_nodes) bytes += node.bytes();
        for (const auto& node : witnesses) bytes += node.bytes();
        for (const auto& window : checks.null_windows) bytes += capacity_bytes(window.unresolved_ranges_m);
        checks.preparation_peak_workspace_bytes = std::max(checks.preparation_peak_workspace_bytes, bytes);
        if (bytes > Table::preparation_workspace_bound_bytes)
            throw std::runtime_error("HF phase screen preparation exceeded its admitted workspace");
    };
    const auto envelope = [&](double offset, std::size_t i, bool derivative) {
        const double p = (offset + half_rate) / rf_step;
        const auto lo = std::min(static_cast<std::size_t>(p), rf_knots - 2);
        const double f = p - lo;
        const auto& a = derivative ? frequency_nodes[lo].d : frequency_nodes[lo].v;
        const auto& b = derivative ? frequency_nodes[lo + 1].d : frequency_nodes[lo + 1].v;
        return (1. - f) * a[i] + f * b[i];
    };
    const auto compare = [&](HfPhaseScreensError& metric, const Samples& a, const Samples& b, double offset) {
        for (std::size_t i = 0; i < dense_points; ++i) observe(metric, a.v[i], b.v[i], i, offset);
    };

    {
        FieldSolver base(nx, ny, grid_step_m, slabs, spec.drift_mps, stop);
        for (std::size_t f = 0; f < frequency_nodes.size(); ++f) {
            base.run(f * rf_step - half_rate, 4, spec.perturbations, false, checks);
            frequency_nodes[f] = base.sample(spec.impact_y_m, true, spec.perturbations && f == rf_knots / 2);
        }
        for (std::size_t f = 0; f + 1 < frequency_nodes.size(); ++f) {
            const double offset = (f + .5) * rf_step - half_rate;
            base.run(offset, 4, spec.perturbations, false, checks);
            const auto mid = base.sample(spec.impact_y_m, false);
            for (std::size_t i = 0; i < dense_points; ++i)
                observe(checks.frequency, envelope(offset, i, false), mid.v[i], i, offset);
            peak_workspace(base.bytes() + mid.bytes());
            ++checks.checked_frequency_offsets;
        }
        for (std::size_t n = 0; n < witness_offsets.size(); ++n) {
            const auto offset = witness_offsets[n];
            const bool needs_slab_check = spec.perturbations &&
                (offset == 0. || std::abs(offset) == Table::occupied_half_band_hz);
            if (offset == 0.) {
                witnesses[n] = frequency_nodes[rf_knots / 2];
            } else {
                base.run(offset, 4, spec.perturbations, false, checks);
                witnesses[n] = base.sample(spec.impact_y_m, true, needs_slab_check);
                ++checks.checked_frequency_offsets;
            }
            for (std::size_t i = 0; i < dense_points; ++i)
                observe(checks.frequency, envelope(offset, i, false), witnesses[n].v[i], i, offset);
            const auto& w = witnesses[n];
            for (std::size_t i = 0; i + 4 < dense_points; i += 4) {
                for (std::size_t k = 1; k < 4; ++k) {
                    const auto h = weights(k / 4.);
                    observe(checks.trajectory, blend(h, w.v[i], w.d[i] * Table::position_step_m,
                        w.v[i + 4], w.d[i + 4] * Table::position_step_m), w.v[i + k], i + k, offset);
                }
            }
            if (needs_slab_check) {
                base.run(offset, 1, true, false, checks);
                const auto one = base.sample(spec.impact_y_m, false);
                base.run(offset, 2, true, false, checks);
                const auto two = base.sample(spec.impact_y_m, false);
                compare(checks.slices_1_vs_2, one, two, offset);
                compare(checks.slices_2_vs_4, two, w, offset);
                const auto smallest = static_cast<std::size_t>(std::min_element(w.v.begin(), w.v.end(),
                    [](auto a, auto b) { return std::norm(a) < std::norm(b); }) - w.v.begin());
                std::vector<std::size_t> selected;
                selected.reserve(dense_points);
                for (std::size_t i = 0; i < dense_points; ++i)
                    if (i == smallest || std::abs(two.v[i] - w.v[i]) >= relative_tol * std::abs(w.v[i]))
                        selected.push_back(i);
                // Selection is fixed before either refined solve, including at a deep
                // minimum with a short coarse-to-production chord from cancellation.
                base.run(offset, 8, true, false, checks);
                const auto eight = base.sample(spec.impact_y_m, false, true);
                base.run(offset, 16, true, false, checks);
                const auto sixteen = base.sample(spec.impact_y_m, false, true);
                const std::array rows{&w, &eight, &sixteen};
                auto& window = checks.null_windows[n / 2];
                auto& fine = null_samples[n / 2];
                window.center_x_m = position(smallest);
                window.offset_hz = offset;
                for (std::size_t i = 0; i < null_window_points; ++i) {
                    const double x = window.center_x_m - Table::null_window_radius_m + i * Table::null_window_step_m;
                    if (std::abs(x) > Table::position_half_span_m) continue;
                    cancelled(stop);
                    auto& p = fine.points[fine.count++];
                    p.x_m = x;
                    for (std::size_t s = 0; s < rows.size(); ++s) {
                        p.field[s] = spectral_row_at(rows[s]->row_spectrum, x);
                        if (i == null_window_half_steps)
                            window.anchor_error = std::max(window.anchor_error, std::abs(p.field[s] - rows[s]->v[smallest]));
                    }
                }
                for (std::size_t i = 0, next = 0; i < dense_points; ++i) {
                    const bool was_selected = next < selected.size() && i == selected[next];
                    if (was_selected) ++next;
                    HfPhaseScreensSlabWitness q{position(i), offset, std::abs(w.v[i]), std::abs(eight.v[i]),
                        std::abs(sixteen.v[i]), std::abs(two.v[i] - w.v[i]), std::abs(w.v[i] - eight.v[i]),
                        std::abs(eight.v[i] - sixteen.v[i])};
                    q.relative_resolved = q.amplitude_16 >= null_floor;
                    if (q.relative_resolved) {
                        q.relative_allowance = (q.difference_4_8 + q.difference_8_16) / q.amplitude_16;
                        checks.deep_slab_qualified &= q.relative_allowance <= relative_tol;
                        if (was_selected)
                            checks.deep_slab_qualified &= q.difference_4_8 <= std::max(1e-6, .5 * q.difference_2_4) &&
                                q.difference_8_16 <= std::max(1e-6, .5 * q.difference_4_8);
                        if (q.relative_allowance > checks.worst_slab_relative.relative_allowance)
                            checks.worst_slab_relative = q;
                    } else {
                        ++checks.unresolved_slab_witnesses;
                    }
                    if (i == smallest) checks.slab_minima[n / 2] = q;
                    checks.deep_slab_witnesses += was_selected;
                    ++checks.slab_cut_points;
                }
                peak_workspace(base.bytes() + one.bytes() + two.bytes() + eight.bytes() + sixteen.bytes() + capacity_bytes(selected));
            }
        }
        if (spec.perturbations) {
            base.run(0., 1, true, true, checks);
            auto single = base.sample(spec.impact_y_m, false);
            for (std::size_t i = 0; i < dense_points; ++i)
                checks.single_screen_difference = std::max(checks.single_screen_difference,
                    std::abs(single.v[i] - frequency_nodes[rf_knots / 2].v[i]));
            peak_workspace(base.bytes() + single.bytes());
        }
        peak_workspace(base.bytes());
    }
    if (spec.perturbations) {
        for (const bool finer : {true, false}) {
            // Halve the cells at fixed physical extent, then independently
            // double both extents at fixed cells. Physical coordinates stay fixed.
            FieldSolver reference(2 * nx, 2 * ny, finer ? .5 * grid_step_m : grid_step_m, slabs, spec.drift_mps, stop);
            for (const auto n : {std::size_t{0}, std::size_t{2}, std::size_t{4}}) {
                reference.run(witness_offsets[n], 4, true, false, checks);
                const auto row = reference.sample(spec.impact_y_m, false);
                compare(finer ? checks.mesh : checks.box, witnesses[n], row, witness_offsets[n]);
                peak_workspace(reference.bytes() + row.bytes());
            }
        }
    }

    auto table = std::shared_ptr<Table>(new Table(spec));
    std::array<Complex, frequency_bins> rf{}, slope{}, uniform{};
    for (std::size_t j = 0; j < frequency_bins; ++j) {
        const auto offset = Table::sample_rate_hz * (static_cast<double>(j) / frequency_bins - .5);
        uniform[j] = std::polar(1., uniform_phase(offset));
    }
    const auto check_energy = [&](const HfFullwaveKernel& h) {
        checks.maximum_discarded_energy_fraction = std::max(checks.maximum_discarded_energy_fraction,
            h.discarded_pre_energy_fraction + h.discarded_tail_energy_fraction);
        checks.maximum_raw_precursor_energy_fraction = std::max(checks.maximum_raw_precursor_energy_fraction,
            h.prearrival_energy_fraction);
    };
    for (std::size_t k = 0; k <= Table::position_intervals; ++k) {
        cancelled(stop);
        for (std::size_t j = 0; j < frequency_bins; ++j) {
            const auto offset = Table::sample_rate_hz * (static_cast<double>(j) / frequency_bins - .5);
            rf[j] = envelope(offset, 4 * k, false) * uniform[j];
            slope[j] = envelope(offset, 4 * k, true) * uniform[j] * Table::position_step_m;
        }
        const auto value_fir = design_hf_fullwave_kernel(rf, fir_design);
        const auto slope_fir = design_hf_fullwave_kernel(slope, fir_design);
        check_energy(value_fir);
        std::copy(value_fir.fir.taps.begin(), value_fir.fir.taps.end(), table->knots_[k].values.begin());
        std::copy(slope_fir.fir.taps.begin(), slope_fir.fir.taps.end(), table->knots_[k].slopes.begin());
    }
    peak_workspace(table->workspace_bytes() + sizeof(HfPhaseScreensStream));

    std::array<double, Table::taps> flight{};
    for (std::size_t k = 0; k < Table::position_intervals; ++k) {
        const auto& lo = table->knots_[k];
        const auto& hi = table->knots_[k + 1];
        for (std::size_t lag = 0; lag < Table::taps; ++lag) {
            // Derivative of the cubic is a convex combination of its
            // three Bezier derivative control points over the interval.
            flight[lag] = std::max({flight[lag], std::abs(lo.slopes[lag]), std::abs(hi.slopes[lag]),
                std::abs(3. * (hi.values[lag] - lo.values[lag]) - lo.slopes[lag] - hi.slopes[lag])});
        }
    }
    for (std::size_t lag = 0; lag < flight.size(); ++lag)
        table->derivative_flight_bound_ += flight[lag] * std::abs(static_cast<double>(lag + Table::first_lag_samples()) - reference_lag);
    checks.retarded_peak_error_bound = table->retarded_peak_error_bound(spec.drift_mps);

    std::array<Complex, witness_offsets.size()> native_phasor;
    for (std::size_t f = 0; f < witness_offsets.size(); ++f) {
        const auto offset = witness_offsets[f];
        native_phasor[f] = std::polar(1., -uniform_phase(offset) -
            2 * pi * offset * Table::modelling_delay_samples / Table::sample_rate_hz);
    }
    HfFullwaveFir fir{Table::first_lag_samples(), std::vector<Complex>(Table::taps)};
    const auto populate_fir = [&](double x) {
        for (std::size_t lag = 0; lag < Table::taps; ++lag)
            fir.taps[lag] = table->coefficient_at_position(lag + Table::first_lag_samples(), x);
    };
    bool resolved_voltage_ok = true;
    for (std::size_t i = 0; i < dense_points; ++i) {
        cancelled(stop);
        const auto p = std::norm(witnesses[2].v[i]);
        checks.minimum_carrier_power = std::min(checks.minimum_carrier_power, p);
        checks.maximum_carrier_power = std::max(checks.maximum_carrier_power, p);
        if (p < null_floor * null_floor) ++checks.unresolved_trajectory_points;
        populate_fir(position(i));
        for (std::size_t f = 0; f < witness_offsets.size(); ++f) {
            const auto target = std::conj(witnesses[f].v[i]) * native_phasor[f];
            const auto v = hf_fullwave_fir_response(fir, witness_offsets[f] / Table::sample_rate_hz);
            observe(checks.fir, v, target, i, witness_offsets[f]);
            if (std::abs(target) >= null_floor)
                resolved_voltage_ok &= std::abs(v - target) <= relative_tol * std::abs(target);
        }
        ++checks.checked_positions;
    }
    if (spec.perturbations) {
        for (std::size_t n = 0; n < null_samples.size(); ++n) {
            auto& window = checks.null_windows[n];
            const auto& fine = null_samples[n];
            const double extent = std::min(Table::position_half_span_m, window.center_x_m + Table::null_window_radius_m) -
                std::max(-Table::position_half_span_m, window.center_x_m - Table::null_window_radius_m);
            checks.null_windows_qualified &= fine.count == 1 + static_cast<std::size_t>(extent / Table::null_window_step_m) &&
                window.anchor_error <= null_anchor_tol;
            for (std::size_t i = 0; i < fine.count; ++i) {
                cancelled(stop);
                const auto& sample = fine.points[i];
                const auto& v4 = sample.field[0];
                const auto& v8 = sample.field[1];
                const auto& v16 = sample.field[2];
                populate_fir(sample.x_m);
                // Remove the common native phasor and conjugation to compare
                // the actual Hermite/FIR evaluation in the raw RF coordinates.
                const auto rendered = std::conj(hf_fullwave_fir_response(fir, window.offset_hz / Table::sample_rate_hz) *
                    std::conj(native_phasor[2 * n]));
                HfPhaseScreensNullPoint p{sample.x_m, std::abs(v4), std::abs(v8), std::abs(v16), std::abs(rendered),
                    std::abs(v8 - v4), std::abs(v16 - v8), std::abs(rendered - v4), std::abs(rendered - v16)};
                p.fir_response_rf = rendered;
                p.reference_16_rf = v16;
                p.relative_resolved = p.amplitude_16 >= null_floor;
                checks.null_windows_qualified &= p.difference_fir_4 <= fir_tol;
                if (p.relative_resolved) {
                    const auto slab_allowance = p.difference_4_8 + p.difference_8_16;
                    p.relative_slab_allowance = slab_allowance / p.amplitude_16;
                    p.relative_fir_allowance = (p.difference_fir_4 + slab_allowance) / p.amplitude_16;
                    checks.null_windows_qualified &= p.relative_slab_allowance <= relative_tol &&
                        p.relative_fir_allowance <= relative_tol;
                    if (!window.worst_slab_relative.relative_resolved ||
                        p.relative_slab_allowance > window.worst_slab_relative.relative_slab_allowance)
                        window.worst_slab_relative = p;
                    if (!window.worst_fir_relative.relative_resolved ||
                        p.relative_fir_allowance > window.worst_fir_relative.relative_fir_allowance)
                        window.worst_fir_relative = p;
                } else {
                    auto& ranges = window.unresolved_ranges_m;
                    if (!ranges.empty() && ranges.back()[1] + Table::null_window_step_m == p.x_m)
                        ranges.back()[1] = p.x_m;
                    else
                        ranges.push_back({p.x_m, p.x_m});
                    ++window.unresolved_points;
                }
                if (i == 0 || p.amplitude_16 < window.minimum_reference.amplitude_16) window.minimum_reference = p;
                if (i == 0 || p.amplitude_fir < window.minimum_fir.amplitude_fir) window.minimum_fir = p;
                ++window.sampled_points;
            }
        }
        peak_workspace(table->workspace_bytes() + sizeof(HfPhaseScreensStream));
    }
    checks.qualified = checks.deep_slab_qualified && checks.null_windows_qualified &&
        checks.mesh.absolute_error <= mesh_tol && checks.box.absolute_error <= box_tol &&
        checks.slices_2_vs_4.absolute_error <= slice_tol &&
        checks.slices_2_vs_4.absolute_error <= std::max(.001, .5 * checks.slices_1_vs_2.absolute_error) &&
        checks.frequency.absolute_error <= frequency_tol && checks.trajectory.absolute_error <= trajectory_tol &&
        checks.fir.absolute_error <= fir_tol && resolved_voltage_ok &&
        checks.maximum_plane_power_error <= power_tol && checks.maximum_large_angle_fraction <= large_angle_tol &&
        checks.maximum_edge_spectral_fraction <= edge_spectrum_tol &&
        checks.maximum_discarded_energy_fraction <= retained_energy_tol && checks.retarded_peak_error_bound <= retarded_tol &&
        checks.checked_positions == dense_points && checks.checked_frequency_offsets == 12 &&
        checks.slab_cut_points == (spec.perturbations ? 3 * dense_points : 0);
    return {checks.qualified ? std::move(table) : nullptr, checks};
}

std::array<Complex, hf_phase_screens_gaussian_positions_m.size()>
hf_phase_screens_gaussian_witness(std::stop_token stop) {
    const Geometry gaussian{{{0., .015, {{{0., 0., 1600., 2400., -.00857}, {}, {}, {}}}}, {}}};
    FieldSolver solver(nx, 2 * ny, grid_step_m, gaussian, 0., stop);
    HfPhaseScreensChecks checks;
    solver.gaussian_witness(checks);
    const auto row = solver.sample(1000., false);
    std::array<Complex, hf_phase_screens_gaussian_positions_m.size()> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto x = hf_phase_screens_gaussian_positions_m[i];
        result[i] = row.v[static_cast<std::size_t>((x + Table::position_half_span_m) / witness_step_m)];
    }
    return result;
}

HfSingleGaussianThicknessWitness hf_phase_screens_gaussian_thickness_witness(std::stop_token stop) {
    using W = HfSingleGaussianThicknessWitness;
    const Geometry input{{{200000., .015, {}}, {}}};
    W result;
    {
        FieldSolver coarse(512, 256, 125., input, 0., stop);
        for (std::size_t i = 0; i < W::x_m.size(); ++i) {
            result.thin_vacuum[i] = coarse.spherical_gaussian(W::offset_hz, W::x_m[i], 1, false);
            for (unsigned j = 0; j < 4; ++j)
                result.atmospheric_slices[i][j] = coarse.spherical_gaussian(W::offset_hz, W::x_m[i], 1u << j, true);
        }
    }
    for (const bool finer : {true, false}) {
        FieldSolver ref(1024, 512, finer ? 62.5 : 125., input, 0., stop);
        const auto v = ref.spherical_gaussian(W::offset_hz, 850., 8, true);
        (finer ? result.finer_mesh_error_at_850m : result.bigger_box_error_at_850m) =
            std::abs(v - result.atmospheric_slices[2][3]);
    }
    const double wavelength = Table::light_speed_mps / (Table::carrier_hz + W::offset_hz);
    const double index = std::sqrt(1. - electron_radius_m * wavelength * wavelength * .015 * tecu / (pi * thickness_m));
    result.spherical_baseline_amplitude = Table::total_distance_m /
        (Table::total_distance_m + thickness_m * (1. / index - 1.));
    return result;
}

HfPhaseScreensStream::HfPhaseScreensStream(std::shared_ptr<const Table> table, const HfPhaseScreensSpec& spec,
                                           std::uint64_t initial)
    : table_(std::move(table)), output_sample_(initial), last_output_sample_(maximum_samples + tail_samples() - 1) {
    if (!table_ || !same_spec(table_->spec_, spec) || initial >= maximum_samples)
        throw std::invalid_argument("phase screen stream needs its admitted table and a supported output clock");
    validate_hf_phase_screens_spec(spec, initial + 1);
}

Complex HfPhaseScreensStream::render(Complex input) {
    if (output_sample_ > last_output_sample_) throw std::invalid_argument("phase screen stream exceeds bounded output clock");
    const auto [k, f] = bracket(x_at(static_cast<double>(output_sample_), table_->spec_));
    const auto w = weights(f);
    const auto& lo = table_->knots_[k];
    const auto& hi = table_->knots_[k + 1];
    history_[cursor_] = input;
    Complex result{};
    auto index = (cursor_ + history_.size() - Table::first_lag_samples()) % history_.size();
    for (std::size_t i = 0; i < Table::taps; ++i) {
        result += blend(w, lo.values[i], lo.slopes[i], hi.values[i], hi.slopes[i]) * history_[index];
        index = index == 0 ? history_.size() - 1 : index - 1;
    }
    if (++cursor_ == history_.size()) cursor_ = 0;
    ++output_sample_;
    return result;
}

Complex HfPhaseScreensStream::step(Complex input) {
    if (draining_ || output_sample_ >= maximum_samples)
        throw std::invalid_argument("phase screen source is drained or beyond its admitted horizon");
    return render(input);
}

void HfPhaseScreensStream::process(std::span<const Complex> input, std::span<Complex> output) {
    if (input.size() != output.size()) throw std::invalid_argument("phase screen input/output size mismatch");
    for (std::size_t i = 0; i < input.size(); ++i) output[i] = step(input[i]);
}

std::size_t HfPhaseScreensStream::drain(std::span<Complex> output) {
    if (!draining_) {
        draining_ = true;
        remaining_tail_ = tail_samples();
    }
    const auto n = std::min(remaining_tail_, output.size());
    for (std::size_t i = 0; i < n; ++i) {
        output[i] = render({});
        --remaining_tail_;
    }
    return n;
}

std::size_t HfPhaseScreensStream::workspace_bytes() const noexcept { return sizeof(*this); }

} // namespace appraisal

