// Compile the public interface without a modem or native-toolkit dependency.
#include "application.hpp"
#include "document_layout.hpp"
#include "text_policy.hpp"
#include <concepts>
#include <iostream>
#include <type_traits>

template<class T> concept ExposesController = requires(T& value) { value.controller; };
template<class T> concept ExposesProducers = requires(T& value) { value.bitmaps; };
template<class T> concept ExposesModem = requires(T& value) { value.snapshot(); value.settings(); };
template<class T> concept ExposesInspection = requires(T& value) { value.inspection(); };
template<class T> concept HasDomainFactory = requires { T::codeword(1, 1); };

using namespace datapump::gui;
static_assert(!ExposesController<Application> && !ExposesProducers<Application>);
static_assert(!ExposesModem<Application> && !ExposesInspection<Application>);
static_assert(!HasDomainFactory<BitmapSource>);
static_assert(std::same_as<decltype(ui::DocumentNode{}.plot), BitmapSource>);
static_assert(std::same_as<decltype(BitmapPresentation{}.source), BitmapSource>);
static_assert(std::is_copy_constructible_v<BitmapSource>);

int main() {
    std::cout << "GUI interface compiles without application internals or a toolkit.\n";
}
