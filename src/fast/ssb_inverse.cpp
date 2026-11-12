#include "ssb_inverse.hpp"
#include "datapump/execution.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace datapump::fast::detail {

void HermitianBand::reset(std::size_t rows,std::size_t half_bandwidth) {
    if(!rows)throw std::invalid_argument("Fast SSB normal matrix requires at least one row");
    const auto width=std::min(half_bandwidth,rows-1);
    if(rows>lower_.max_size()/(width+1))
        throw std::length_error("Fast SSB normal matrix exceeds storage bounds");
    lower_.assign(rows*(width+1),Complex{});
    rows_=rows;
    half_bandwidth_=width;
    state_=State::assembled;
}

std::size_t HermitianBand::first_column(std::size_t row) const noexcept {
    return row>half_bandwidth_?row-half_bandwidth_:0;
}

std::size_t HermitianBand::past_last_row(std::size_t column) const noexcept {
    return column+std::min(half_bandwidth_,rows_-column-1)+1;
}

std::size_t HermitianBand::index(std::size_t row,std::size_t column) const noexcept {
    return row*(half_bandwidth_+1)+half_bandwidth_-(row-column);
}

std::span<HermitianBand::Complex> HermitianBand::lower_row(std::size_t row) {
    if(state_!=State::assembled)throw std::logic_error("Fast SSB normal matrix is not ready for assembly");
    if(row>=rows_)throw std::out_of_range("Fast SSB normal matrix row is outside its geometry");
    const auto first=first_column(row);
    return std::span(lower_).subspan(index(row,first),row-first+1);
}

HermitianBand::FactorResult HermitianBand::factor(double ridge,std::stop_token stop) {
    if(state_!=State::assembled)throw std::logic_error("Fast SSB normal matrix is not assembled");
    if(!std::isfinite(ridge)||ridge<0)throw std::invalid_argument("Invalid Fast SSB normal matrix ridge");
    state_=State::empty;
    for(std::size_t row=0;row<rows_;++row) {
        if(stop.stop_requested())return FactorResult::cancelled;
        const auto first=first_column(row);
        for(std::size_t col=first;col<row;++col) {
            auto entry=lower_[index(row,col)];
            for(std::size_t k=first;k<col;++k)
                entry-=lower_[index(row,k)]*lower_[index(k,k)].real()*std::conj(lower_[index(col,k)]);
            lower_[index(row,col)]=entry/lower_[index(col,col)].real();
        }
        auto diagonal=lower_[index(row,row)].real()+ridge;
        for(std::size_t k=first;k<row;++k)
            diagonal-=std::norm(lower_[index(row,k)])*lower_[index(k,k)].real();
        if(!std::isfinite(diagonal)||diagonal<=0)return FactorResult::nonpositive_pivot;
        lower_[index(row,row)]=diagonal;
        if(row%32==0)execution::checkpoint();
    }
    state_=State::factored;
    return FactorResult::complete;
}

bool HermitianBand::solve_in_place(std::span<Complex> rhs,std::stop_token stop) const {
    if(state_!=State::factored||rhs.size()!=rows_)
        throw std::logic_error("Fast SSB solve requires a matching factored matrix");
    for(std::size_t row=0;row<rows_;++row) {
        if(stop.stop_requested())return false;
        for(std::size_t col=first_column(row);col<row;++col)
            rhs[row]-=lower_[index(row,col)]*rhs[col];
        if(row%32==0)execution::checkpoint();
    }
    for(std::size_t row=rows_;row--!=0;) {
        if(stop.stop_requested())return false;
        rhs[row]/=lower_[index(row,row)].real();
        for(std::size_t next=row+1;next<past_last_row(row);++next)
            rhs[row]-=std::conj(lower_[index(next,row)])*rhs[next];
        if(row%32==0)execution::checkpoint();
    }
    return true;
}

HermitianBand::Complex HermitianBand::selected_value(std::size_t row,std::size_t column) const noexcept {
    if(row<column)return std::conj(selected_[index(column,row)]);
    return selected_[index(row,column)];
}

bool HermitianBand::inverse_diagonal(std::span<double> diagonal,std::stop_token stop) {
    if(state_!=State::factored||diagonal.size()!=rows_)
        throw std::logic_error("Fast SSB selected inverse requires a matching factored matrix");
    if(stop.stop_requested())return false;
    selected_.resize(lower_.size());
    // Descending rows solve the selected upper triangle of
    // L^H (L D L^H)^-1 = D^-1 L^-1. The lower triangle is its adjoint.
    for(std::size_t row=rows_;row--!=0;) {
        if(stop.stop_requested())return false;
        const auto end=past_last_row(row);
        for(std::size_t col=row+1;col<end;++col) {
            Complex entry{};
            for(std::size_t k=row+1;k<end;++k)
                entry-=std::conj(lower_[index(k,row)])*selected_value(k,col);
            selected_[index(col,row)]=std::conj(entry);
        }
        auto entry=1./lower_[index(row,row)].real();
        for(std::size_t k=row+1;k<end;++k)
            entry-=(std::conj(lower_[index(k,row)])*selected_[index(k,row)]).real();
        if(!std::isfinite(entry)||entry<=0)return false;
        diagonal[row]=entry;
        selected_[index(row,row)]=entry;
        if(row%32==0)execution::checkpoint();
    }
    return true;
}

std::size_t HermitianBand::workspace_bytes() const noexcept {
    return sizeof(*this)+(lower_.capacity()+selected_.capacity())*sizeof(Complex);
}

} // namespace datapump::fast::detail
