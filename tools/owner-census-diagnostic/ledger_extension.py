"""Private deterministic source and producer extension for workspace ledger schema 2."""

def once(text, old, new):
    count = text.count(old)
    if count != 1:
        raise ValueError(f"extension anchor must occur once ({count}): {old[:100]!r}")
    return text.replace(old, new, 1)


def live_edits(source):
    edits = []
    def add(old, new):
        edits.append((old, new))
    add('''try {::datapump::diagnostic::owner_census::capture(bank,value,raw_candidate,reason,
                observation_epoch(bank,value,bank.samples),bank_capacity(value));}catch(...){}''', '''try {::datapump::diagnostic::owner_census::capture(bank,value,raw_candidate,reason,
                observation_epoch(bank,value,bank.samples),bank_capacity(value),[](const auto& receiver) {
                    return receiver_control_margin(receiver.options.modem,receiver.options.key.has_value());
                });}catch(...){}''')
    old='            raw_candidate={};'+NS+'profile(raw_candidate,profile);raw_candidate.family=family;\n'
    # This occurs twice in the accepted preparer, once for profile and once for epoch.
    edits.append((old+'                raw_candidate.valid=true;',old.replace('family=family;','family=family;raw_candidate.stage="candidate_key_selected";')+'                raw_candidate.valid=true;'))
    add('                if(bank.working_bytes>=capacity || capacity-bank.working_bytes<=control_margin) {\n                    raw_omission("receiver allocation refused");', '                if(bank.working_bytes>=capacity || capacity-bank.working_bytes<=control_margin) {\n                    raw_candidate.capacity=capacity;raw_candidate.margin=control_margin;raw_candidate.stage="receiver_allocation_precheck_refused";\n                    raw_omission("receiver allocation refused");')
    add('raw_candidate.config_known=true;','raw_candidate.config_known=true;raw_candidate.stage="config_prepared";')
    add('raw_candidate.search_known=true;','raw_candidate.search_known=true;raw_candidate.stage="search_prepared";')
    add('                        return std::make_unique<modem::StreamingReceiver>(config,\n', '                        raw_candidate.stage="streaming_receiver_constructor_attempt";\n                        return std::make_unique<modem::StreamingReceiver>(config,\n')
    add('                        raw_candidate.allocation_known=true;\n', '                        raw_candidate.allocation_known=true;raw_candidate.stage="strategy_selected_allocation_known";\n')
    add('                    if(!strategy.supported) {unavailable(strategy.reason);continue;}', '                    if(!strategy.supported) {raw_candidate.stage="profile_preflight_selection_refused";unavailable(strategy.reason);continue;}')
    add('                } catch(const Error& error) {unavailable(error.what());continue;}\n            }', '                } catch(const Error& error) {raw_candidate.stage="profile_preflight_error_returned";unavailable(error.what());continue;}\n            }')
    add('                    if(!selection.supported) {unavailable(selection.reason);continue;}', '                    if(!selection.supported) {raw_candidate.stage="receiver_strategy_selection_refused";unavailable(selection.reason);continue;}')
    add('        if(bank.receivers.empty() && !(scheduled_quiet_only && !bank.coverage_limited) && bank.receiver_limit.empty())\n            unavailable("no receive profile fits the configured DSP workspace");', '        if(bank.receivers.empty() && !(scheduled_quiet_only && !bank.coverage_limited) && bank.receiver_limit.empty()) {\n            raw_candidate.stage="all_profiles_exhausted_no_workspace_fit";\n            unavailable("no receive profile fits the configured DSP workspace");\n        }')
    result = source
    for old, new in edits:
        result = once(result, old, new)
    return result, edits

NS = '::datapump::diagnostic::owner_census::'
