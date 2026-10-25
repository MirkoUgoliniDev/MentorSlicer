set(_srcdir ${CMAKE_CURRENT_LIST_DIR}/cr_tpms)

if (MSVC)
    set(_output  ${DESTDIR}/include/cr_tpms_library.h
                 ${DESTDIR}/lib/cr_tpms_library.lib
                 ${DESTDIR}/bin/cr_tpms_library.dll)

    add_custom_command(
        OUTPUT  ${_output}
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/cr_tpms_library.h ${DESTDIR}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/cr_tpms_library.lib ${DESTDIR}/lib/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/cr_tpms_library.dll ${DESTDIR}/bin/
    )

    add_custom_target(dep_CR_TPMS SOURCES ${_output})

elseif(APPLE)
    set(_output  ${DESTDIR}/include/cr_tpms_library.h
    ${DESTDIR}/lib/libcr_tpms_library.a)

    add_custom_command(
    OUTPUT  ${_output}
    COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/cr_tpms_library.h ${DESTDIR}/include/
    COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/mac/${CMAKE_SYSTEM_PROCESSOR}/libcr_tpms_library.a ${DESTDIR}/lib/
    )
    add_custom_target(dep_CR_TPMS SOURCES ${_output})
    message(STATUS "CR_TPMS Mac _output: ${_srcdir}/lib/mac/${CMAKE_SYSTEM_PROCESSOR}/libcr_tpms_library.a")
    message(STATUS "${DESTDIR}/lib/libcr_tpms_library.a")
elseif(UNIX)
    set(_output  ${DESTDIR}/include/cr_tpms_library.h
    ${DESTDIR}/lib/libcr_tpms_library.a)

    add_custom_command(
    OUTPUT  ${_output}
    COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/cr_tpms_library.h ${DESTDIR}/include/
    COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/linux/${CMAKE_SYSTEM_PROCESSOR}/libcr_tpms_library.a ${DESTDIR}/lib/
    )

    add_custom_target(dep_CR_TPMS SOURCES ${_output})
    message(STATUS "CR_TPMS Linux _output: ${CMAKE_SYSTEM_PROCESSOR}")
endif ()

