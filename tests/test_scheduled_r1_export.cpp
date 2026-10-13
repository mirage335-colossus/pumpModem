#include "scheduled_r1_export.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace datapump::modem;
using namespace datapump::modem::detail;
void check(bool p,const char* s){if(!p)throw std::runtime_error(s);}
struct Output {std::vector<std::pair<std::size_t,double>> rows;bool append(std::size_t i,double v){rows.emplace_back(i,v);return true;}};
int main() {
    for(unsigned trial=0;trial<4096;++trial) {
        const auto fraction=std::ldexp(static_cast<long double>((trial*713U)&((1U<<21)-1)),-21);
        const std::uint64_t block=10000,selected=block+7,end=selected+127,N=96000,C=15;
        const auto source_first=static_cast<long double>(7*N);
        const auto carry=trial%2;
        const auto original_piece=1000.L+fraction+carry*N;
        const SchedHopCoordinate anchor{1000000000000ULL,0};
        const auto mapped=anchor.add_signed(source_first+original_piece+7);
        ScheduledR1ExportInput input{1,3.75L,source_first,block,block,selected,end,N,C,
            7,7+carry,mapped,anchor,true,true};
        const auto proof=scheduled_r1_export_proof(input);check(proof.eligible,"exact-grid R1 case rejected");
        Output output;unsigned cancellation_checks=0;
        auto skip=[&](std::size_t offset){return ((offset+trial)%11)==0;};
        const auto result=scheduled_r1_export_append(output,proof,block,selected,end,skip,[&]{++cancellation_checks;});
        check(result.end==end && result.appended==output.rows.size() && cancellation_checks>0,"export endpoint/mask/poll accounting differs");
        std::size_t at=0;
        // Direct ORIGINAL anchored expression.
        for(auto physical=selected;physical<end;++physical)if(!skip(physical-block)) {
            auto original=original_piece+static_cast<long double>(physical-block);
            if(carry)original-=N;
            check(output.rows[at].first==physical-block &&
                std::bit_cast<std::uint64_t>(output.rows[at].second)==std::bit_cast<std::uint64_t>(static_cast<double>(original)),"direct ORIGINAL coordinate bits changed");
            ++at;
        }
        // Common ORIGINAL chip/piece reanchors, including fractional starts.
        at=0;
        for(auto physical=selected;physical<end;++physical)if(!skip(physical-block)) {
            const auto piece=block+((physical-block)/15)*15;
            auto within=original_piece+static_cast<long double>(piece-block);
            if(carry)within-=N;
            const auto original=within+static_cast<long double>(physical-piece);
            check(std::bit_cast<std::uint64_t>(output.rows[at++].second)==std::bit_cast<std::uint64_t>(static_cast<double>(original)),"common reanchored coordinate bits changed");
        }
        auto rejected=input;rejected.rate=1.000001L;check(!scheduled_r1_export_proof(rejected).eligible,"nonunit clock accepted");
        rejected=input;rejected.origin=std::nextafter(3.75L,4.L);check(!scheduled_r1_export_proof(rejected).eligible,"off-grid origin accepted");
        rejected=input;rejected.full_flat=false;check(!scheduled_r1_export_proof(rejected).eligible,"uncertified gain accepted");
        rejected=input;rejected.cuts_aligned=false;check(!scheduled_r1_export_proof(rejected).eligible,"uncertified cuts accepted");
        rejected=input;rejected.target_index+=2;check(!scheduled_r1_export_proof(rejected).eligible,"private target ownership changed");
    }
    check(!scheduled_r1_export_grid(0x1p31L) && !scheduled_r1_export_grid(std::numeric_limits<long double>::infinity()),"grid bound/nonfinite accepted");
    std::cout<<"R1 exact direct/common coordinate, mask, target and fallback oracles passed4096cases\n";
}
