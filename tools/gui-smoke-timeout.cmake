# Optional native GUI workflow allowance. Keep the existing GUI and process
# defaults when omitted; this setting never changes CLI verification timeouts.
if(DEFINED GUI_SMOKE_TIMEOUT)
  if(NOT GUI_SMOKE_TIMEOUT MATCHES "^[1-9][0-9]*$" OR
      GUI_SMOKE_TIMEOUT LESS 10 OR GUI_SMOKE_TIMEOUT GREATER 600)
    message(FATAL_ERROR "GUI_SMOKE_TIMEOUT must be an integer from 10 to 600 seconds")
  endif()
  math(EXPR GUI_SMOKE_PROCESS_TIMEOUT "${GUI_SMOKE_TIMEOUT} + 30")
endif()
