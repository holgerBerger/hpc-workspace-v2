if(BUILD_WITH_VCPKG)
    find_package(CURL CONFIG REQUIRED)
    find_package(fmt CONFIG REQUIRED)
    find_package(Microsoft.GSL CONFIG)
    find_package(glaze CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
endif()

find_package(Threads REQUIRED)

# Check for C++17 thread support
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++23" COMPILER_SUPPORTS_CXX23)
if(NOT COMPILER_SUPPORTS_CXX23)
    message(FATAL_ERROR "Compiler does not support C++23")
endif()

add_library(ws_common
    build_info.h
    caps.cpp
    caps.h
    config.cpp
    config.h
    db.h
    dbv1.cpp
    dbv1.h
    user.cpp
    user.h
    utils.cpp
    utils.h
    UserConfig.cpp
)
target_compile_features(ws_common PUBLIC cxx_std_23)



if(BUILD_WITH_VCPKG)
    # ms-gsl is header-only, no cmake config — just need the include dir
    find_path(MS_GSL_INCLUDE_DIR NAMES gsl/gsl)
    if(MS_GSL_INCLUDE_DIR)
        target_include_directories(ws_common PRIVATE ${MS_GSL_INCLUDE_DIR})
    endif()
    set(GSL_TARGET "" CACHE STRING "" FORCE)
else()
    set(GSL_TARGET Microsoft.GSL::GSL CACHE STRING "" FORCE)
endif()

target_link_libraries(ws_common
    PUBLIC
        fmt::fmt
        glaze::glaze
        ${LIBCAP}
        spdlog::spdlog
    PRIVATE
        ${GSL_TARGET}
)

# Mail library with curl dependency
add_library(mail
    mail.cpp
    mail.h
)
target_compile_features(mail PUBLIC cxx_std_23)
target_link_libraries(mail
    PUBLIC
        CURL::libcurl
        spdlog::spdlog
    PRIVATE
        fmt::fmt
)
target_include_directories(mail PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(ws_list
    ws_list.cpp
)
if(NOT BUILD_WITH_VCPKG)
    target_include_directories(ws_list PRIVATE ${thread_pool_SOURCE_DIR}/include)
else()
    find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIR NAMES BS_thread_pool.hpp)
    if(BSHOSHANY_THREAD_POOL_INCLUDE_DIR)
        target_include_directories(ws_list PRIVATE ${BSHOSHANY_THREAD_POOL_INCLUDE_DIR})
    endif()
endif()
target_link_libraries(ws_list
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
        Threads::Threads
)

add_executable(ws_find
    ws_find.cpp
)
target_link_libraries(ws_find
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
)

add_executable(ws_allocate
    ws_allocate.cpp
)
target_link_libraries(ws_allocate
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
)

add_executable(ws_release
    ws_release.cpp
)
target_link_libraries(ws_release
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
)

add_executable(ws_restore
    ws_restore.cpp
)
target_link_libraries(ws_restore
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
)

# create copy with different name for tests
# this file should not get installed!
add_custom_command(
 TARGET ws_restore
 POST_BUILD
 COMMAND cp ${CMAKE_BINARY_DIR}/bin/ws_restore ${CMAKE_BINARY_DIR}/bin/ws_restore_notest
 COMMENT "cp ${CMAKE_BINARY_DIR}/ws_restore ${CMAKE_BINARY_DIR}/ws_restore_notest"
)

add_executable(ws_prepare
    ws_prepare.cpp
)
target_link_libraries(ws_prepare
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
)

add_executable(ws_register
    ws_register.cpp
)
target_link_libraries(ws_register
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
)

add_executable(ws_expirer
    ws_expirer.cpp
)
target_link_libraries(ws_expirer
    PRIVATE
        ws_common
        mail
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
)

add_executable(ws_stat
    ws_stat.cpp
)
if(NOT BUILD_WITH_VCPKG)
    target_include_directories(ws_stat PRIVATE ${thread_pool_SOURCE_DIR}/include)
else()
    find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIR NAMES BS_thread_pool.hpp)
    if(BSHOSHANY_THREAD_POOL_INCLUDE_DIR)
        target_include_directories(ws_stat PRIVATE ${BSHOSHANY_THREAD_POOL_INCLUDE_DIR})
    endif()
endif()
target_link_libraries(ws_stat
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
        Threads::Threads
)

add_executable(ws_editdb
    ws_editdb.cpp
)
if(NOT BUILD_WITH_VCPKG)
    target_include_directories(ws_editdb PRIVATE ${thread_pool_SOURCE_DIR}/include)
else()
    find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIR NAMES BS_thread_pool.hpp)
    if(BSHOSHANY_THREAD_POOL_INCLUDE_DIR)
        target_include_directories(ws_editdb PRIVATE ${BSHOSHANY_THREAD_POOL_INCLUDE_DIR})
    endif()
endif()
target_link_libraries(ws_editdb
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
        Threads::Threads
)

add_executable(ws_send_ical
    ws_send_ical.cpp
)
target_link_libraries(ws_send_ical
    PRIVATE
        ws_common
        mail
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
)

add_executable(ws_validate_config
    ws_validate_config.cpp
)
target_link_libraries(ws_validate_config
    PRIVATE
        ws_common
        ${Boost_LIBRARIES}
        fmt::fmt
        spdlog::spdlog
)

# Installation rules for user commands (BINDIR)
install(TARGETS
    ws_list
    ws_find
    ws_allocate
    ws_release
    ws_restore
    ws_register
    ws_send_ical
    ws_stat
    ws_validate_config
    COMPONENT Runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Installation rules for admin commands (SBINDIR)
install(TARGETS
    ws_expirer
    ws_editdb
    ws_prepare
    COMPONENT Runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})

# Post-install privilege setup
set(WS_INSTALL_SET_PRIVILEGES ON CACHE BOOL "Set capabilities or setuid on privileged executables during install")

if(WS_INSTALL_SET_PRIVILEGES)
    if(LIBCAP)
        install(SCRIPT "${CMAKE_SOURCE_DIR}/src/setcap_install.cmake")
    else()
        set(_WS_CHOWN_OWNER "${WS_SETUID_UID}")
        if(_WS_CHOWN_OWNER STREQUAL "")
            set(_WS_CHOWN_OWNER "root")
        endif()

        set(_WS_CHOWN_GROUP "${WS_SETUID_GID}")
        if(_WS_CHOWN_GROUP STREQUAL "")
            set(_WS_CHOWN_GROUP "root")
        endif()

        install(SCRIPT "${CMAKE_SOURCE_DIR}/src/setuid_install.cmake")
    endif()
endif()
