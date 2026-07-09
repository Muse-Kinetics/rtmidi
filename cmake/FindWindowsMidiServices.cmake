include(FindPackageHandleStandardArgs)

if(NOT WIN32)
  set(WindowsMidiServices_FOUND FALSE)
  set(WindowsMidiServices_NOT_FOUND_MESSAGE "Windows MIDI Services is only available on Windows.")
  return()
endif()

set(_wms_roots)
if(DEFINED WindowsMidiServices_ROOT)
  list(APPEND _wms_roots "${WindowsMidiServices_ROOT}")
endif()
if(DEFINED ENV{WindowsMidiServices_ROOT})
  file(TO_CMAKE_PATH "$ENV{WindowsMidiServices_ROOT}" _wms_env_root)
  list(APPEND _wms_roots "${_wms_env_root}")
endif()
list(APPEND _wms_roots
  "C:/Program Files/Windows MIDI Services"
  "C:/Program Files (x86)/Windows MIDI Services")
list(REMOVE_DUPLICATES _wms_roots)

find_file(WindowsMidiServices_WINMD
  NAMES Microsoft.Windows.Devices.Midi2.winmd
  PATHS ${_wms_roots}
  PATH_SUFFIXES "Desktop App SDK Runtime")

find_package_handle_standard_args(WindowsMidiServices
  REQUIRED_VARS WindowsMidiServices_WINMD)

mark_as_advanced(WindowsMidiServices_WINMD)