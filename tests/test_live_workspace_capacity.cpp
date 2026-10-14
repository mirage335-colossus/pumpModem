#include "datapump/live.hpp"
#include "datapump/sched_hop.hpp"
#include <iostream>
#include <stdexcept>
using namespace datapump;using namespace datapump::modem;
#define CHECK(c) do{if(!(c))throw std::runtime_error("check failed: " #c);}while(false)
int main(){try {
    live::Settings raw;raw.transfer.dsp_workspace_bytes=64*1024*1024;
    auto explicit_fields=raw;explicit_fields.dsp_workspace_bytes=raw.transfer.dsp_workspace_bytes;
    explicit_fields.content_limit=raw.transfer.content_limit;
    CHECK(live::receiver_workspace_capacity(raw)==live::receiver_workspace_capacity(explicit_fields));
    explicit_fields.dsp_workspace_bytes=192*1024*1024;
    auto explicit_transfer=explicit_fields;explicit_transfer.transfer.dsp_workspace_bytes=192*1024*1024;
    CHECK(live::receiver_workspace_capacity(explicit_fields)==live::receiver_workspace_capacity(explicit_transfer));
    auto simulation=explicit_fields;simulation.simulation=true;
    CHECK(live::receiver_workspace_capacity(simulation)>live::receiver_workspace_capacity(explicit_fields));
    CHECK(live::receiver_control_margin(raw.transfer.modem,true)==live::receiver_control_margin(raw.transfer.modem,false));
    auto long_config=raw.transfer.modem;long_config.bandwidth_hz=1;long_config.spreading_factor=512;
    CHECK(symbol_sample_count(long_config)>=60ULL*long_config.sample_rate);
    CHECK(live::receiver_control_margin(long_config,true)+57344==live::receiver_control_margin(long_config,false));
    Crypto key(Bytes(32,0x3a));
    for(const auto multiplier:{10U,100U,1000U,10000U}) {
        auto scheduled=raw;scheduled.transfer.key=key;scheduled.transfer.modem.data_key=key;
        scheduled.transfer.modem.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{multiplier,1});
        auto normalized=scheduled;normalized.dsp_workspace_bytes=scheduled.transfer.dsp_workspace_bytes;
        normalized.content_limit=scheduled.transfer.content_limit;
        CHECK(live::receiver_workspace_capacity(scheduled)==live::receiver_workspace_capacity(normalized));
    }
    std::cout<<"normalized pure Live workspace checks passed (no Session/device)\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
