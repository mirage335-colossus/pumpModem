# Keep the checksum-pinned upstream tree pristine. Compile a deterministic local
# overlay, with narrow received-index changes, instead of silently editing it.
function(datapump_xz_replace text_variable before after)
  string(FIND "${${text_variable}}" "${before}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Pinned XZ receive-hardening context changed; review the overlay")
  endif()
  string(REPLACE "${before}" "${after}" result "${${text_variable}}")
  set(${text_variable} "${result}" PARENT_SCOPE)
endfunction()

function(datapump_prepare_xz source destination)
  file(STRINGS "${source}/UPSTREAM.sha256" inventory)
  foreach(entry IN LISTS inventory)
    if(NOT entry MATCHES "^([0-9a-f]+)  (.+)$")
      message(FATAL_ERROR "Invalid pinned XZ inventory")
    endif()
    set(expected "${CMAKE_MATCH_1}")
    set(path "${CMAKE_MATCH_2}")
    file(SHA256 "${source}/${path}" actual)
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "Modified pinned XZ source: ${path}")
    endif()
    get_filename_component(parent "${destination}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    if(path STREQUAL "src/liblzma/lz/lz_decoder.h")
      file(READ "${source}/${path}" text)
      datapump_xz_replace(text [=[#include "common.h"]=]
        [=[#include "common.h"
#include "datapump/speculation.h"]=])
      datapump_xz_replace(text [=[	return dict->buf[dict->pos - distance - 1
			+ (distance < dict->pos
				? 0 : dict->size - LZ_DICT_REPEAT_MAX)];]=]
        [=[	/* Preserve the history check as a register dependency. The final
	 * clipping also contains a mispredicted dictionary-wrap branch. */
	const size_t safe_distance = datapump_index_nospec(distance, dict->full);
	const size_t offset = dict->pos - safe_distance - 1
			+ (safe_distance < dict->pos
				? 0 : dict->size - LZ_DICT_REPEAT_MAX);
	return dict->buf[datapump_index_nospec(offset, dict->size)];]=])
      datapump_xz_replace(text [=[	size_t back = dict->pos - distance - 1;
	if (distance >= dict->pos)
		back += dict->size - LZ_DICT_REPEAT_MAX;]=]
        [=[	/* Upstream validates match distances before reaching this helper.
	 * Keep that bound in the address dependency too while preserving the
	 * optimized dictionary-copy loop. */
	distance = (uint32_t)datapump_index_nospec(distance, dict->full);
	size_t back = dict->pos - distance - 1;
	if (distance >= dict->pos)
		back += dict->size - LZ_DICT_REPEAT_MAX;
	back = datapump_index_nospec(back, dict->size - left + 1);]=])
    elseif(path STREQUAL "src/liblzma/lz/lz_decoder.c")
      file(READ "${source}/${path}" text)
      datapump_xz_replace(text [=[	coder->dict.buf[LZ_DICT_INIT_POS - 1] = '\0';]=]
        [=[	/* Initialize the bounded fallback region used by the receive
	 * hardening overlay, including an empty/reset dictionary and its
	 * possible zero-length SIMD read past the initial position. */
	memzero(coder->dict.buf, LZ_DICT_INIT_POS + LZ_DICT_EXTRA);]=])
    elseif(path STREQUAL "src/liblzma/lzma/lzma2_decoder.c")
      file(READ "${source}/${path}" text)
      datapump_xz_replace(text [=[		coder->lzma.reset(coder->lzma.coder, &coder->options);]=]
        [=[		/* Received properties select probability-table geometry. */
		datapump_speculation_barrier();
		coder->lzma.reset(coder->lzma.coder, &coder->options);]=])
    else()
      configure_file("${source}/${path}" "${destination}/${path}" COPYONLY)
      continue()
    endif()
    set(previous "")
    if(EXISTS "${destination}/${path}")
      file(READ "${destination}/${path}" previous)
    endif()
    if(NOT previous STREQUAL text)
      file(WRITE "${destination}/${path}" "${text}")
    endif()
  endforeach()
endfunction()
