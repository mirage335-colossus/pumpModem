#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::fast::detail {

// A reusable Hermitian normal matrix for one locally bounded symbol window.
// The stored band is the lower triangle, including the diagonal.
class HermitianBand {
public:
    using Complex = std::complex<double>;
    enum class FactorResult { complete, nonpositive_pivot, cancelled };

    void reset(std::size_t rows,std::size_t half_bandwidth);
    std::size_t rows() const noexcept { return rows_; }
    std::size_t half_bandwidth() const noexcept { return half_bandwidth_; }
    std::size_t first_column(std::size_t row) const noexcept;
    // Increasing column order, beginning at first_column(row).
    std::span<Complex> lower_row(std::size_t row);

    // Replaces the matrix by the unit-lower LDL^H factor of A + ridge I.
    // Reset the matrix after an unsuccessful factorization.
    FactorResult factor(double ridge,std::stop_token stop={});
    bool solve_in_place(std::span<Complex> rhs,std::stop_token stop={}) const;
    // Computes diag((A + ridge I)^-1) using the selected inverse of the
    // factor band. A false return leaves the output unusable.
    bool inverse_diagonal(std::span<double> diagonal,std::stop_token stop={});
    std::size_t workspace_bytes() const noexcept;

private:
    enum class State { empty, assembled, factored };
    std::size_t index(std::size_t row,std::size_t column) const noexcept;
    std::size_t past_last_row(std::size_t column) const noexcept;
    Complex selected_value(std::size_t row,std::size_t column) const noexcept;

    std::size_t rows_ = 0;
    std::size_t half_bandwidth_ = 0;
    std::vector<Complex> lower_;
    std::vector<Complex> selected_;
    State state_ = State::empty;
};

} // namespace datapump::fast::detail
