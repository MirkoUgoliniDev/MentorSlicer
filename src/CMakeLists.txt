cmake_minimum_required(VERSION 3.13)
# project(CrealityPrint-native)

add_subdirectory(build-utils)
add_subdirectory(admesh)
# add_subdirectory(avrdude)
# boost/nowide
add_subdirectory(boost)
add_subdirectory(clipper)
add_subdirectory(clipper2)
add_subdirectory(miniz)
add_subdirectory(glu-libtess)
add_subdirectory(qhull)
add_subdirectory(Shiny)
add_subdirectory(semver)
add_subdirectory(libigl)
add_subdirectory(hints)
add_subdirectory(mcut)
add_subdirectory(qoi)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ENABLE_WEBRTC_VIDEO ON)
else()
    set(ENABLE_WEBRTC_VIDEO OFF)
endif()
add_subdirectory(video)
# Adding libnest2d project for bin packing...
add_subdirectory(libnest2d)

# Diagnostics are exposed through one configuration-gated interface. Release
# builds receive neither fingerprint nor performance instrumentation.
add_library(slic3r_diagnostics_relwithdebinfo INTERFACE)
target_compile_definitions(slic3r_diagnostics_relwithdebinfo INTERFACE
    $<$<CONFIG:RelWithDebInfo>:SLIC3R_DIAGNOSTICS_FINGERPRINT_ENABLED>)

# Tracy remains an implementation detail of performance diagnostics. Compiling
# its client directly into the consumer avoids an unconditional Visual Studio
# project dependency.
target_include_directories(slic3r_diagnostics_relwithdebinfo SYSTEM INTERFACE
    $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_SOURCE_DIR}/tracy/public>)
target_compile_definitions(slic3r_diagnostics_relwithdebinfo INTERFACE
    $<$<CONFIG:RelWithDebInfo>:SLIC3R_DIAGNOSTICS_PERFORMANCE_ENABLED>
    $<$<CONFIG:RelWithDebInfo>:TRACY_ENABLE>
    $<$<CONFIG:RelWithDebInfo>:TRACY_ON_DEMAND>
    $<$<CONFIG:RelWithDebInfo>:TRACY_ONLY_LOCALHOST>)

#_VLD_ENABLED 
set(CGAL_ENABLE_VLD 0)
if(CMAKE_VLD_ENABLED)
    if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        message(STATUS "Enabling Visual Leak Detector (VLD) for Debug build")
        set(CGAL_ENABLE_VLD 1)
    endif()
endif()
#
message(STATUS "Before CGAL_ENABLE_VLD=${CGAL_ENABLE_VLD}")
message("bbbbSLIC3R_APP_NAME=${SLIC3R_APP_NAME}")
find_package(libnoise REQUIRED)
add_subdirectory(libslic3r)

if (SLIC3R_GUI)
    add_subdirectory(glad)
    add_subdirectory(imgui)
    add_subdirectory(imguizmo)
    add_subdirectory(hidapi)
    include_directories(hidapi/include)

    if(WIN32)
        message(STATUS "WXWIN environment set to: $ENV{WXWIN}")
    elseif(UNIX)
        set(wxWidgets_USE_UNICODE ON)
        if(SLIC3R_STATIC)
            set(wxWidgets_USE_STATIC ON)
        else()
            set(wxWidgets_USE_STATIC OFF)
        endif()
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set (wxWidgets_CONFIG_OPTIONS "--toolkit=gtk${SLIC3R_GTK}")
        find_package(wxWidgets 3.3 REQUIRED COMPONENTS base core adv html gl aui net webview)
    else ()
        find_package(wxWidgets 3.3 CONFIG REQUIRED COMPONENTS html adv gl core base webview aui net)
    endif ()

    if(UNIX)
        message(STATUS "wx-config path: ${wxWidgets_CONFIG_EXECUTABLE}")
    endif()

    foreach(_wx_use_file IN LISTS wxWidgets_USE_FILE)
        include("${_wx_use_file}")
    endforeach()

    find_package(JPEG QUIET)
    if (APPLE)
        set(_slic3r_saved_find_library_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
        find_library(SLIC3R_TIFF_LIBRARY
            NAMES tiff libtiff
            PATHS ${CMAKE_PREFIX_PATH}
            PATH_SUFFIXES lib
            NO_DEFAULT_PATH
        )
        set(CMAKE_FIND_LIBRARY_SUFFIXES ${_slic3r_saved_find_library_suffixes})
        if (SLIC3R_TIFF_LIBRARY)
            set(TIFF_FOUND TRUE)
            set(TIFF_LIBRARIES ${SLIC3R_TIFF_LIBRARY})
        else ()
            set(TIFF_FOUND FALSE)
            unset(TIFF_LIBRARIES)
        endif ()
    else ()
        find_package(TIFF QUIET)
    endif ()

    string(REGEX MATCH "wxpng" WX_PNG_BUILTIN ${wxWidgets_LIBRARIES})
    if (PNG_FOUND AND NOT WX_PNG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX png)
        list(APPEND wxWidgets_LIBRARIES ${PNG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxtiff" WX_TIFF_BUILTIN ${wxWidgets_LIBRARIES})
    if (TIFF_FOUND AND NOT WX_TIFF_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX tiff)
        list(APPEND wxWidgets_LIBRARIES ${TIFF_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxjpeg" WX_JPEG_BUILTIN ${wxWidgets_LIBRARIES})
    if (JPEG_FOUND AND NOT WX_JPEG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX jpeg)
        list(APPEND wxWidgets_LIBRARIES ${JPEG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxexpat" WX_EXPAT_BUILTIN ${wxWidgets_LIBRARIES})
    if (EXPAT_FOUND AND NOT WX_EXPAT_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX expat)
        list(APPEND wxWidgets_LIBRARIES ${EXPAT_LIBRARIES})
    endif ()
    
    # This is an issue in the new wxWidgets cmake build, doesn't deal with librt
    find_library(LIBRT rt)
    if(LIBRT)
        list(APPEND wxWidgets_LIBRARIES ${LIBRT})
    endif()
    # This fixes a OpenGL linking issue on OSX. wxWidgets cmake build includes
    # wrong libs for opengl in the link line and it does not link to it by himself.
    # libslic3r_gui will link to opengl anyway, so lets override wx
    list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX OpenGL)

#    list(REMOVE_ITEM wxWidgets_LIBRARIES oleacc)
    message(STATUS "wx libs: ${wxWidgets_LIBRARIES}")

    add_subdirectory(slic3r)
endif()

# Create a slic3r executable
# Process mainfests for various platforms.
if (NOT ${CUSTOM_TYPE} STREQUAL "")
    __build_rc_config()
    __build_manifest_config()
else()
    #customized module_info.cmake
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/CrealityPrint.rc.in ${CMAKE_CURRENT_BINARY_DIR}/CrealityPrint.rc @ONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/CrealityPrint.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/CrealityPrint.manifest @ONLY)
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/osx/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
message("PROJECT_DLL_NAME_WIN32=${PROJECT_DLL_NAME_WIN32}")
set(PROJECT_DLL ${PROJECT_DLL_NAME_WIN32})
if(WIN32)
set(PROJECT_DLL ${PROJECT_DLL_NAME_WIN32})
elseif(APPLE)
set(PROJECT_DLL ${PROCESS_NAME})
endif()

add_subdirectory(slic3r/CLI)

if (WIN32)
    add_library(${PROJECT_DLL} SHARED CrealityPrint.cpp)
elseif (APPLE AND CMAKE_MACOSX_BUNDLE)
    add_executable(${PROJECT_DLL} MACOSX_BUNDLE CrealityPrint.cpp)
else ()
    add_executable(${PROJECT_DLL} CrealityPrint.cpp)
endif ()

set(slic3r_tracy_client_source
    ${CMAKE_CURRENT_SOURCE_DIR}/tracy/public/TracyClient.cpp)
target_sources(${PROJECT_DLL} PRIVATE
    $<$<CONFIG:RelWithDebInfo>:${slic3r_tracy_client_source}>)
set_source_files_properties(${slic3r_tracy_client_source}
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_link_libraries(${PROJECT_DLL} slic3r_diagnostics_relwithdebinfo)

target_include_directories(${PROJECT_DLL} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_DLL} libslic3r_cli)

if (MINGW)
    target_link_options(${PROJECT_DLL} PUBLIC "-Wl,-allow-multiple-definition")
    set_target_properties(${PROJECT_DLL} PROPERTIES PREFIX "")
endif (MINGW)

if (NOT WIN32 AND NOT APPLE)
    # Binary name on unix like systems (Linux, Unix)
    set_target_properties(${PROJECT_DLL} PROPERTIES OUTPUT_NAME "${PROCESS_NAME}")
    if (FLATPAK)
        target_link_directories(${PROJECT_DLL} PUBLIC /app/lib64/ /app/lib/)
    endif()
    set(SLIC3R_APP_CMD "${PROCESS_NAME}")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/unix/BuildLinuxImage.sh.in ${CMAKE_CURRENT_BINARY_DIR}/BuildLinuxImage.sh @ONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/unix/BuildLinuxDeb.sh.in ${CMAKE_CURRENT_BINARY_DIR}/BuildLinuxDeb.sh @ONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/unix/BuildUosV25Deb.sh.in ${CMAKE_CURRENT_BINARY_DIR}/BuildUosV25Deb.sh @ONLY)
endif ()

target_link_libraries(${PROJECT_DLL} libslic3r cereal::cereal)


if(CGAL_ENABLE_VLD AND VLD_LIBRARY)
    message("------vld found, ${VLD_LIBRARY}------")   
    target_link_libraries(${PROJECT_DLL} ${VLD_LIBRARY})
    # Ensure VLD is enabled for the build.
    target_compile_definitions(${PROJECT_DLL} PRIVATE -DCGAL_ENABLE_VLD)
    message(STATUS "Visual Leak Detector (VLD) enabled for this build.")
else()
    message(STATUS "Visual Leak Detector (VLD) not found, proceeding without it.")
endif()
if(WIN32)
  if(MSVC_TOOLSET_VERSION)
    SET(MSVC_PREFIX "vc${MSVC_TOOLSET_VERSION}")
    SET(ASSIMP_MSVC_VERSION ${MSVC_PREFIX})
  else()
    if(MSVC12)
      SET(ASSIMP_MSVC_VERSION "vc120")
    elseif(MSVC14)
      SET(ASSIMP_MSVC_VERSION "vc140")
    elseif(MSVC15)
      SET(ASSIMP_MSVC_VERSION "vc141")
    elseif(MSV142)
      SET(ASSIMP_MSVC_VERSION "vc142")
    elseif(MSVC143)
      SET(ASSIMP_MSVC_VERSION "vc143")
    endif()
  endif()
  
  # ʹ�� debug/optimized �ؼ���֧�ֶ����������� (�� Visual Studio)
  find_library(LIB_ASSIMP_DEBUG assimp-${ASSIMP_MSVC_VERSION}-mtd)
  if(NOT LIB_ASSIMP_DEBUG)
    find_library(LIB_ASSIMP_DEBUG assimp-${ASSIMP_MSVC_VERSION}-mt)
  endif()
  find_library(LIB_ASSIMP_RELEASE assimp-${ASSIMP_MSVC_VERSION}-mt)
  target_link_libraries(${PROJECT_DLL} libslic3r cereal::cereal 
    $<$<CONFIG:Debug>:${LIB_ASSIMP_DEBUG}>
    $<$<NOT:$<CONFIG:Debug>>:${LIB_ASSIMP_RELEASE}>)
else()
  find_library(LIB_ASSIMP assimp)
  target_link_libraries(${PROJECT_DLL} libslic3r cereal::cereal ${LIB_ASSIMP})
endif()

find_library(CP_TPMS_Gradual cr_tpms_library)
if (CP_TPMS_Gradual)
    target_link_libraries(${PROJECT_DLL} ${CP_TPMS_Gradual})
else()
    set(_msvc_arch "${CMAKE_SYSTEM_PROCESSOR}")
    if (MSVC AND CMAKE_GENERATOR_PLATFORM)
        string(TOUPPER "${CMAKE_GENERATOR_PLATFORM}" _msvc_arch)
    endif()
    if ("${_msvc_arch}" STREQUAL "ARM64")
        message(FATAL_ERROR "Missing Windows ARM64 CR_TPMS library: cr_tpms_library.lib")
    else()
        message(FATAL_ERROR "Missing CR_TPMS library: cr_tpms_library")
    endif()
endif()

# cr_km_recipe: closed-source K-M recipe library.
# The library and header are installed under <prefix>/{lib,include}.
find_library(CP_KM_Recipe cr_km_recipe)
if(NOT CP_KM_Recipe)
    message(FATAL_ERROR "Missing CR_KM_RECIPE library. Build and install dep_CR_KM_RECIPE first.")
endif()
get_filename_component(_cr_km_recipe_libdir "${CP_KM_Recipe}" DIRECTORY)
get_filename_component(_cr_km_recipe_root "${_cr_km_recipe_libdir}" DIRECTORY)
target_include_directories(${PROJECT_DLL} PRIVATE "${_cr_km_recipe_root}/include")
target_compile_definitions(${PROJECT_DLL} PRIVATE CR_CLOSE_STATIC_DEFINE)
target_link_libraries(${PROJECT_DLL} ${CP_KM_Recipe})

find_library(CP_FILLTENSOR_Gradual cr_FillTensor_library)
if (CP_FILLTENSOR_Gradual)
    target_compile_definitions(libslic3r PUBLIC CR_FILLTENSOR_STATIC_DEFINE)
    target_link_libraries(libslic3r ${CP_FILLTENSOR_Gradual})
else()
    set(_msvc_arch "${CMAKE_SYSTEM_PROCESSOR}")
    if (MSVC AND CMAKE_GENERATOR_PLATFORM)
        string(TOUPPER "${CMAKE_GENERATOR_PLATFORM}" _msvc_arch)
    endif()
    if ("${_msvc_arch}" STREQUAL "ARM64")
        message(FATAL_ERROR "Missing Windows ARM64 CR_FILLTENSOR library: cr_FillTensor_library.lib")
    else()
        message(FATAL_ERROR "Missing CR_FILLTENSOR library: cr_FillTensor_library")
    endif()
endif()
if (APPLE)
#    add_compile_options(-stdlib=libc++)
#    add_definitions(-DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE)
    # -liconv: boost links to libiconv by default
    target_link_libraries(${PROJECT_DLL} "-liconv -framework IOKit" "-framework CoreFoundation" "-framework AVFoundation" "-framework AVKit" "-framework CoreMedia" "-framework VideoToolbox" -lc++)
elseif (MSVC)
    # Manifest is provided through CrealityPrint.rc, don't generate your own.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
else ()
    # Boost on Raspberry-Pi does not link to pthreads explicitely.
    target_link_libraries(${PROJECT_DLL} ${CMAKE_DL_LIBS} -lstdc++ Threads::Threads pangoft2-1.0  atomic PNG::PNG ZLIB::ZLIB)
endif ()

# Add the Slic3r GUI library, libcurl, OpenGL and GLU libraries.
if (SLIC3R_GUI)
#    target_link_libraries(CrealityPrint ws2_32 uxtheme setupapi libslic3r_gui ${wxWidgets_LIBRARIES})
target_link_libraries(${PROJECT_DLL} libslic3r_gui glad)
    if (MSVC)
        # Generate debug symbols even in release mode.
        target_link_options(${PROJECT_DLL} PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
        target_link_libraries(${PROJECT_DLL} user32.lib Setupapi.lib)
    elseif (MINGW)
        target_link_libraries(${PROJECT_DLL} ws2_32 uxtheme setupapi)
    elseif (APPLE)
        target_link_libraries(${PROJECT_DLL} "-framework OpenGL")
    else ()
        target_link_libraries(${PROJECT_DLL} -ldl)
    endif ()
    #if (WIN32)
    #    find_library(PSAPI_LIB NAMES Psapi)
    #    target_link_libraries(CrealityPrint ${PSAPI_LIB})
    #endif ()
endif ()

# On Windows, a shim application is required to produce a console / non console version of the Slic3r application.
# Also the shim may load the Mesa software OpenGL renderer if the default renderer does not support OpenGL 2.0 and higher.



if (WIN32)
    if (MINGW)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -municode")
    endif()

    add_executable(${STARTUP_PROJECT_NAME} WIN32 CrealityPrint_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/CrealityPrint.rc)
    # Generate debug symbols even in release mode.
    if(MSVC)
        target_link_options(${STARTUP_PROJECT_NAME} PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif()
    target_compile_definitions(${STARTUP_PROJECT_NAME} PRIVATE -DSLIC3R_WRAPPER_NOCONSOLE)
    # buildinfo.h is generated in the build tree.
    target_include_directories(${STARTUP_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/libslic3r)
    add_dependencies(${STARTUP_PROJECT_NAME} ${PROJECT_DLL})
    set_target_properties(${STARTUP_PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROCESS_NAME}")
    # Mesa software renderer (dep_Mesa): opengl32.dll loads libgallium_wgl.dll from the same folder.
    set(_mesa_dlls "${CMAKE_PREFIX_PATH}/bin/mesa/opengl32.dll" "${CMAKE_PREFIX_PATH}/bin/mesa/libgallium_wgl.dll")
    if (EXISTS "${CMAKE_PREFIX_PATH}/bin/mesa/opengl32.dll")
        set(_mesa_found ON)
        add_custom_command(TARGET ${STARTUP_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${STARTUP_PROJECT_NAME}>/mesa"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_dlls}
                "$<TARGET_FILE_DIR:${STARTUP_PROJECT_NAME}>/mesa"
            COMMENT "Copying Mesa software renderer"
        )
    endif ()
#    target_link_libraries(${STARTUP_PROJECT_NAME} PRIVATE boost_headeronly)
endif ()


# Link the resources dir to where Slic3r GUI expects it
set(output_dlls_Release "")
set(output_dlls_Debug "")
set(output_dlls_RelWithDebInfo "")

if (WIN32)
    # This has to be a separate target due to the windows command line lenght limits
    add_custom_target(COPY_DLLS ALL DEPENDS ${PROJECT_DLL})

    if (CMAKE_CONFIGURATION_TYPES)
        foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}" WIN_CONF_OUTPUT_DIR)
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/resources" WIN_RESOURCES_SYMLINK)
            message(STATUS "WIN_RESOURCES_SYMLINK=${WIN_RESOURCES_SYMLINK}")
            message(STATUS "SLIC3R_RESOURCES_DIR_WIN=${SLIC3R_RESOURCES_DIR_WIN}")
            add_custom_command(TARGET ${PROJECT_DLL} POST_BUILD
                COMMAND if exist "${WIN_CONF_OUTPUT_DIR}" "("
                        if not exist "${WIN_RESOURCES_SYMLINK}" "("
                            mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}"
                        ")"
                    ")"
                COMMENT "Symlinking the resources directory into the build tree"
                VERBATIM
            )
            
            # add_custom_command(TARGET ${PROJECT_DLL} PRE_BUILD
            #     COMMAND ${CMAKE_COMMAND} -E make_directory ${WIN_RESOURCES_SYMLINK}
    		# 	COMMAND ${CMAKE_COMMAND} -E copy_directory
    		# 	${CMAKE_SOURCE_DIR}/resources/
    		# 	"$<TARGET_FILE_DIR:${PROJECT_DLL}>/resources/"
            #     COMMENT "TARGET_FILE_DIR :$<TARGET_FILE_DIR:${PROJECT_DLL}>/resources/"
    		# 	)
        endforeach ()

        # For multi-config generators (Visual Studio), copy DLLs for all configurations
        # For single-config generators, CMAKE_CONFIGURATION_TYPES is empty, use CMAKE_BUILD_TYPE instead
        if(CMAKE_CONFIGURATION_TYPES)
            # Multi-config generator (Visual Studio, Ninja Multi-Config, etc.)
            foreach(CONF ${CMAKE_CONFIGURATION_TYPES})
                if ("${CONF}" STREQUAL "Debug")
                    crealityprint_copy_dlls(COPY_DLLS "Debug" "d" output_dlls_Debug)
                    if(CGAL_ENABLE_VLD AND VLD_LIBRARY)
                        copy_vld_dlls(COPY_DLLS "Debug")
                    endif()
                elseif("${CONF}" STREQUAL "RelWithDebInfo")
                    crealityprint_copy_dlls(COPY_DLLS "RelWithDebInfo" "" output_dlls_RelWithDebInfo)
                elseif("${CONF}" STREQUAL "Release")
                    crealityprint_copy_dlls(COPY_DLLS "Release" "" output_dlls_Release)
                endif()
            endforeach()
        else()
            # Single-config generator (Makefile, Ninja, etc.)
            if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
                crealityprint_copy_dlls(COPY_DLLS "Debug" "d" output_dlls_Debug)
                if(CGAL_ENABLE_VLD AND VLD_LIBRARY)
                    copy_vld_dlls(COPY_DLLS "Debug")
                endif()
            elseif("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
                crealityprint_copy_dlls(COPY_DLLS "RelWithDebInfo" "" output_dlls_RelWithDebInfo)
            else()
                crealityprint_copy_dlls(COPY_DLLS "Release" "" output_dlls_Release)
            endif()
        endif()
    else ()
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/resources" WIN_RESOURCES_SYMLINK)
        message(STATUS "SLIC3R_RESOURCES_DIR_WIN222=${SLIC3R_RESOURCES_DIR_WIN}")

        
        add_custom_command(TARGET ${PROJECT_DLL} POST_BUILD
            COMMAND if not exist "${WIN_RESOURCES_SYMLINK}" "(" mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}" ")"
            COMMENT "Symlinking the resources directory into the build tree"
            VERBATIM
        )
    endif ()
else ()
    if (APPLE AND NOT CMAKE_MACOSX_BUNDLE)
        # On OSX, the name of the binary matches the name of the Application.
        add_custom_command(TARGET ${PROJECT_DLL} POST_BUILD
            COMMAND ln -sf ${PROJECT_DLL} CrealityPrint-slicer
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:CrealityPrint>"
            VERBATIM)
    else ()
        add_custom_command(TARGET ${PROJECT_DLL} POST_BUILD
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_DLL}>"
            VERBATIM)
    endif ()
    if (XCODE)
        # Because of Debug/Release/etc. configurations (similar to MSVC) the slic3r binary is located in an extra level
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")
        set(BIN_CONF_DIR "Debug")
    else ()
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/../resources")
    endif ()
    if (CMAKE_MACOSX_BUNDLE)
        if (CMAKE_CONFIGURATION_TYPES)
            set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/CrealityPrint.app/Contents/Resources")
        else()
            set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/CrealityPrint.app/Contents/Resources")
        endif()
        set(MACOSX_BUNDLE_ICON_FILE Icon.icns)
        set(MACOSX_BUNDLE_NAME "CrealityPrint")
        set(MACOSX_BUNDLE_DISPLAY_NAME "CrealityPrint")
        set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${CREALITYPRINT_VERSION})
        set(MACOSX_BUNDLE_BUNDLE_VERSION ${CREALITYPRINT_BUILD_NUMBER})
        set(MACOSX_BUNDLE_COPYRIGHT "Copyright(C) 2022-2024 Creality All Rights Reserved")
        
    endif()

    message("SLIC3R_RESOURCES_DIR_Mac=${SLIC3R_RESOURCES_DIR}")
    message("BIN_RESOURCES_DIR=${BIN_RESOURCES_DIR}")
    if(APPLE)
        add_custom_command(TARGET ${PROJECT_DLL} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${PROJECT_DLL}>/../Resources"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_DLL}>/../Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SLIC3R_RESOURCES_DIR}" "$<TARGET_FILE_DIR:${PROJECT_DLL}>/../Resources"
            COMMENT "Copying resources to .app bundle"
            )
    else()
        add_custom_command(TARGET ${PROJECT_DLL} POST_BUILD
        COMMAND ln -sfn "${SLIC3R_RESOURCES_DIR}" "${BIN_RESOURCES_DIR}"
        COMMENT "Symlinking the resources directory into the build tree"
        VERBATIM)
    endif()
endif ()

# Slic3r binary install target. Default build type is release in case no CMAKE_BUILD_TYPE is provided.
if( ("${CMAKE_BUILD_TYPE}" STREQUAL "Release") OR ("${CMAKE_BUILD_TYPE}" STREQUAL "") )
    set (build_type "Release")
    set(CMAKE_BUILD_POSTFIX "")
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
    set (build_type "RelWithDebInfo")
    set(CMAKE_BUILD_POSTFIX "")
else()
    set (build_type "Debug")
    set(CMAKE_BUILD_POSTFIX "d")
endif()
message(STATUS "libslic3r-CMAKE_BUILD_TYPE: ${build_type}")
message(STATUS "CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}")
if (WIN32)
    install(TARGETS ${PROJECT_DLL} RUNTIME DESTINATION ".")
    if (MSVC)
        install(TARGETS ${STARTUP_PROJECT_NAME} RUNTIME DESTINATION ".")
    endif ()
    install(FILES ${output_dlls_${build_type}} DESTINATION ".")
    # Print Advisor MCP sidecar packaged by tools/build-mcp-exe.bat (missing when Node.js was not found).
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/${build_type}/mentorslicer-mcp.exe" DESTINATION "." OPTIONAL)
    # Mesa software-renderer: installed to a mesa/ subdirectory because the app
    # loads it from <exe_dir>/mesa/opengl32.dll when host OpenGL is unavailable.
    if (_mesa_found)
        install(FILES ${_mesa_dlls} DESTINATION "mesa")
    endif ()
else ()
    set(CMAKE_INSTALL_BINDIR "./")
    message(STATUS "CMAKE_INSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}")
    install(TARGETS ${PROJECT_DLL} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()
