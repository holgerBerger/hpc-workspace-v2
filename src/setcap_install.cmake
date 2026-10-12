file(TO_NATIVE_PATH "${CMAKE_INSTALL_PREFIX}/bin/ws_allocate" _WS_CAPA_ALLOCATE)
execute_process(
    COMMAND setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" "${_WS_CAPA_ALLOCATE}"
    RESULT_VARIABLE _WS_CAPA_ALLOCATE_RC)
if(_WS_CAPA_ALLOCATE_RC)
    message(WARNING "setcap failed for ws_allocate")
endif()
if(NOT _WS_CAPA_ALLOCATE_RC)
    message(STATUS "setcap applied to ws_allocate")
endif()

file(TO_NATIVE_PATH "${CMAKE_INSTALL_PREFIX}/bin/ws_release" _WS_CAPA_RELEASE)
execute_process(
    COMMAND setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" "${_WS_CAPA_RELEASE}"
    RESULT_VARIABLE _WS_CAPA_RELEASE_RC)
if(_WS_CAPA_RELEASE_RC)
    message(WARNING "setcap failed for ws_release")
endif()
if(NOT _WS_CAPA_RELEASE_RC)
    message(STATUS "setcap applied to ws_release")
endif()

file(TO_NATIVE_PATH "${CMAKE_INSTALL_PREFIX}/bin/ws_restore" _WS_CAPA_RESTORE)
execute_process(
    COMMAND setcap "CAP_DAC_OVERRIDE=p CAP_DAC_READ_SEARCH=p CAP_FOWNER=p" "${_WS_CAPA_RESTORE}"
    RESULT_VARIABLE _WS_CAPA_RESTORE_RC)
if(_WS_CAPA_RESTORE_RC)
    message(WARNING "setcap failed for ws_restore")
endif()
if(NOT _WS_CAPA_RESTORE_RC)
    message(STATUS "setcap applied to ws_restore")
endif()
