#pragma once

#include "datapump/fast/profile.hpp"
#include <complex>
#include <cstddef>
#include <vector>

namespace datapump::fast::detail {

// Fixed public geometry for an already validated single-carrier profile. No
// received contents or source lengths participate in this schedule.
class SingleCarrierSchedule {
public:
    enum class Kind { marker, pilot, data };
    struct Position {
        Kind kind;
        std::complex<double> known_value{};
        std::size_t bit_offset=0;
        // Valid MSB-first bits in the fixed physical interval. The remaining
        // low bits of a bits_per_symbol-wide label are always zero.
        unsigned bit_count=0;
    };

    explicit SingleCarrierSchedule(const Profile& validated_profile);

    const unsigned bits_per_symbol;
    const std::size_t data_symbols;
    const std::size_t pilot_stride;
    const std::size_t pilot_count; // Known symbols after each payload group.
    const std::size_t marker_cadence;
    const std::size_t marker_span;
    const std::size_t training_count;

    std::size_t marker_symbols(std::size_t interval_ordinal) const;
    std::size_t interval_symbols(std::size_t interval_ordinal) const;
    std::size_t total_interval_symbols(std::size_t interval_count) const;
    std::complex<double> training_symbol(std::size_t position) const;
    Position position(std::size_t interval_ordinal,std::size_t position) const;
};

// Canonical Gray QAM mapper without allocating a full constellation table.
std::complex<double> square_qam_point(unsigned order,unsigned label);

double single_carrier_pulse_radius(const Profile& validated_profile);
std::size_t single_carrier_low_rate_decimation(const Profile& validated_profile);

// The transmitter's finite RRC table and piecewise-linear interpolation. Its
// slope is with respect to symbol time, away from table knots and the cutoff.
class RrcPulseTable {
public:
    RrcPulseTable(double rolloff,double support);
    double operator()(double symbol_time) const;
    double slope(double symbol_time) const;
    double radius() const {return radius_;}
    std::size_t workspace_bytes() const {return values_.capacity()*sizeof(double);}

private:
    double radius_;
    std::vector<double> values_;
};

} // namespace datapump::fast::detail
