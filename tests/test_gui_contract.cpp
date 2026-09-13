// Compile the public interface without a modem or native-toolkit dependency.
#if __has_include("datapump/live.hpp") || __has_include("datapump/modem.hpp")
#error "The GUI facade exposes the application's private modem include path."
#endif
#include "application.hpp"
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

// Semantic tones must keep identical meaning for document labels, list cells
// and bitmap captions, including the full-white monochrome data accent.
static_assert(theme::text_rgb(ui::TextTone::normal,false)==theme::Rgb{240,240,240});
static_assert(theme::text_rgb(ui::TextTone::normal,true)==theme::Rgb{208,208,208});
static_assert(theme::text_rgb(ui::TextTone::data,false)==theme::Rgb{255,255,255});
static_assert(theme::text_rgb(ui::TextTone::data,true)==theme::Rgb{144,192,184});
static_assert(theme::text_rgb(ui::DocumentTone::accent,false)==theme::Rgb{255,255,255});
static_assert(theme::text_rgb(ui::DocumentTone::accent,true)==theme::Rgb{144,192,184});
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
    std::cout << "GUI interface compiles without application internals or a toolkit.\n";
}
