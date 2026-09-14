cmake_minimum_required(VERSION 3.21)
# Native adapters can see only the public GUI vocabulary and native helpers.
# Traverse includes as well as entry points: moving a domain decision into a
# helper must not bypass the boundary. New application fields/pages/producers
# do not require changes to this list; a new public primitive does.
set(contract_headers
  application.hpp bitmap.hpp ui_contract.hpp ui_document.hpp
  desktop_layout.hpp control_layout.hpp document_layout.hpp document_actions.hpp
  control_binding.hpp record_interactions.hpp presentation_palette.hpp
  text_policy.hpp utf8_policy.hpp control_interactions.hpp record_scroll.hpp service_queue.hpp chrome_layout.hpp theme.hpp
  binding_state.hpp record_reconciliation.hpp document_presentation.hpp overlay.hpp)
set(native_headers backend_fltk_document.hpp backend_rev_document.hpp
  bitmap_fltk.hpp theme_fltk.hpp backend_rev_theme.hpp rev_platform.hpp)

# Public headers may depend only on each other and the standard library. An
# unknown angle include is not automatically a system header: that used to let
# project-root and vendored toolkit includes bypass the public boundary.
set(standard_headers
  algorithm any array atomic barrier bit bitset cassert cctype cerrno cfenv
  cfloat charconv chrono cinttypes ciso646 climits clocale cmath codecvt compare
  complex concepts condition_variable coroutine csetjmp csignal cstdarg cstddef
  cstdint cstdio cstdlib cstring ctgmath ctime cuchar cwchar cwctype deque
  exception execution expected filesystem format forward_list fstream functional
  future generator initializer_list iomanip ios iosfwd iostream istream iterator
  latch limits list locale map mdspan memory memory_resource mutex new numbers
  numeric optional ostream print queue random ranges ratio regex scoped_allocator
  semaphore set shared_mutex source_location span spanstream sstream stack
  stdexcept stop_token streambuf string string_view strstream syncstream system_error
  thread tuple type_traits typeindex typeinfo unordered_map unordered_set utility
  valarray variant vector version assert.h ctype.h errno.h fenv.h float.h
  inttypes.h iso646.h limits.h locale.h math.h setjmp.h signal.h stdarg.h
  stdbool.h stddef.h stdint.h stdio.h stdlib.h string.h time.h uchar.h wchar.h wctype.h)

# Normalize the preprocessing constructs that otherwise hide dependencies or
# split identifiers. Preserve string/character literals while removing comments,
# including strings containing URLs or comment-looking text. This remains a
# source architecture lint, not a replacement for compiling the public headers.
function(read_gui_source path output)
  file(READ "${path}" remaining)
  string(REPLACE "\\\r\n" "" remaining "${remaining}")
  string(REPLACE "\\\n" "" remaining "${remaining}")
  set(source "")
  while(TRUE)
    string(REGEX MATCH "\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'|/\\*([^*]|\\*+[^*/])*\\*+/|//[^\n]*" token "${remaining}")
    if(token STREQUAL "")
      string(APPEND source "${remaining}")
      break()
    endif()
    string(FIND "${remaining}" "${token}" offset)
    string(SUBSTRING "${remaining}" 0 ${offset} prefix)
    string(APPEND source "${prefix}")
    string(LENGTH "${token}" length)
    math(EXPR offset "${offset}+${length}")
    string(SUBSTRING "${remaining}" ${offset} -1 remaining)
    if(token MATCHES "^/[/\\*]")
      string(REGEX REPLACE "[^\n]" " " token "${token}")
    endif()
    string(APPEND source "${token}")
  endwhile()
  set(${output} "${source}" PARENT_SCOPE)
endfunction()

function(binding_aliases source output)
  string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'" " " source "${source}")
  get_property(binding_types GLOBAL PROPERTY gui_binding_types)
  set(previous_types "")
  while(NOT "${previous_types}" STREQUAL "${binding_types}")
    set(previous_types "${binding_types}")
    list(JOIN binding_types "|" type_pattern)
    string(REGEX MATCHALL "using[ \t\r\n]+[A-Za-z_][A-Za-z_0-9]*[ \t\r\n]*=[ \t\r\n]*([A-Za-z_][A-Za-z_0-9]*[ \t\r\n]*::[ \t\r\n]*)*(${type_pattern})[ \t\r\n]*;" aliases "${source}")
    foreach(alias IN LISTS aliases)
      if(alias STREQUAL "")
        continue()
      endif()
      string(REGEX REPLACE "^using[ \t\r\n]+([A-Za-z_][A-Za-z_0-9]*).*" "\\1" alias_name "${alias}")
      list(APPEND binding_types "${alias_name}")
    endforeach()
    string(REGEX MATCHALL "typedef[ \t\r\n]+([A-Za-z_][A-Za-z_0-9]*[ \t\r\n]*::[ \t\r\n]*)*(${type_pattern})[ \t\r\n]+[A-Za-z_][A-Za-z_0-9]*[ \t\r\n]*;" aliases "${source}")
    foreach(alias IN LISTS aliases)
      if(alias STREQUAL "")
        continue()
      endif()
      string(REGEX REPLACE ".*[ \t\r\n]([A-Za-z_][A-Za-z_0-9]*)[ \t\r\n]*;?$" "\\1" alias_name "${alias}")
      list(APPEND binding_types "${alias_name}")
    endforeach()
    list(REMOVE_DUPLICATES binding_types)
  endwhile()
  set(${output} "${binding_types}" PARENT_SCOPE)
endfunction()

function(check_application_bindings name source native)
  string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'" " " source "${source}")
  # A public helper must not become a hiding place for a backend's application
  # decisions. Only shared desktop geometry and declaration defaults name actual
  # bindings below this boundary; all other helpers operate on opaque values.
  if(NOT native)
    if(name STREQUAL "application.hpp" OR name STREQUAL "ui_contract.hpp")
      string(REGEX REPLACE "(ui::)?Page[ \t\r\n]+page[ \t\r\n]*=[ \t\r\n]*(ui::)?Page[ \t\r\n]*::[ \t\r\n]*console[ \t\r\n]*;" "" source "${source}")
    endif()
  endif()
  get_property(binding_types GLOBAL PROPERTY gui_binding_types)
  list(JOIN binding_types "|" type_pattern)
  if(source MATCHES "using[ \t\r\n]+enum[ \t\r\n]+([A-Za-z_][A-Za-z_0-9]*[ \t\r\n]*::[ \t\r\n]*)*(${type_pattern})[ \t\r\n]*;")
    message(FATAL_ERROR "${name} imports application bindings with using enum; keep binding values opaque.")
  endif()
  # CMake lists retain semicolons inside unmatched square brackets. Remove
  # unrelated punctuation before collecting references with a leading token
  # boundary, so array-index expressions cannot merge multiple references.
  string(REGEX REPLACE "[^A-Za-z_0-9: \t\r\n]" " " source "${source}")
  string(REGEX MATCHALL "(^|[^A-Za-z_0-9])(${type_pattern})[ \t\r\n]*::[ \t\r\n]*[A-Za-z_][A-Za-z_0-9]*" references "${source}")
  foreach(reference IN LISTS references)
    string(REGEX REPLACE "^[^A-Za-z_]" "" reference "${reference}")
    string(REGEX REPLACE "[ \t\r\n]" "" reference "${reference}")
    if(reference MATCHES "::(none|count)$")
      continue()
    endif()
    if(NOT native)
      if(name STREQUAL "desktop_layout.hpp" AND reference MATCHES "^Slot::")
        continue()
      elseif(name STREQUAL "control_layout.hpp" AND reference MATCHES "^Slot::(page|tabs)$")
        continue()
      endif()
    endif()
    message(FATAL_ERROR "${name} contains application binding ${reference}; use the shared declaration/presentation.")
  endforeach()
endfunction()

function(check_gui_boundary path native)
  get_filename_component(path "${path}" ABSOLUTE)
  get_property(visited GLOBAL PROPERTY gui_boundary_visited)
  if(path IN_LIST visited)
    return()
  endif()
  set_property(GLOBAL APPEND PROPERTY gui_boundary_visited "${path}")
  read_gui_source("${path}" source)
  get_filename_component(name "${path}" NAME)
  if(native)
    string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'" " " color_source "${source}")
    if(color_source MATCHES "(rgba|fl_rgb_color)[ \t\r\n]*\\([ \t\r\n]*[0-9]")
      message(FATAL_ERROR "${name} defines a literal native color; use the shared theme palette.")
    endif()
  endif()
  # Store normalized source for the second pass, after aliases from all public
  # and native helpers are known. A helper alias must not conceal an application
  # binding in a translation unit that includes it.
  string(SHA256 key "${path}")
  set_property(GLOBAL PROPERTY "gui_source_${key}" "${source}")
  set_property(GLOBAL PROPERTY "gui_native_${key}" "${native}")
  if(source MATCHES "#[ \t]*include[ \t]*[<\"]datapump/")
    message(FATAL_ERROR "${name} includes modem/application internals below the GUI boundary.")
  endif()
  if(source MATCHES "#[ \t]*(include_next|import)([^A-Za-z_0-9]|$)")
    message(FATAL_ERROR "${name} uses an unsupported include directive; use explicit literal includes.")
  endif()
  string(REGEX MATCHALL "(^|\n)[ \t]*#[ \t]*include[^\n]*|(^|[;\n])[ \t]*(export[ \t]+)?import[ \t]+[^;\n]+" includes "${source}")
  foreach(include IN LISTS includes)
    string(STRIP "${include}" include)
    if(include STREQUAL "")
      continue()
    endif()
    if(include MATCHES "^(export[ \t]+)?import[ \t]+(std|std\\.compat)[ \t]*$")
      continue()
    elseif(native AND include MATCHES "^import[ \t]+Rev\\.[A-Za-z_0-9.]+[ \t]*$")
      continue()
    endif()
    if(NOT include MATCHES "^(#[ \t]*include[ \t]*|(export[ \t]+)?import[ \t]+)[<\"]([^>\"]+)[>\"][ \t]*$")
      message(FATAL_ERROR "${name} has a nonliteral include or unsupported module import; dependencies must be explicit: ${include}")
    endif()
    set(header "${CMAKE_MATCH_3}")
    if(header IN_LIST standard_headers)
      continue()
    endif()
    if(header MATCHES "^(FL/|Rev/|third_party/(rev|fltk)/)" AND NOT native)
      message(FATAL_ERROR "Public GUI header ${name} depends on a native toolkit.")
    endif()
    # Native regression methods are compiled only into the dedicated test
    # executable. They may use fixtures but production must not include them.
    if(header STREQUAL "../../tests/rev_adapter_probes.inc")
      if(NOT source MATCHES "#ifdef DATAPUMP_REV_ADAPTER_TEST[ \t\r\n]+#include \"../../tests/rev_adapter_probes.inc\"[ \t\r\n]+#endif")
        message(FATAL_ERROR "Rev native probes must be guarded by DATAPUMP_REV_ADAPTER_TEST.")
      endif()
      continue()
    endif()
    if(header IN_LIST contract_headers)
      check_gui_boundary("${ROOT}/src/gui/${header}" FALSE)
    elseif(native AND header IN_LIST native_headers)
      check_gui_boundary("${ROOT}/src/gui/${header}" TRUE)
    elseif(native AND include MATCHES "<" AND header MATCHES "^(FL/|X11/|sys/|windows\\.h$|shellapi\\.h$|unistd\\.h$)")
      continue() # Toolkit drawing and native platform services.
    else()
      message(FATAL_ERROR "${name} includes '${header}' outside the public GUI/native helper boundary.")
    endif()
  endforeach()
endfunction()

# Discover future source extensions and nested helpers too. Native sources must
# use the backend_ prefix or the explicitly named platform/bitmap helpers.
file(GLOB_RECURSE gui_sources LIST_DIRECTORIES FALSE "${ROOT}/src/gui/*")
list(FILTER gui_sources INCLUDE REGEX "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inc|inl|ipp|ixx|cppm)$")
set(adapters ${gui_sources})
list(FILTER adapters INCLUDE REGEX "/backend_[^/]+\\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inc|inl|ipp|ixx|cppm)$")
list(APPEND adapters "${ROOT}/src/gui/bitmap_fltk.hpp" "${ROOT}/src/gui/theme_fltk.hpp"
  "${ROOT}/src/gui/rev_platform.cpp" "${ROOT}/src/gui/rev_platform.hpp" "${ROOT}/src/gui/rev_entry_win.cpp")
foreach(path IN LISTS adapters)
  check_gui_boundary("${path}" TRUE)
endforeach()
foreach(name IN LISTS contract_headers)
  check_gui_boundary("${ROOT}/src/gui/${name}" FALSE)
endforeach()

# Resolve alias chains across header boundaries until stable, then check each
# source's own decisions. Merely including a declaration is still permitted.
set_property(GLOBAL PROPERTY gui_binding_types Field Command Page Bitmap Slot Menu)
get_property(visited GLOBAL PROPERTY gui_boundary_visited)
set(previous_types "")
while(TRUE)
  foreach(path IN LISTS visited)
    string(SHA256 key "${path}")
    get_property(source GLOBAL PROPERTY "gui_source_${key}")
    binding_aliases("${source}" binding_types)
    set_property(GLOBAL PROPERTY gui_binding_types "${binding_types}")
  endforeach()
  if("${previous_types}" STREQUAL "${binding_types}")
    break()
  endif()
  set(previous_types "${binding_types}")
endwhile()
foreach(path IN LISTS visited)
  string(SHA256 key "${path}")
  get_property(source GLOBAL PROPERTY "gui_source_${key}")
  get_property(native GLOBAL PROPERTY "gui_native_${key}")
  get_filename_component(name "${path}" NAME)
  check_application_bindings("${name}" "${source}" "${native}")
endforeach()

# The other direction matters too: feature code cannot call a toolkit directly.
foreach(path IN LISTS gui_sources)
  if(path IN_LIST adapters)
    continue()
  endif()
  read_gui_source("${path}" source)
  if(source MATCHES "(#[ \t]*include|import[ \t]+)[^\n]*(FL/|backend_|bitmap_fltk|theme_fltk|rev_platform|third_party/(rev|fltk))|import[ \t]+Rev\\.|Rev[ \t\r\n]*::|(^|[^A-Za-z_0-9])Fl_[A-Za-z_]")
    message(FATAL_ERROR "Shared GUI source ${path} depends on a native adapter/toolkit.")
  endif()
  if(source MATCHES "#[ \t]*include[ \t]+[^<\" \t\r\n]")
    message(FATAL_ERROR "Shared GUI source ${path} has a nonliteral include; dependencies must be explicit.")
  endif()
  if(source MATCHES "#[ \t]*(if|ifdef|ifndef|elif)[^\n]*(DATAPUMP_GUI_BACKEND|DATAPUMP_(FLTK|REV)_ADAPTER|FLTK_VERSION|FL_MAJOR_VERSION)")
    message(FATAL_ERROR "Shared GUI source ${path} branches on a native backend; feature behavior must be shared.")
  endif()
endforeach()

foreach(name GuiFltk.cmake GuiRev.cmake)
  file(READ "${ROOT}/cmake/${name}" source)
  string(REGEX MATCHALL "src/gui/[A-Za-z_0-9./-]+\\.(cppm|cpp|cxx|cc|ixx|c)" sources "${source}")
  foreach(path IN LISTS sources)
    set(absolute "${ROOT}/${path}")
    if(NOT absolute IN_LIST adapters)
      message(FATAL_ERROR "${name} duplicates shared application source ${path}; link datapump_gui_application.")
    endif()
  endforeach()
endforeach()
foreach(name main.cpp live_widgets.hpp inspection_widgets.cpp inspection_widgets.hpp pattern_space_view.hpp screen_inspection.cpp backend_rev_inspection.hpp)
  if(EXISTS "${ROOT}/src/gui/${name}")
    message(FATAL_ERROR "Obsolete parallel GUI implementation remains: ${name}")
  endif()
endforeach()
