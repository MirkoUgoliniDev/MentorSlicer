
include(ProcessorCount)
ProcessorCount(NPROC)

if(DEFINED OPENSSL_ARCH)
    set(_cross_arch ${OPENSSL_ARCH})
else()
    if(WIN32)
        set(_cross_arch "VC-WIN64A")
    elseif(APPLE)
        set(_cross_arch "darwin64-arm64-cc")
	endif()
endif()

if(WIN32)
    if(EXISTS "C:/Strawberry/perl/bin/perl.exe")
        set(_openssl_perl "C:/Strawberry/perl/bin/perl.exe")
    else()
        find_program(_openssl_perl perl)
    endif()
    if(NOT _openssl_perl)
        message(FATAL_ERROR "Perl is required to build OpenSSL on Windows. Install Strawberry Perl or add perl.exe to PATH.")
    endif()
    if(_openssl_perl MATCHES "[/\\\\]Git[/\\\\]usr[/\\\\]bin[/\\\\]perl(\\.exe)?$")
        message(FATAL_ERROR "OpenSSL requires a native Windows Perl. Found Git/MSYS Perl at ${_openssl_perl}; install Strawberry Perl or put C:/Strawberry/perl/bin before Git usr/bin.")
    endif()
    message(STATUS "OpenSSL Perl: ${_openssl_perl}")
    set(_conf_cmd "${_openssl_perl}" Configure )
    set(_cross_comp_prefix_line "")
    set(_make_cmd nmake)
    set(_install_cmd nmake install_sw )
else()
    if(APPLE)
        set(_conf_cmd export MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} && ./Configure -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
    else()
        set(_conf_cmd "./config")
    endif()
    set(_cross_comp_prefix_line "")
    set(_make_cmd make -j${NPROC})
    set(_install_cmd make -j${NPROC} install_sw)
    if (CMAKE_CROSSCOMPILING)
        set(_cross_comp_prefix_line "--cross-compile-prefix=${TOOLCHAIN_PREFIX}-")

        if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
            set(_cross_arch "linux-aarch64")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armhf") # For raspbian
            # TODO: verify
            set(_cross_arch "linux-armv4")
        endif ()
    endif ()
endif()

ExternalProject_Add(dep_OpenSSL
    #EXCLUDE_FROM_ALL ON
    # OpenSSL 3.5 is the LTS line (supported until April 2030).
    URL "https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz"
    URL_HASH SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/OpenSSL
	CONFIGURE_COMMAND ${_conf_cmd} ${_cross_arch}
        "--openssldir=${DESTDIR}"
        "--prefix=${DESTDIR}"
        "--libdir=lib"
        ${_cross_comp_prefix_line}
        no-shared
        no-asm
        no-ssl3-method
        no-dynamic-engine
        no-tests
    BUILD_IN_SOURCE ON
    BUILD_COMMAND ${_make_cmd}
    INSTALL_COMMAND ${_install_cmd}
)

ExternalProject_Add_Step(dep_OpenSSL install_cmake_files
    DEPENDEES install

    COMMAND ${CMAKE_COMMAND} -E copy_directory openssl "${DESTDIR}${CMAKE_INSTALL_LIBDIR}/cmake/openssl"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)
