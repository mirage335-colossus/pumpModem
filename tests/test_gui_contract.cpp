// Compile the public interface without a modem or native-toolkit dependency.
#if __has_include("datapump/live.hpp") || __has_include("datapump/modem.hpp")
#error "The GUI facade exposes the application's private modem include path."
#endif
#include "application.hpp"
#include "binding_state.hpp"
#include "chrome_layout.hpp"
#include "document_presentation.hpp"
#include "record_reconciliation.hpp"
#include "control_binding.hpp"
#include "control_interactions.hpp"
#include "document_actions.hpp"
#include "document_layout.hpp"
#include "presentation_palette.hpp"
#include "record_interactions.hpp"
#include "record_scroll.hpp"
#include "service_queue.hpp"
#include "text_policy.hpp"
#include <concepts>
#include <iostream>
#include <type_traits>

template<class T> concept ExposesController = requires(T& value) { value.controller; };
template<class T> concept ExposesProducers = requires(T& value) { value.bitmaps; };
template<class T> concept ExposesModem = requires(T& value) { value.snapshot(); value.settings(); };
template<class T> concept ExposesInspection = requires(T& value) { value.inspection(); };
template<class T> concept ExposesImplementation = requires(T& value) { value.impl_; };
template<class T> concept HasDomainFactory = requires { T::codeword(1, 1); };

using namespace datapump::gui;
static_assert(!ExposesController<Application> && !ExposesProducers<Application>);
static_assert(!ExposesModem<Application> && !ExposesInspection<Application>);
static_assert(!ExposesImplementation<Application>);
static_assert(!HasDomainFactory<BitmapSource>);
static_assert(std::same_as<decltype(ui::DocumentNode{}.plot), BitmapSource>);
static_assert(std::same_as<decltype(BitmapPresentation{}.source), BitmapSource>);
static_assert(std::is_copy_constructible_v<BitmapSource>);
// Both native backends bind these shared fields and slots. Model estimates
// use shared visibility rules without introducing toolkit-specific controls.
static_assert(ui::persistent_slot(ui::Slot::link_power));
static_assert(ui::persistent_slot(ui::Slot::link_loss));
static_assert(ui::persistent_slot(ui::Slot::link_noise));
static_assert(ui::persistent_slot(ui::Slot::simulation_confidence));
static_assert(ui::persistent_slot(ui::Slot::simulation_cpu_time));
static_assert(ui::persistent_slot(ui::Slot::simulation_gpu_time));
static_assert(ui::persistent_slot(ui::Slot::simulation_oscillator));
static_assert(ui::persistent_slot(ui::Slot::simulation_oscillator_detail));
static_assert(ui::persistent_slot(ui::Slot::lpi_estimate));
static_assert(ui::Field::simulation_confidence!=ui::Field::simulation_cpu_time&&
              ui::Field::simulation_cpu_time!=ui::Field::simulation_gpu_time);

// Semantic tones must keep identical meaning for document labels, list cells
// and bitmap captions, including the full-white monochrome data accent.
static_assert(theme::text_rgb(ui::TextTone::normal,false)==theme::Rgb{240,240,240});
static_assert(theme::text_rgb(ui::TextTone::normal,true)==theme::Rgb{208,208,208});
static_assert(theme::text_rgb(ui::TextTone::data,false)==theme::Rgb{255,255,255});
static_assert(theme::text_rgb(ui::TextTone::data,true)==theme::Rgb{144,192,184});
static_assert(theme::text_rgb(ui::TextTone::negative,true)==theme::negative_tint);
static_assert(theme::text_rgb(ui::TextTone::negative,false)==theme::text_rgb(ui::TextTone::normal,false));
static_assert(theme::text_rgb(ui::TextTone::negative,true)==theme::text_rgb(ui::DocumentTone::negative,true));
static_assert(theme::text_rgb(ui::DocumentTone::accent,false)==theme::Rgb{255,255,255});
static_assert(theme::text_rgb(ui::DocumentTone::accent,true)==theme::Rgb{144,192,184});
static_assert(theme::text_rgb(ui::DocumentTone::positive,true).green>theme::text_rgb(ui::DocumentTone::positive,true).red);
static_assert(theme::text_rgb(ui::DocumentTone::negative,true).red>theme::text_rgb(ui::DocumentTone::negative,true).green);
static_assert(theme::text_rgb(ui::DocumentTone::caution,true).red>theme::text_rgb(ui::DocumentTone::caution,true).blue);
static_assert(theme::text_rgb(ui::DocumentTone::positive,false)==theme::text_rgb(ui::DocumentTone::negative,false));
static_assert([] {
    for(bool color:{false,true}) {
        const auto disabled=theme::widget_rgb(theme::WidgetRole::disabled_text,color);
        for(auto tone:{ui::TextTone::normal,ui::TextTone::muted,ui::TextTone::data,ui::TextTone::inverse,ui::TextTone::negative})
            if(theme::text_rgb(tone,color,false)!=disabled)return false;
        for(auto tone:{ui::DocumentTone::text,ui::DocumentTone::muted,ui::DocumentTone::accent,ui::DocumentTone::comparison,
                       ui::DocumentTone::positive,ui::DocumentTone::caution,ui::DocumentTone::negative})
            if(theme::text_rgb(tone,color,false)!=disabled)return false;
    }
    for(auto fill:{ui::DocumentFill::none,ui::DocumentFill::surface,ui::DocumentFill::alternate,ui::DocumentFill::parity})
        if(theme::document_fill_rgb(fill,true,false)!=theme::widget_rgb(theme::WidgetRole::disabled_background))return false;
    return true;
}());
static_assert([] {
    for(bool color:{false,true}) {
        if(theme::text_rgb(ui::TextTone::muted,color)!=theme::Rgb{160,160,160} ||
           theme::text_rgb(ui::TextTone::inverse,color)!=theme::Rgb{0,0,0} ||
           theme::text_rgb(ui::DocumentTone::text,color)!=theme::text_rgb(ui::TextTone::normal,color) ||
           theme::text_rgb(ui::DocumentTone::muted,color)!=theme::text_rgb(ui::TextTone::muted,color))return false;
    }
    return true;
}());
static_assert(!theme::document_fill_rgb(ui::DocumentFill::none));
static_assert(theme::document_fill_rgb(ui::DocumentFill::none,true)==theme::Rgb{16,16,16});
static_assert([] {
    for(bool action:{false,true}) {
        if(theme::document_fill_rgb(ui::DocumentFill::surface,action)!=theme::Rgb{16,16,16} ||
           theme::document_fill_rgb(ui::DocumentFill::alternate,action)!=theme::Rgb{0,0,0} ||
           theme::document_fill_rgb(ui::DocumentFill::parity,action)!=theme::Rgb{80,80,80})return false;
    }
    return true;
}());

int main() {
    // GCC 11's standard library does not make FieldState's string members
    // literal types. Keep this default-value check active in Release too.
    if (ui::FieldState{}.text_tone != ui::TextTone::normal) {
        std::cerr << "Default GUI field text tone must be normal.\n";
        return 1;
    }
    std::cout << "GUI interface compiles without application internals or a toolkit.\n";
}
