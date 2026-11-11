# Post-install privilege setup for the privileged tools, included by src/CMakeLists.txt.
#
#   ws_set_privileges(TARGETS <target>... DESTINATION <dir>
#                     SETUID OWNER <owner> GROUP <group> | CAPABILITIES <capabilities>
#                     [COMPONENT <component>])
#
# Exactly one mechanism has to be selected:
#   SETUID        set owner and group (names or numeric ids, as accepted by chown), then the
#                 setuid bit; the setuid bit is never applied if chown failed
#   CAPABILITIES  set file capabilities with setcap, e.g. "CAP_DAC_OVERRIDE=p CAP_FOWNER=p"
#
# Like install(TARGETS), the function is called at configure time and adds install rules. These
# rules run in the order they were added, so call it after the install(TARGETS) of the same
# targets, with the same DESTINATION and COMPONENT. The installed file is located at install
# time, honoring DESTDIR and the install prefix given then (cmake --install --prefix).
# Failing chown, chmod or setcap only warn.

function(ws_set_privileges)
    set(fn ws_set_privileges)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "SETUID" "DESTINATION;OWNER;GROUP;CAPABILITIES;COMPONENT" "TARGETS")
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "${fn}: unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    # A keyword given with an empty value must neither select nor skip anything. With policy
    # CMP0174 NEW an empty value ("") defines ARG_<keyword> as empty and is reported as such;
    # with OLD (the default for cmake_minimum_required < 3.31) it leaves ARG_<keyword> undefined,
    # so it counts as not given: required values still fail, an empty COMPONENT is ignored.
    # Values are therefore always checked with STREQUAL "", never only with DEFINED.
    foreach(keyword IN ITEMS TARGETS DESTINATION OWNER GROUP CAPABILITIES COMPONENT)
        set(given_${keyword} FALSE)
        if(DEFINED ARG_${keyword} OR keyword IN_LIST ARG_KEYWORDS_MISSING_VALUES)
            set(given_${keyword} TRUE)
        endif()
    endforeach()
    foreach(keyword IN ITEMS TARGETS DESTINATION)
        if("${ARG_${keyword}}" STREQUAL "")
            message(FATAL_ERROR "${fn}: ${keyword} missing or empty")
        endif()
    endforeach()

    # select the mechanism, exactly one
    if(ARG_SETUID AND given_CAPABILITIES)
        message(FATAL_ERROR "${fn}: SETUID and CAPABILITIES are mutually exclusive")
    elseif(ARG_SETUID)
        foreach(keyword IN ITEMS OWNER GROUP)
            if("${ARG_${keyword}}" STREQUAL "")
                message(FATAL_ERROR "${fn}: SETUID needs a non-empty ${keyword}")
            endif()
        endforeach()
    elseif(given_CAPABILITIES)
        if("${ARG_CAPABILITIES}" STREQUAL "")
            message(FATAL_ERROR "${fn}: CAPABILITIES must not be empty")
        endif()
        if(given_OWNER OR given_GROUP)
            message(FATAL_ERROR "${fn}: OWNER and GROUP are only valid with SETUID")
        endif()
    else()
        message(FATAL_ERROR "${fn}: select a mechanism, SETUID or CAPABILITIES")
    endif()
    if(given_COMPONENT AND "${ARG_COMPONENT}" STREQUAL "")
        message(FATAL_ERROR "${fn}: COMPONENT must not be empty")
    endif()

    # relative destinations are relative to the prefix used at install time
    if(IS_ABSOLUTE "${ARG_DESTINATION}")
        set(dir "${ARG_DESTINATION}")
    else()
        set(dir "\${CMAKE_INSTALL_PREFIX}/${ARG_DESTINATION}")
    endif()
    set(component_args "")
    if(DEFINED ARG_COMPONENT)
        set(component_args COMPONENT "${ARG_COMPONENT}")
    endif()

    # install-time code; @...@ is filled in now, ${...} and $ENV{...} are evaluated at install time
    if(ARG_SETUID)
        set(template [=[
set(_ws_file "$ENV{DESTDIR}@dir@/$<TARGET_FILE_NAME:@target@>")
# owner first, then the setuid bit: chown clears the setuid bit
execute_process(COMMAND chown "@ARG_OWNER@:@ARG_GROUP@" "${_ws_file}" RESULT_VARIABLE _ws_rc)
if(NOT _ws_rc EQUAL 0)
    message(WARNING "chown @ARG_OWNER@:@ARG_GROUP@ failed for @target@ (${_ws_rc}), setuid bit NOT applied")
else()
    execute_process(COMMAND chmod u+s "${_ws_file}" RESULT_VARIABLE _ws_rc)
    if(NOT _ws_rc EQUAL 0)
        message(WARNING "chmod u+s failed for @target@ (${_ws_rc})")
    else()
        message(STATUS "setuid bit applied to @target@ (@ARG_OWNER@:@ARG_GROUP@)")
    endif()
endif()
]=])
    else()
        set(template [=[
set(_ws_file "$ENV{DESTDIR}@dir@/$<TARGET_FILE_NAME:@target@>")
execute_process(COMMAND setcap "@ARG_CAPABILITIES@" "${_ws_file}" RESULT_VARIABLE _ws_rc)
if(NOT _ws_rc EQUAL 0)
    message(WARNING "setcap failed for @target@ (${_ws_rc})")
else()
    message(STATUS "setcap applied to @target@: @ARG_CAPABILITIES@")
endif()
]=])
    endif()

    foreach(target IN LISTS ARG_TARGETS)
        if(NOT TARGET ${target})
            message(FATAL_ERROR "${fn}: ${target} is not a target")
        endif()
        string(CONFIGURE "${template}" code @ONLY)
        install(CODE "${code}" ${component_args})
    endforeach()
endfunction()
