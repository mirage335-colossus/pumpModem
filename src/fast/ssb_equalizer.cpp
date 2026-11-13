#include "ssb_equalizer.hpp"
#include "datapump/fast/modem.hpp"
#include "ssb_display_points.hpp"
#include "ssb_inverse.hpp"
#include "ssb_soft_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace datapump::fast::detail {
namespace {

using Complex = std::complex<double>;
using SymbolKind = SingleCarrierSchedule::Kind;
constexpr std::size_t pre_roll_symbols = 8;
constexpr std::size_t lookahead_symbols = 20;
constexpr std::size_t maximum_interval_centers = 16384;
constexpr std::size_t maximum_interval_columns = 3072;
constexpr std::size_t feedback_context_count = 3;

struct Frame {
    std::size_t start;
    std::size_t end;
};

Frame protected_frame(Frame frame, const SingleCarrierSchedule& schedule,
                      std::uint64_t ordinal, std::size_t protected_bits) {
    if (protected_bits >= physical_interval_bits) return frame;
    for (auto symbol = frame.start; symbol < frame.end; ++symbol) {
        const auto position = schedule.position(ordinal, symbol - frame.start);
        if (position.kind == SymbolKind::data && position.bit_offset + position.bit_count > protected_bits) {
            frame.end = symbol;
            break;
        }
    }
    return frame;
}

std::optional<std::span<const Complex>> observed(SsbAcquisitionView view, SsbCenterRange range,
                                                std::size_t stride) {
    if (view.stride != stride || !stride || range.first < view.first_center_input_tick ||
        range.past_last <= range.first) return {};
    const auto begin = range.first - view.first_center_input_tick;
    const auto length = range.past_last - range.first;
    if (begin % stride || length % stride) return {};
    const auto first = begin / stride;
    const auto count = length / stride;
    if (first > view.samples.size() || count > view.samples.size() - first) return {};
    return view.samples.subspan(static_cast<std::size_t>(first), static_cast<std::size_t>(count));
}

Complex inner(std::span<const Complex> left, std::span<const Complex> right) {
    Complex result{};
    for (std::size_t i = 0; i < left.size(); ++i) result += std::conj(left[i]) * right[i];
    return result;
}

struct DataPosition {
    std::size_t column, bit_offset;
    unsigned bit_count;
};

struct PilotPosition {
    std::size_t column;
    Complex value;
};

struct WindowSymbol {
    std::optional<std::size_t> column;
    Complex known{};
    bool presence_template = false;
};

struct Window {
    std::size_t first_symbol = 0;
    std::size_t columns = 0;
    std::vector<WindowSymbol> symbols;
    std::vector<DataPosition> data;
    std::vector<PilotPosition> pilots;
};

void add(SsbChannelTerm& total, const SsbChannelTerm& term, Complex weight) {
    total.value += weight * term.value;
    total.relative += weight * term.relative;
    total.offset += weight * term.offset;
    total.slope += weight * term.slope;
}

std::array<Complex, ssb_tracker_parameters> derivatives(const SsbChannelTerm& term) {
    constexpr Complex j{0., 1.};
    return {term.value, j * term.value, term.relative, j * term.relative, term.offset, term.slope};
}

struct FilteredRow {
    std::span<const std::size_t> columns;
    std::span<const SsbChannelTerm> coefficients;
    SsbChannelTerm known;
    Complex presence_template;
};

struct RowScratch {
    std::array<SsbChannelTerm, SsbChannelModel::maximum_row_symbols> terms{}, coefficients{};
    std::array<std::size_t, SsbChannelModel::maximum_row_symbols> columns{};
};

FilteredRow filtered_row(const SsbChannelModel& model, const Window& window, std::uint64_t center,
                         RowScratch& scratch, std::optional<SsbTimingBasis> timing = {}) {
    const auto row = model.row(center, scratch.terms, timing);
    SsbChannelTerm known;
    Complex presence_template{};
    std::size_t count = 0;
    for (std::size_t i = 0; i < row.terms.size(); ++i) {
        const auto k = row.first_symbol + i - window.first_symbol;
        const auto& symbol = window.symbols[k];
        const auto& term = row.terms[i];
        if (symbol.column) {
            scratch.columns[count] = *symbol.column;
            scratch.coefficients[count++] = term;
        } else {
            add(known, term, symbol.known);
            if (symbol.presence_template) presence_template += term.value * symbol.known;
        }
    }
    return {std::span(scratch.columns).first(count), std::span(scratch.coefficients).first(count),
            known, presence_template};
}

struct Projection {
    HermitianBand factor;
    std::vector<Complex> mean;
    double observed_power = 0.;
    double pilot_power = 0.;
    double raw_pilot_power = 0.;
    Complex cross{};
    std::size_t workspace = 0;
};

std::optional<Projection> project(const SsbChannelModel& model, const Window& window,
        std::span<const Complex> input, std::uint64_t first, double ridge, std::stop_token stop) {
    Projection result;
    auto& normal = result.factor;
    if (window.columns) normal.reset(window.columns, model.maximum_support_symbols() - 1);
    result.mean.resize(window.columns);
    auto& rhs = result.mean;
    std::vector<Complex> pilot_rhs(window.columns);
    RowScratch scratch;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (i % 32 == 0 && stop.stop_requested()) return {};
        const auto center = first + i * model.stride();
        const auto row = filtered_row(model, window, center, scratch);
        const auto y = input[i] - row.known.value;
        const auto p = row.presence_template;
        const auto prediction_input = y + p;
        result.observed_power += std::norm(prediction_input);
        result.raw_pilot_power += std::norm(p);
        result.cross += std::conj(p) * prediction_input;
        for (std::size_t j = 0; j < row.columns.size(); ++j) {
            const auto column = row.columns[j];
            const auto adjoint = std::conj(row.coefficients[j].value);
            rhs[column] += adjoint * y;
            pilot_rhs[column] += adjoint * p;
            const auto lower = normal.lower_row(column);
            const auto first_column = normal.first_column(column);
            for (std::size_t k = 0; k <= j; ++k) {
                if (row.columns[k] < first_column) throw std::logic_error("Fast SSB model exceeded the inverse band");
                lower[row.columns[k] - first_column] += adjoint * row.coefficients[k].value;
            }
        }
    }
    if (!std::isfinite(result.observed_power) || !std::isfinite(result.raw_pilot_power)) return {};
    result.pilot_power = result.raw_pilot_power;
    if (!window.columns) return result;
    if (normal.factor(ridge, stop) != HermitianBand::FactorResult::complete) return {};
    const auto original_rhs = rhs;
    auto pilot_solution = pilot_rhs;
    if (!normal.solve_in_place(rhs, stop) || !normal.solve_in_place(pilot_solution, stop)) return {};
    double explained_observed = 0., explained_pilot = 0.;
    Complex explained_cross{};
    for (std::size_t i = 0; i < rhs.size(); ++i) {
        const auto y = rhs[i] + pilot_solution[i];
        explained_observed += (std::conj(original_rhs[i] + pilot_rhs[i]) * y).real();
        explained_pilot += (std::conj(pilot_rhs[i]) * pilot_solution[i]).real();
        explained_cross += std::conj(pilot_rhs[i]) * y;
    }
    result.observed_power = std::max(0., result.observed_power - explained_observed);
    result.pilot_power = std::max(0., result.pilot_power - explained_pilot);
    result.cross -= explained_cross;
    result.workspace = sizeof(scratch) + (rhs.capacity() + pilot_rhs.capacity() +
        original_rhs.capacity() + pilot_solution.capacity()) * sizeof(Complex);
    if (!std::isfinite(std::norm(result.cross))) return {};
    return result;
}

std::pair<Complex, double> extrinsic(std::span<const Complex> mean, std::span<const double> posterior,
                                    std::size_t column) {
    const auto rho = std::clamp(posterior[column], 1e-5, .995);
    const auto gain = 1. - rho;
    return {mean[column] / gain, rho / gain};
}

struct TrackerProposal {
    SsbChannelAdjustment state;
    bool accepted = false;
};

struct PilotFit {
    Complex error;
    std::array<Complex, ssb_tracker_parameters> direction;
    double weight;
    bool fit;
};

TrackerProposal propose_tracking(const SsbChannelModel& model, const Window& window,
        std::span<const Complex> mean, std::span<const double> posterior,
        const std::array<std::vector<Complex>, ssb_tracker_parameters>& sensitivity,
        double calibration_floor, SsbTimingBasis timing, std::stop_token stop) {
    const auto stride = double(model.stride());
    const SsbTrackerStep prior{.12, .15, .16, .25, .15 * stride, .08 * stride};
    const std::array<bool, ssb_tracker_parameters> active{true, true, model.relative_gain_available(),
        model.relative_gain_available(), true, true};
    TrackerProposal proposed{model.adjustment()};
    std::vector<PilotFit> pilots;
    pilots.reserve(window.pilots.size());
    std::array<std::size_t, 2> useful{};
    for (std::size_t i = 0; i < window.pilots.size(); ++i) {
        const auto& pilot = window.pilots[i];
        const auto [z, variance] = extrinsic(mean, posterior, pilot.column);
        const auto error = z - pilot.value;
        if (!(variance <= .8 && std::abs(error) <= .85)) continue;
        PilotFit p{error, {}, 1. / std::max(.04, variance + calibration_floor), i % 2 == 0};
        for (std::size_t j = 0; j < p.direction.size(); ++j)
            if (active[j]) p.direction[j] = sensitivity[j][pilot.column] * (1. + variance) * prior[j];
        ++useful[!p.fit];
        pilots.push_back(p);
    }
    if (useful[0] < 6 || useful[1] < 6) return proposed;

    HermitianBand gram;
    gram.reset(ssb_tracker_parameters, ssb_tracker_parameters - 1);
    std::array<Complex, ssb_tracker_parameters> rhs{};
    for (std::size_t i = 0; i < ssb_tracker_parameters; ++i) {
        const auto row = gram.lower_row(i);
        for (const auto& pilot : pilots) {
            if (!pilot.fit) continue;
            const auto adjoint = pilot.weight * std::conj(pilot.direction[i]);
            rhs[i] -= (adjoint * pilot.error).real();
            for (std::size_t j = 0; j <= i; ++j) row[j] += (adjoint * pilot.direction[j]).real();
        }
    }
    std::array<double, ssb_tracker_parameters> inverse{};
    if (gram.factor(1., stop) != HermitianBand::FactorResult::complete ||
        !gram.solve_in_place(rhs, stop) || !gram.inverse_diagonal(inverse, stop)) return proposed;

    const SsbTrackerStep limit{.15, .20, .15, .20, .20 * stride,
        std::min(stride, 50e-6 * timing.half_span_ticks)};
    SsbTrackerStep step{};
    for (std::size_t j = 0; j < step.size(); ++j) {
        if (inverse[j] <= .97 * .97)
            step[j] = std::clamp(.5 * rhs[j].real() * prior[j], -limit[j], limit[j]);
    }
    const auto candidate = model.adjusted(step, timing);
    const auto midpoint_offset = candidate.clock_offset + candidate.clock_slope * timing.midpoint_ticks;
    if (std::abs(model.reference_clock() + candidate.clock_slope) > .0003 ||
        std::abs(midpoint_offset) > 2. * stride || std::abs(candidate.common) < .65 ||
        std::abs(candidate.common) > 1.35 || std::abs(candidate.relative_log.real()) > .6 ||
        std::abs(candidate.relative_log.imag()) > 1.2) return proposed;

    std::array<double, 2> before{}, after{};
    for (const auto& pilot : pilots) {
        auto error = pilot.error;
        for (std::size_t j = 0; j < step.size(); ++j) error += pilot.direction[j] * (step[j] / prior[j]);
        before[!pilot.fit] += pilot.weight * std::norm(pilot.error);
        after[!pilot.fit] += pilot.weight * std::norm(error);
    }
    if (after[0] < .99 * before[0] && after[1] < .99 * before[1]) return {candidate, true};
    return proposed;
}

struct FeedbackSlice {
    std::uint64_t ordinal = 0;
    std::uint64_t first_center = 0;
    std::vector<Complex> samples;
};

} // namespace

struct SsbEqualizer::Impl {
    Profile profile;
    SingleCarrierSchedule schedule;
    SsbChannelModel model;
    std::uint64_t revision = 0;
    std::optional<std::uint64_t> last_present;
    double signal_power = 0.;
    double ridge = 0.;
    double startup_nmse = 0.;
    double payload_ridge_scale = 1.;
    double tracker_calibration = .02;
    double soft_calibration = .02;
    std::size_t observation_bound = 0;
    std::size_t intervals_per_cycle = 0;
    std::size_t protected_bits = 0;
    std::size_t frontend_radius = 0;
    std::array<std::optional<FeedbackSlice>, feedback_context_count> feedback;
    std::optional<std::uint64_t> retained_cycle;

    Impl(const Profile& p, const SsbFrontendGeometry& g, const SsbAcquisitionLock& lock, std::stop_token stop)
        : profile(p), schedule(p), model(p, g, lock, stop), intervals_per_cycle(cycle_intervals(p)),
          protected_bits(capacity_coded_bits(p)), frontend_radius(g.radius()) {
        if (!p.capacity_mode || schedule.training_count != 2048 || !intervals_per_cycle ||
            protected_bits / physical_interval_bits > intervals_per_cycle)
            throw std::invalid_argument("Fast SSB equalizer requires the fixed capacity schedule");
        for (const auto& path : model.atoms()) signal_power += std::norm(path.gain) * p.amplitude * p.amplitude;
        if (!(std::isfinite(signal_power) && signal_power > 0.) ||
            !std::isfinite(lock.diagnostics.public_holdout_nmse) || lock.diagnostics.public_holdout_nmse < 0.)
            throw std::invalid_argument("Invalid Fast SSB acquisition calibration");
        const auto centers_per_symbol = 1. / (model.symbols_per_tick() * model.stride());
        startup_nmse = lock.diagnostics.public_holdout_nmse;
        ridge = signal_power * std::max(1e-3, centers_per_symbol * startup_nmse);
        const auto bound = std::ceil((schedule.interval_symbols(0) + pre_roll_symbols + lookahead_symbols) *
            centers_per_symbol / (1. - .001)) + 2.;
        if (!std::isfinite(bound) || bound > maximum_interval_centers)
            throw std::length_error("Fast SSB interval exceeds its native center bound");
        observation_bound = static_cast<std::size_t>(bound);
    }

    Frame frame(std::uint64_t ordinal) const {
        if (ordinal >= std::numeric_limits<std::size_t>::max())
            throw std::length_error("Fast SSB interval ordinal exceeds local geometry");
        const auto first = schedule.total_interval_symbols(static_cast<std::size_t>(ordinal));
        const auto last = schedule.total_interval_symbols(static_cast<std::size_t>(ordinal) + 1);
        if (last > std::numeric_limits<std::size_t>::max() - schedule.training_count - lookahead_symbols)
            throw std::length_error("Fast SSB interval position exceeds local geometry");
        return {schedule.training_count + first, schedule.training_count + last};
    }

    SsbCenterRange range(Frame f) const {
        return {model.center_at_or_after_symbol(f.start - pre_roll_symbols),
            model.center_at_or_after_symbol(f.end + lookahead_symbols)};
    }

    std::vector<SsbCenterRange> public_groups(std::uint64_t ordinal) const {
        const auto symbols = ssb_public_group_symbols(schedule, ordinal);
        std::vector<SsbCenterRange> result;
        result.reserve(symbols.size());
        for (const auto group : symbols) {
            result.push_back({model.center_at_or_after_symbol(group.first),
                model.center_at_or_after_symbol(group.past_last)});
        }
        return result;
    }

    Window make_window(Frame f, SsbCenterRange centers, std::uint64_t ordinal, bool full_interval) const {
        Window result;
        result.first_symbol = model.covered_symbols(centers.first).first;
        auto last = model.covered_symbols(centers.past_last - model.stride()).second;
        if (full_interval) {
            result.first_symbol = std::min(f.start, result.first_symbol);
            last = std::max(f.end, last);
        }
        if (last - result.first_symbol > maximum_interval_columns)
            throw std::length_error("Fast SSB inverse exceeded its symbol window bound");
        result.symbols.reserve(last - result.first_symbol);
        if (full_interval) result.data.reserve(schedule.data_symbols);
        for (auto k = result.first_symbol; k < last; ++k) {
            WindowSymbol symbol;
            if (k < schedule.training_count) {
                symbol.known = schedule.training_symbol(k);
            } else if (k < f.start || k >= f.end) {
                symbol.column = result.columns++;
            } else {
                const auto local = k - f.start;
                const auto position = schedule.position(ordinal, local);
                if (position.kind == SymbolKind::data) {
                    symbol.column = result.columns++;
                    if (full_interval) result.data.push_back({*symbol.column, position.bit_offset, position.bit_count});
                } else if (full_interval && position.kind == SymbolKind::pilot &&
                        (k + 1 == f.end || schedule.position(ordinal, local + 1).kind != SymbolKind::pilot)) {
                    symbol.column = result.columns++;
                    result.pilots.push_back({*symbol.column, position.known_value});
                } else {
                    symbol.known = position.known_value;
                    symbol.presence_template = true;
                }
            }
            result.symbols.push_back(symbol);
        }
        return result;
    }

    std::optional<std::size_t> feedback_position(std::uint64_t ordinal) const {
        const auto full = protected_bits / physical_interval_bits;
        if (full < 4) return {};
        const auto slot = ordinal % intervals_per_cycle;
        if (slot == full - 4) return 0;
        if (slot == full - 3) return 1;
        if (slot == full - 1) return 2;
        return {};
    }

    bool compact_feedback() const noexcept {
        return intervals_per_cycle == 1 && compact_acoustic_framing(profile);
    }

    std::optional<FeedbackSlice> retain_slice(std::uint64_t ordinal, SsbAcquisitionView view,
                                              Frame f, SsbCenterRange range) const {
        const auto stride = model.stride();
        auto [begin, end] = range;
        while (begin < end && model.covered_symbols(begin).first < f.start) begin += stride;
        while (end > begin && model.covered_symbols(end - stride).second > f.end) end -= stride;
        if (const auto samples = observed(view, {begin, end}, stride))
            return FeedbackSlice{ordinal, begin, {samples->begin(), samples->end()}};
        return {};
    }

    std::optional<FeedbackSlice> retain_slice(std::uint64_t ordinal, SsbAcquisitionView view) const {
        const auto f = frame(ordinal);
        return retain_slice(ordinal, view, f, {model.center_at_or_after_symbol(f.start + pre_roll_symbols),
            model.center_at_or_after_symbol(f.end - pre_roll_symbols)});
    }

    std::array<std::optional<FeedbackSlice>, feedback_context_count> retain_compact_slices(
            std::uint64_t ordinal, SsbAcquisitionView view) const {
        const auto f = protected_frame(frame(ordinal), schedule, ordinal, protected_bits);
        const auto stride = model.stride();
        const auto aperture = (2 * frontend_radius + stride - 1) / stride * stride;
        const auto span = f.end - f.start;
        std::array<std::optional<FeedbackSlice>, feedback_context_count> slices;
        for (std::size_t i = 0; i < slices.size(); ++i) {
            const auto begin = model.center_at_or_after_symbol(f.start + span * i / slices.size());
            auto end = model.center_at_or_after_symbol(f.start + span * (i + 1) / slices.size());
            if (end <= begin || end - begin <= aperture) return {};
            // The later holdout's centered FIR must share no raw input
            // samples with the preceding fit windows.
            if (i + 1 < slices.size()) end -= aperture;
            slices[i] = retain_slice(ordinal, view, f, {begin, end});
            if (!slices[i]) return {};
        }
        return slices;
    }
};

SsbEqualizer::SsbEqualizer(const Profile& p, const SsbFrontendGeometry& g, const SsbAcquisitionLock& lock,
                           std::stop_token stop) : impl_(std::make_unique<Impl>(p, g, lock, stop)) {}
SsbEqualizer::~SsbEqualizer() = default;
SsbEqualizer::SsbEqualizer(SsbEqualizer&&) noexcept = default;
SsbEqualizer& SsbEqualizer::operator=(SsbEqualizer&&) noexcept = default;

SsbCenterRange SsbEqualizer::required_centers(std::uint64_t ordinal) const {
    return impl_->range(impl_->frame(ordinal));
}

std::vector<SsbCenterRange> SsbEqualizer::quiet_groups(std::uint64_t ordinal) const {
    const auto groups = impl_->public_groups(ordinal);
    std::vector<SsbCenterRange> result;
    for (const auto unit : ssb_quiet_units(groups.size()))
        result.push_back({groups[unit.first].first, groups[unit.end - 1].past_last});
    return result;
}

std::size_t SsbEqualizer::maximum_observation_centers() const noexcept { return impl_->observation_bound; }
double SsbEqualizer::reference_power() const noexcept { return impl_->signal_power; }

std::size_t SsbEqualizer::workspace_bytes() const noexcept {
    auto bytes = sizeof(Impl) + impl_->model.workspace_bytes();
    for (const auto& slice : impl_->feedback) if (slice) bytes += slice->samples.capacity() * sizeof(Complex);
    return bytes;
}

std::optional<SsbIntervalEvidence> SsbEqualizer::estimate(SsbAcquisitionView view, std::uint64_t ordinal,
        std::stop_token stop, std::optional<double> noise_power, SsbDisplayPoints* display) const {
    const auto& impl = *impl_;
    if (stop.stop_requested()) return {};
    if (noise_power && (!std::isfinite(*noise_power) || *noise_power < 0.))
        throw std::invalid_argument("Invalid Fast SSB presence noise estimate");
    const auto f = impl.frame(ordinal);
    const auto range = impl.range(f);
    const auto stride = impl.model.stride();
    const auto input = observed(view, range, stride);
    if (!input || input->size() > impl.observation_bound) return {};
    const auto window = impl.make_window(f, range, ordinal, true);
    const auto payload_ridge = impl.ridge * impl.payload_ridge_scale;
    auto projection = project(impl.model, window, *input, range.first, payload_ridge, stop);
    if (!projection) return {};
    auto& normal = projection->factor;
    const auto& rhs = projection->mean;
    const auto y_power = projection->observed_power;
    const auto p_power = projection->pilot_power;
    const auto cross = projection->cross;
    SsbIntervalEvidence result;
    result.ordinal = ordinal;
    result.revision_ = impl.revision;
    result.workspace_bytes = projection->workspace + normal.workspace_bytes() +
        window.symbols.capacity() * sizeof(WindowSymbol) + window.data.capacity() * sizeof(DataPosition) +
        window.pilots.capacity() * sizeof(PilotPosition);
    if (!(y_power > 0. && p_power > 0.)) return result;
    result.presence.coherence = std::clamp(std::norm(cross) / (y_power * p_power), 0., 1.);
    result.presence.amplitude = std::abs(cross) / p_power;
    result.presence.projected_power = y_power / input->size();
    const auto floor = std::max(impl.signal_power * 1e-8, noise_power.value_or(0.));
    result.presence.present = result.presence.coherence >= .40 && result.presence.amplitude >= .15 &&
        result.presence.amplitude <= 3. && result.presence.projected_power > 8. * floor;
    if (!result.presence.present) return result;

    std::vector<double> posterior(window.columns);
    if (!normal.inverse_diagonal(posterior, stop)) return {};
    for (auto& variance : posterior) variance *= payload_ridge;
    std::array<double, 22> metrics{};
    for (const auto& d : window.data) {
        const auto [symbol, variance] = extrinsic(rhs, posterior, d.column);
        if (!(variance < 10.) || !std::isfinite(std::norm(symbol))) continue;
        if (display) display->record(d.bit_offset / impl.schedule.bits_per_symbol, symbol);
        square_qam_soft_demodulate(impl.profile.constellation, symbol,
            std::span(metrics).first(impl.schedule.bits_per_symbol));
        for (unsigned bit = 0; bit < d.bit_count; ++bit)
            result.soft_bits[d.bit_offset + bit] = static_cast<float>(
                std::clamp(metrics[bit] / (variance + impl.soft_calibration), -24., 24.));
    }
    for (const auto& p : window.pilots) {
        const auto [symbol, variance] = extrinsic(rhs, posterior, p.column);
        result.pilot_error_power += std::norm(symbol - p.value);
        result.expected_pilot_variance += variance;
    }
    if (!window.pilots.empty()) {
        result.pilot_error_power /= window.pilots.size();
        result.expected_pilot_variance /= window.pilots.size();
        const auto residual = result.pilot_error_power - result.expected_pilot_variance;
        result.proposed_tracker_calibration_ = std::clamp(residual, ssb_legacy_calibration_guard, .35);
        result.proposed_soft_calibration_ = std::clamp(residual,
            ssb_demapper_variance_guard(impl.profile.constellation), .35);
    } else {
        result.proposed_tracker_calibration_ = impl.tracker_calibration;
        result.proposed_soft_calibration_ = impl.soft_calibration;
    }
    if (!std::isfinite(result.proposed_tracker_calibration_) ||
        !std::isfinite(result.proposed_soft_calibration_)) return {};

    if (window.pilots.size() >= 12) {
        const auto first = impl.model.relative_ticks(range.first);
        const auto past = impl.model.relative_ticks(range.past_last);
        const SsbTimingBasis timing{.5 * (first + past), .5 * (past - first)};
        RowScratch scratch;
        std::array<std::vector<Complex>, ssb_tracker_parameters> sensitivity;
        for (auto& direction : sensitivity) direction.resize(window.columns);
        for (std::size_t i = 0; i < input->size(); ++i) {
            if (i % 32 == 0 && stop.stop_requested()) return {};
            const auto row = filtered_row(impl.model, window, range.first + i * stride, scratch, timing);
            auto predicted = row.known;
            for (std::size_t j = 0; j < row.columns.size(); ++j)
                add(predicted, row.coefficients[j], rhs[row.columns[j]]);
            const auto residual = (*input)[i] - predicted.value;
            const auto predicted_derivatives = derivatives(predicted);
            for (std::size_t j = 0; j < row.columns.size(); ++j) {
                const auto term = derivatives(row.coefficients[j]);
                const auto adjoint = std::conj(term[0]);
                for (std::size_t k = 0; k < term.size(); ++k)
                    sensitivity[k][row.columns[j]] += std::conj(term[k]) * residual - adjoint * predicted_derivatives[k];
            }
        }
        for (auto& direction : sensitivity) if (!normal.solve_in_place(direction, stop)) return {};
        const auto proposal = propose_tracking(impl.model, window, rhs, posterior, sensitivity,
            impl.tracker_calibration, timing, stop);
        result.tracker_proposed = proposal.accepted;
        result.proposed_ = proposal.state;
        result.workspace_bytes += ssb_tracker_parameters * window.columns * sizeof(Complex);
    }
    result.workspace_bytes += normal.workspace_bytes() + posterior.capacity() * sizeof(double);
    return stop.stop_requested() ? std::nullopt : std::optional(std::move(result));
}

std::optional<SsbPublicPresence> SsbEqualizer::score_group(SsbAcquisitionView view, std::uint64_t ordinal,
        std::size_t index, std::stop_token stop, std::optional<double> noise_power,
        SsbQuietUnitDiagnostics* diagnostics) const {
    if (diagnostics) *diagnostics = {};
    if (stop.stop_requested()) return {};
    if (noise_power && (!std::isfinite(*noise_power) || *noise_power < 0.))
        throw std::invalid_argument("Invalid Fast SSB group noise estimate");
    const auto& impl = *impl_;
    const auto groups = impl.public_groups(ordinal);
    const auto units = ssb_quiet_units(groups.size());
    if (index >= units.size()) throw std::out_of_range("Fast SSB group outside its public schedule");
    const auto unit = units[index];
    const SsbCenterRange range{groups[unit.first].first, groups[unit.end - 1].past_last};
    const auto stride = impl.model.stride();
    if (range.first < view.first_center_input_tick || stride != view.stride ||
        (range.first - view.first_center_input_tick) % stride) return SsbPublicPresence::unscored;
    const auto input = observed(view, range, stride);
    if (!input) return {};
    std::array<SsbQuietGroupProjection, ssb_quiet_maximum_groups> projections{};
    const auto frame = impl.frame(ordinal);
    for (auto i = unit.first; i < unit.end; ++i) {
        const auto group = groups[i];
        const auto samples = input->subspan((group.first - range.first) / stride,
            (group.past_last - group.first) / stride);
        const auto window = impl.make_window(frame, group, ordinal, false);
        const auto projection = project(impl.model, window, samples, group.first, impl.ridge, stop);
        if (stop.stop_requested()) return {};
        if (!projection) return SsbPublicPresence::unscored;
        projections[i - unit.first] = {samples.size(), projection->raw_pilot_power,
            projection->pilot_power, projection->observed_power, projection->cross};
    }
    const auto exact_zero = std::ranges::all_of(*input, [](Complex sample) { return sample == Complex{}; });
    return classify_ssb_quiet_unit(std::span(projections).first(unit.end - unit.first),
        exact_zero, noise_power, diagnostics);
}

bool SsbEqualizer::commit_present(const SsbIntervalEvidence& estimate, SsbAcquisitionView view,
                                  std::stop_token stop) {
    auto& impl = *impl_;
    if (stop.stop_requested() || !estimate.presence.present || estimate.revision_ != impl.revision ||
        (impl.last_present && estimate.ordinal <= *impl.last_present) ||
        !observed(view, required_centers(estimate.ordinal), impl.model.stride())) return false;
    const auto cycle = estimate.ordinal / impl.intervals_per_cycle;
    const auto index = impl.feedback_position(estimate.ordinal);
    auto slice = index ? impl.retain_slice(estimate.ordinal, view) : std::nullopt;
    const auto compact = impl.compact_feedback();
    auto compact_slices = compact ? impl.retain_compact_slices(estimate.ordinal, view) : decltype(impl.feedback){};
    if (estimate.tracker_proposed && !impl.model.commit_adjustment(estimate.proposed_, stop)) return false;
    if (impl.retained_cycle != cycle) {
        impl.feedback = {};
        impl.retained_cycle = cycle;
    }
    if (index) impl.feedback[*index] = std::move(slice);
    if (compact) impl.feedback = std::move(compact_slices);
    impl.tracker_calibration = estimate.proposed_tracker_calibration_;
    impl.soft_calibration = estimate.proposed_soft_calibration_;
    impl.last_present = estimate.ordinal;
    ++impl.revision;
    return true;
}

bool SsbEqualizer::wants_cycle_feedback(std::uint64_t ordinal) const noexcept {
    const auto& impl = *impl_;
    return ordinal % impl.intervals_per_cycle == impl.intervals_per_cycle - 1 &&
        impl.retained_cycle == ordinal / impl.intervals_per_cycle &&
        std::ranges::all_of(impl.feedback, [](const auto& slice) { return slice.has_value(); });
}

namespace {

struct VerifiedWindow {
    Frame frame;
    const FeedbackSlice* slice;
    std::vector<Complex> symbols;
};

struct VerifiedRow {
    const VerifiedWindow* window;
    std::uint64_t center;
    Complex sample;
};

std::optional<VerifiedWindow> verified_window(const Profile& profile, const SingleCarrierSchedule& schedule,
        Frame frame, const FeedbackSlice& slice, std::size_t cycle_intervals, std::span<const std::uint8_t> bits) {
    const auto slot = slice.ordinal % cycle_intervals;
    const auto offset = slot * physical_interval_bits;
    if (offset >= bits.size()) return {};
    const auto slot_bits = bits.subspan(offset, std::min(physical_interval_bits, bits.size() - offset));
    frame = protected_frame(frame, schedule, slice.ordinal, slot_bits.size());
    VerifiedWindow result{frame, &slice, {}};
    result.symbols.reserve(frame.end - frame.start);
    for (std::size_t k = 0; k < frame.end - frame.start; ++k) {
        const auto position = schedule.position(slice.ordinal, k);
        if (position.kind != SymbolKind::data) {
            result.symbols.push_back(position.known_value);
            continue;
        }
        unsigned label = 0;
        for (const auto bit : slot_bits.subspan(position.bit_offset, position.bit_count)) {
            if (bit > 1) return {};
            label = (label << 1) | bit;
        }
        // Only canonical zero suffix within a protected symbol is known.
        // Physical interval alignment fill is excluded from the window.
        label <<= schedule.bits_per_symbol - position.bit_count;
        result.symbols.push_back(square_qam_point(profile.constellation, label));
    }
    return result;
}

std::vector<VerifiedRow> verified_rows(const SsbChannelModel& model, std::span<const VerifiedWindow> windows) {
    std::vector<VerifiedRow> result;
    for (const auto& w : windows) {
        const auto& slice = *w.slice;
        for (std::size_t i = 0; i < slice.samples.size(); ++i) {
            const auto center = slice.first_center + i * model.stride();
            const auto [first, end] = model.covered_symbols(center);
            if (first >= w.frame.start && end <= w.frame.end && end > first)
                result.push_back({&w, center, slice.samples[i]});
        }
    }
    return result;
}

struct KnownDesign {
    std::vector<Complex> samples;
    std::vector<std::vector<Complex>> paths;
    std::vector<std::vector<Complex>> rates;

    std::size_t workspace_bytes() const {
        auto size = samples.capacity() * sizeof(Complex);
        for (const auto* columns : {&paths, &rates})
            for (const auto& c : *columns) size += c.capacity() * sizeof(Complex);
        return size;
    }
};

std::optional<KnownDesign> known_design(const SsbChannelModel& model, std::span<const VerifiedRow> rows,
        std::span<const SsbAcquisitionAtom> atoms, std::span<const SsbPulseCache> caches, double time,
        bool with_rates, std::stop_token stop) {
    KnownDesign design;
    design.samples.reserve(rows.size());
    for (const auto& row : rows) design.samples.push_back(row.sample);
    design.paths.resize(atoms.size());
    if (with_rates) design.rates.resize(atoms.size());
    for (std::size_t a = 0; a < atoms.size(); ++a) {
        design.paths[a].reserve(rows.size());
        if (with_rates) design.rates[a].reserve(rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (i % 32 == 0 && stop.stop_requested()) return {};
            const auto& row = rows[i];
            const auto coefficient = model.known_path(row.center, row.window->frame.start,
                row.window->symbols, atoms[a], caches[a], time);
            if (!coefficient) return {};
            design.paths[a].push_back(coefficient->value);
            if (with_rates) design.rates[a].push_back(coefficient->frequency_derivative);
        }
    }
    return design;
}

struct KnownFit {
    HermitianBand factor;
    std::vector<Complex> gains;
    std::vector<Complex> residual;
    double residual_energy;
};

std::optional<KnownFit> fit_known(const KnownDesign& design, std::stop_token stop) {
    const auto n = design.paths.size();
    KnownFit fit{};
    fit.factor.reset(n, n - 1);
    fit.gains.resize(n);
    double scale = 0.;
    for (std::size_t i = 0; i < n; ++i) {
        if (stop.stop_requested()) return {};
        const auto row = fit.factor.lower_row(i);
        for (std::size_t j = 0; j <= i; ++j) row[j] = inner(design.paths[i], design.paths[j]);
        scale = std::max(scale, row[i].real());
        fit.gains[i] = inner(design.paths[i], design.samples);
    }
    if (fit.factor.factor(scale * 1e-7, stop) != HermitianBand::FactorResult::complete ||
        !fit.factor.solve_in_place(fit.gains, stop)) return {};
    fit.residual = design.samples;
    for (std::size_t a = 0; a < n; ++a)
        for (std::size_t i = 0; i < fit.residual.size(); ++i) fit.residual[i] -= fit.gains[a] * design.paths[a][i];
    fit.residual_energy = inner(fit.residual, fit.residual).real();
    if (!std::isfinite(fit.residual_energy)) return {};
    return fit;
}

struct KnownScore {
    double nmse = std::numeric_limits<double>::infinity();
    double coherence = 0.;
};

KnownScore score_known(const KnownDesign& design, std::span<const Complex> gains) {
    double residual = 0., received = 0., predicted = 0.;
    Complex correlation{};
    for (std::size_t i = 0; i < design.samples.size(); ++i) {
        Complex value{};
        for (std::size_t a = 0; a < gains.size(); ++a) value += gains[a] * design.paths[a][i];
        residual += std::norm(design.samples[i] - value);
        received += std::norm(design.samples[i]);
        predicted += std::norm(value);
        correlation += std::conj(value) * design.samples[i];
    }
    if (!(received > 0. && predicted > 0.)) return {};
    return {residual / received, correlation.real() / std::sqrt(received * predicted)};
}

std::vector<std::size_t> rate_groups(std::span<const SsbAcquisitionAtom> atoms) {
    std::vector<std::size_t> groups(atoms.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        const auto same = std::find_if(atoms.begin(), atoms.begin() + i, [&](const auto& atom) {
            return std::abs(atom.frequency_hz - atoms[i].frequency_hz) <= 1e-6;
        });
        groups[i] = same == atoms.begin() + i ? count++ : groups[same - atoms.begin()];
    }
    return groups;
}

std::optional<std::vector<double>> rate_step(const KnownDesign& design, const KnownFit& fit,
        std::span<const std::size_t> groups, double centers_per_symbol, std::size_t& workspace,
        std::stop_token stop) {
    const auto n = *std::max_element(groups.begin(), groups.end()) + 1;
    const auto rows = design.samples.size();
    const auto atoms = design.paths.size();
    if (rows <= atoms) return {};
    std::vector<std::vector<Complex>> derivatives(n, std::vector<Complex>(rows));
    workspace = std::max(workspace, n * rows * sizeof(Complex));
    for (std::size_t a = 0; a < atoms; ++a)
        for (std::size_t t = 0; t < rows; ++t)
            derivatives[groups[a]][t] += fit.gains[a] * design.rates[a][t];
    std::vector<Complex> projected(atoms);
    // Profile complex gains out of each real line-rate direction.
    for (auto& direction : derivatives) {
        if (stop.stop_requested()) return {};
        for (std::size_t a = 0; a < atoms; ++a) projected[a] = inner(design.paths[a], direction);
        if (!fit.factor.solve_in_place(projected, stop)) return {};
        for (std::size_t a = 0; a < atoms; ++a)
            for (std::size_t t = 0; t < rows; ++t) direction[t] -= projected[a] * design.paths[a][t];
    }
    HermitianBand gram;
    gram.reset(n, n - 1);
    std::vector<Complex> rhs(n);
    std::vector<double> inverse(n);
    double scale = 0.;
    for (std::size_t i = 0; i < n; ++i) {
        const auto row = gram.lower_row(i);
        for (std::size_t j = 0; j <= i; ++j) row[j] = inner(derivatives[i], derivatives[j]).real();
        scale = std::max(scale, row[i].real());
        rhs[i] = inner(derivatives[i], fit.residual).real();
    }
    const auto noise = centers_per_symbol * fit.residual_energy / (rows - atoms);
    const auto ridge = std::max(noise / (.15 * .15), scale * 1e-8);
    if (gram.factor(ridge, stop) != HermitianBand::FactorResult::complete ||
        !gram.solve_in_place(rhs, stop) || !gram.inverse_diagonal(inverse, stop)) return {};
    std::vector<double> step(n);
    for (std::size_t j = 0; j < n; ++j)
        if (noise * inverse[j] <= .10 * .10) step[j] = std::clamp(rhs[j].real(), -.12, .12);
    return step;
}

std::vector<SsbPulseCache> path_caches(const SsbChannelModel& model,
        std::span<const SsbAcquisitionAtom> atoms, std::stop_token stop) {
    std::vector<SsbPulseCache> caches;
    caches.reserve(atoms.size());
    for (const auto& atom : atoms) caches.push_back(model.make_path_cache(atom.frequency_hz, stop));
    return caches;
}

std::size_t cache_storage(std::span<const SsbPulseCache> caches) {
    std::size_t bytes = 0;
    for (const auto& c : caches) bytes += c.workspace_bytes();
    return bytes;
}

} // namespace

SsbCycleRefinement SsbEqualizer::finish_coding_cycle(std::uint64_t wire_ordinal,
        const std::optional<ValidatedCapacityCycle>& cycle, std::stop_token stop) {
    auto& impl = *impl_;
    SsbCycleRefinement receipt;
    if (impl.retained_cycle != wire_ordinal) return receipt;
    auto slices = std::exchange(impl.feedback, {});
    impl.retained_cycle.reset();
    if (stop.stop_requested() || !cycle || cycle->wire_ordinal != wire_ordinal ||
        cycle->protected_bits.size() != impl.protected_bits ||
        !std::ranges::all_of(slices, [](const auto& slice) { return slice.has_value(); })) return receipt;
    std::array<VerifiedWindow, feedback_context_count> windows;
    for (std::size_t i = 0; i < slices.size(); ++i) {
        const auto& slice = *slices[i];
        if (slice.ordinal / impl.intervals_per_cycle != wire_ordinal ||
            (!impl.compact_feedback() && impl.feedback_position(slice.ordinal) != i))
            return receipt;
        auto window = verified_window(impl.profile, impl.schedule, impl.frame(slice.ordinal),
            slice, impl.intervals_per_cycle, cycle->protected_bits);
        if (!window) return receipt;
        windows[i] = std::move(*window);
    }
    const auto rows = verified_rows(impl.model, std::span(windows).first(2));
    const auto held_rows = verified_rows(impl.model, std::span(windows).last(1));
    const auto atom_count = impl.model.atoms().size();
    if (rows.size() <= 2 * atom_count || held_rows.size() <= atom_count) return receipt;
    receipt.attempted = true;
    const auto time = .5 * (impl.model.relative_ticks(rows.front().center) + impl.model.relative_ticks(rows.back().center));
    const auto original = impl.model.atoms();
    try {
        auto control_caches = path_caches(impl.model, original, stop);
        auto holdout = known_design(impl.model, held_rows, original, control_caches, time, false, stop);
        auto design = known_design(impl.model, rows, original, control_caches, time, true, stop);
        if (!holdout || !design) return receipt;
        auto fitted = fit_known(*design, stop);
        if (!fitted) return receipt;

        std::vector<Complex> control;
        control.reserve(atom_count);
        for (const auto& atom : original) control.push_back(impl.model.effective_gain(atom, time));
        const auto baseline = score_known(*holdout, control);
        const auto gain_score = score_known(*holdout, fitted->gains);
        auto gain_control = fitted->gains;
        receipt.baseline_holdout_nmse = baseline.nmse;
        // A validated compact acoustic cycle gives an independent estimate
        // of how well the startup channel model predicts continuing audio.
        // Reserve more inverse regularization when that error rises so deep
        // fades cannot yield overconfident payload bits. Quiet scoring keeps
        // its original ridge and the following interval keeps its geometry.
        if (impl.profile.channel == Channel::acoustic_short && impl.compact_feedback() &&
            baseline.coherence > .9 && std::isfinite(baseline.nmse)) {
            impl.payload_ridge_scale = std::clamp(baseline.nmse / std::max(.001, impl.startup_nmse), 1., 2.);
        }
        const auto groups = rate_groups(original);
        auto atoms = std::vector<SsbAcquisitionAtom>(original.begin(), original.end());
        std::vector<SsbPulseCache> trial_caches;
        std::size_t rate_workspace = 0;
        std::size_t trial_workspace = 0;
        for (unsigned iteration = 0; iteration < 2 && !stop.stop_requested(); ++iteration) {
            const auto step = rate_step(*design, *fitted, groups,
                1. / (impl.model.symbols_per_tick() * impl.model.stride()), rate_workspace, stop);
            if (!step) break;
            auto candidate = atoms;
            double largest = 0.;
            for (std::size_t i = 0; i < atom_count; ++i) {
                const auto rate = std::clamp(atoms[i].frequency_hz + (*step)[groups[i]],
                    original[i].frequency_hz - .20, original[i].frequency_hz + .20);
                candidate[i].frequency_hz = std::clamp(rate, -500., 500.);
                largest = std::max(largest, std::abs(candidate[i].frequency_hz - atoms[i].frequency_hz));
            }
            if (largest < .001) break;
            auto caches = path_caches(impl.model, candidate, stop);
            auto trial = known_design(impl.model, rows, candidate, caches, time, true, stop);
            if (!trial) break;
            auto trial_fit = fit_known(*trial, stop);
            trial_workspace = std::max(trial_workspace, trial->workspace_bytes() + cache_storage(caches));
            if (!trial_fit || !(trial_fit->residual_energy < fitted->residual_energy)) break;
            atoms = std::move(candidate);
            trial_caches = std::move(caches);
            design = std::move(trial);
            fitted = std::move(trial_fit);
        }
        auto rate_score = KnownScore{};
        if (!trial_caches.empty()) {
            if (const auto trial_holdout = known_design(impl.model, held_rows, atoms, trial_caches, time, false, stop))
                rate_score = score_known(*trial_holdout, fitted->gains);
        }
        receipt.workspace_bytes = design->workspace_bytes() + holdout->workspace_bytes() + cache_storage(control_caches) +
            cache_storage(trial_caches) + rate_workspace + trial_workspace +
            (rows.capacity() + held_rows.capacity()) * sizeof(VerifiedRow);
        const auto acceptable = [&](KnownScore score) {
            return score.nmse <= .08 && score.nmse <= .80 * baseline.nmse && score.coherence >= .95;
        };
        const auto accept_rates = acceptable(rate_score) && rate_score.nmse <= 1.02 * gain_score.nmse;
        const auto chosen = accept_rates ? rate_score : gain_score;
        receipt.selected_holdout_nmse = chosen.nmse;
        receipt.signed_coherence = chosen.coherence;
        if (!acceptable(chosen) || stop.stop_requested()) return receipt;
        if (!accept_rates) atoms.assign(original.begin(), original.end());
        const auto& gains = accept_rates ? fitted->gains : gain_control;
        constexpr auto tau = 2. * std::numbers::pi;
        for (std::size_t i = 0; i < atom_count; ++i) {
            const auto cycles = std::remainder(-atoms[i].frequency_hz * time / impl.model.input_rate(), 1.);
            atoms[i].gain = gains[i] * std::polar(1., tau * cycles);
        }
        receipt.accepted = impl.model.replace_paths(std::move(atoms),
            accept_rates ? std::move(trial_caches) : std::move(control_caches), stop);
        receipt.updated_rates = receipt.accepted && accept_rates;
        if (receipt.accepted) ++impl.revision;
        return receipt;
    } catch (...) {
        if (stop.stop_requested()) return receipt;
        throw;
    }
}

} // namespace datapump::fast::detail
