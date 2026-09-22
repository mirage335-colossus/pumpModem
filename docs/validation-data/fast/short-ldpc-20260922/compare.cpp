#include "datapump/fast/codec.hpp"
#include "datapump/fast/preset.hpp"
#include "datapump/fast/modem.hpp"
#include <cmath>
#include <iostream>
using namespace datapump::fast;
void show(const char* name,const Profile& p){std::cout<<name<<" n="<<p.ldpc_frame_bits<<" rate="<<code_rate_name(p.code_rate)<<" depth="<<p.interleave_depth<<" marker="<<p.marker_spacing_intervals<<" pilots="<<p.pilot_spacing_symbols<<" minimum="<<estimate_transmission(p,true,60).seconds<<" public="<<estimate_transmission(p,false,50000000).source_bps<<" keyed="<<estimate_transmission(p,true,50000000).source_bps<<" file="<<estimate_transmission(p,true,2660).seconds<<'\n';}
int main(){auto p=resolve_snr_preset(Channel::acoustic_short,-6).profile;show("selected",p);auto q=p;q.compact_convolutional=true;q.ldpc_frame_bits=16200;q.code_rate=CodeRate::half;q.pilot_spacing_symbols=64;show("previous",q);q=p;q.interleave_depth=2;show("extended",q);q.marker_spacing_intervals=2;show("rejected_sparse",q);q=p;q.code_rate=CodeRate::two_thirds;show("stronger",q);q=p;q.code_rate=CodeRate::half;show("half",q);q=p;q.ldpc_frame_bits=648;q.interleave_depth=3;show("648",q);q=p;q.ldpc_frame_bits=1296;show("1296",q);}
