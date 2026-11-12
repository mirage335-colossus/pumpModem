#include "fast/ssb_inverse.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using datapump::fast::detail::HermitianBand;

namespace {
using Complex=HermitianBand::Complex;
using Matrix=std::vector<std::vector<Complex>>;
using FactorResult=HermitianBand::FactorResult;

void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

std::vector<Complex> dense_solve(Matrix matrix,std::vector<Complex> rhs) {
    const auto size=matrix.size();
    for(std::size_t col=0;col<size;++col) {
        std::size_t pivot=col;
        for(std::size_t row=col+1;row<size;++row)
            if(std::abs(matrix[row][col])>std::abs(matrix[pivot][col]))pivot=row;
        std::swap(matrix[pivot],matrix[col]);
        std::swap(rhs[pivot],rhs[col]);
        for(std::size_t row=col+1;row<size;++row) {
            const auto scale=matrix[row][col]/matrix[col][col];
            for(std::size_t k=col;k<size;++k)matrix[row][k]-=scale*matrix[col][k];
            rhs[row]-=scale*rhs[col];
        }
    }
    for(std::size_t row=size;row--!=0;) {
        for(std::size_t col=row+1;col<size;++col)rhs[row]-=matrix[row][col]*rhs[col];
        rhs[row]/=matrix[row][row];
    }
    return rhs;
}

void check_matrix(HermitianBand& band,std::size_t size,std::size_t width,double ridge) {
    std::mt19937 random(712+static_cast<unsigned>(size+width));
    std::uniform_real_distribution<double> number(-.25,.25);
    Matrix basis(size,std::vector<Complex>(size));
    for(std::size_t row=0;row<size;++row) {
        basis[row][row]=.75+2*std::abs(number(random));
        const auto first=row>width?row-width:0;
        for(std::size_t col=first;col<row;++col)
            basis[row][col]={number(random),number(random)};
    }

    Matrix matrix(size,std::vector<Complex>(size));
    band.reset(size,width);
    check(band.rows()==size&&band.half_bandwidth()==std::min(width,size-1),"Band geometry mismatch");
    for(std::size_t row=0;row<size;++row) {
        auto lower=band.lower_row(row);
        const auto first=band.first_column(row);
        check(lower.size()==row-first+1,"Lower band view geometry mismatch");
        for(std::size_t col=0;col<size;++col)
            for(std::size_t k=0;k<size;++k)
                matrix[row][col]+=basis[row][k]*std::conj(basis[col][k]);
        std::copy_n(matrix[row].begin()+static_cast<std::ptrdiff_t>(first),lower.size(),lower.begin());
        matrix[row][row]+=ridge;
    }

    check(band.factor(ridge)==FactorResult::complete,"Valid Hermitian band did not factor");
    std::vector<Complex> rhs(size);
    for(auto& item:rhs)item={number(random),number(random)};
    const auto expected=dense_solve(matrix,rhs);
    check(band.solve_in_place(rhs),"Valid complex right hand side did not solve");
    for(std::size_t i=0;i<size;++i)
        check(std::abs(rhs[i]-expected[i])<1e-11,"Complex band solve differs from dense solve");

    std::vector<double> diagonal(size);
    check(band.inverse_diagonal(diagonal),"Selected inverse diagonal failed");
    for(std::size_t i=0;i<size;++i) {
        std::vector<Complex> unit(size);
        unit[i]=1.;
        const auto inverse_column=dense_solve(matrix,std::move(unit));
        check(std::abs(diagonal[i]-inverse_column[i])<1e-11,"Selected inverse diagonal differs from dense solve");
    }
    const auto bytes=band.workspace_bytes();
    check(bytes>=2*size*(band.half_bandwidth()+1)*sizeof(Complex),"Workspace omits a retained band");
    check(band.inverse_diagonal(diagonal)&&band.workspace_bytes()==bytes,"Repeated selected inverse grew workspace");

    std::stop_source stop;
    stop.request_stop();
    check(!band.solve_in_place(rhs,stop.get_token())&&!band.inverse_diagonal(diagonal,stop.get_token()),
          "Stopped solve or selected inverse continued");
}

void check_recovery(HermitianBand& band) {
    band.reset(2,1);
    band.lower_row(0)[0]=1.;
    auto second=band.lower_row(1);
    second[0]=2.;
    second[1]=1.;
    check(band.factor(0.)==FactorResult::nonpositive_pivot,"Indefinite matrix was admitted");

    band.reset(1,1);
    band.lower_row(0)[0]=1.;
    std::stop_source stop;
    stop.request_stop();
    check(band.factor(0.,stop.get_token())==FactorResult::cancelled,"Stopped factorization continued");
    band.reset(1,1);
    band.lower_row(0)[0]=1.;
    check(band.factor(0.)==FactorResult::complete,"Reset after cancellation did not recover");

    try {
        constexpr auto limit=std::numeric_limits<std::size_t>::max();
        band.reset(limit,limit);
    }catch(const std::length_error&) {
        return;
    }
    throw std::runtime_error("Overflowing matrix geometry was admitted");
}
}

int main() {
    try {
        HermitianBand band;
        check_matrix(band,13,3,.014);
        check_matrix(band,9,8,.001);
        check_matrix(band,6,1,0.);
        check_matrix(band,5,0,.05);
        check_matrix(band,1,12,0.);
        check_recovery(band);
        std::cout<<"Fast SSB complex band solves and selected inverse match dense references\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
