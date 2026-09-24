#pragma once

#include "state.hpp"
#include <FL/Fl_Choice.H>

namespace datapump::gui {
inline void populate_key_choice(Fl_Choice& choice,std::span<const std::string> names) {
    const auto labels=key_choice_labels(names);
    choice.clear();
    // FLTK add() parses separators, submenus and accelerators, then merges
    // matching names. Add unique placeholders first, before replacing any
    // label, so each stored key always has exactly one stable menu index.
    for(std::size_t index=0;index<labels.size();++index)
        choice.add(std::to_string(index).c_str(),0,nullptr);
    for(std::size_t index=0;index<labels.size();++index)
        choice.replace(static_cast<int>(index),labels[index].c_str());
    choice.value(names.empty()?0:1);
    // Explicitly reselecting the current entry also acknowledges a failed
    // keyfile load; otherwise an already-selected None cannot clear that gate.
    choice.when(FL_WHEN_RELEASE_ALWAYS);
}
}
