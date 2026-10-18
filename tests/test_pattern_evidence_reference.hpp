#pragma once
#include <algorithm>
#include <cmath>
#include <complex>

// Frozen pre-change scalar formulas for independent regression checks.
namespace datapump::test::pattern_evidence_reference {
using Complex=std::complex<double>;
inline double legacy_explained(Complex dot,double template_energy,double condition,bool exact_real,Complex template_square) {
    if(template_energy<=1e-30)return 0;
    auto fitted=std::norm(dot)/(template_energy*condition);
    if(exact_real) {
        // Individual real samples fit two real carrier bases. Their exact
        // Gram matrix is encoded by sum(|template|^2) and sum(template^2).
        // The trace-only bound loses half the energy even for an exact fit.
        const auto determinant=template_energy*template_energy-std::norm(template_square);
        if(determinant>1e-12*template_energy*template_energy)
            fitted=2*(template_energy*std::norm(dot)-(template_square*dot*dot).real())/determinant;
    }
    return fitted;
}
inline double legacy_evidence(Complex dot,double energy,double template_energy,double count,double condition,bool real_rank,
                bool exact_real,Complex template_square) {
    if(count<4 || energy<=1e-30 || template_energy<=1e-30)return 0;
    const auto fraction=std::clamp(legacy_explained(dot,template_energy,condition,exact_real,template_square)/energy,0.,1.-1e-15);
    // Nonsingular quadrature bins use a covariance-eigenratio bound. Individual
    // real samples use the exact rank-two fit above; an ill-conditioned Gram
    // matrix retains the conservative lambda_max<=trace bound. These scores
    // assume independent Gaussian input samples, with unknown common variance.
    return -(real_rank?(count-2)/2:count-1)*std::log1p(-fraction);
}
}
