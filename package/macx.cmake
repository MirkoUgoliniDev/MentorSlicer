set(App_Name CrealityPrint.app)
set(BUNDLE_NAME CrealityPrint)
set(App_Volumn_Name "Creality Print App")
set(App_Scpt CMakeDMGSetup.scpt)
SET(CPACK_GENERATOR "DragNDrop")
set(CPACK_SYSTEM_NAME "MacOS") 
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version" FORCE)
set(OSX_CODESIGN_IDENTITY "Developer ID Application" CACHE STRING "Identity to use for code signing")
set(CPACK_PACKAGE_FILE_NAME "${PROCESS_NAME}-${CREALITYPRINT_VERSION}-macx-${CMAKE_OSX_ARCHITECTURES}-${PROJECT_VERSION_EXTRA}")
message(STATUS "CMAKE_OSX_DEPLOYMENT_TARGET -> 10.15")

message(STATUS "CPACK_PACKAGING_INSTALL_PREFIX=${CPACK_PACKAGING_INSTALL_PREFIX}")

message(STATUS "CPACK_OUTPUT_FILE_PREFIX=${CPACK_OUTPUT_FILE_PREFIX}")
message(STATUS "CMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}    BUNDLE_NAME=${BUNDLE_NAME}  App_Name=${App_Name} MACPASS=$ENV{MACPASS}")
# Append Qt's lib folder which is two levels above Qt5Widgets_DIR
include(InstallRequiredSystemLibraries)
set(CPACK_CODESIGN_PATH ${CMAKE_CURRENT_BINARY_DIR}/../_CPack_Packages/MacOS/DragNDrop/${CPACK_PACKAGE_FILE_NAME})

install(CODE "execute_process(COMMAND codesign --timestamp --force --options=runtime -s \"${OSX_CODESIGN_IDENTITY}\" --deep \"${CPACK_CODESIGN_PATH}/${BUNDLE_NAME}.app\"
    RESULT_VARIABLE codesign_result)
if(NOT \"\${codesign_result}\" STREQUAL \"0\")
    message(FATAL_ERROR \"macOS code signing failed: \${codesign_result}\")
endif()")
install(CODE "if(\"\$ENV{MENTORSLICER_NOTARY_PASSWORD}\" STREQUAL \"\")
    message(FATAL_ERROR \"macOS notarization needs the app-specific password in the MENTORSLICER_NOTARY_PASSWORD environment variable\")
endif()
execute_process(COMMAND bash \"${CMAKE_CURRENT_SOURCE_DIR}/macx/Notarized-script.sh\" \"${CPACK_CODESIGN_PATH}\" \"${BUNDLE_NAME}\" \"\$ENV{MENTORSLICER_NOTARY_PASSWORD}\"
    RESULT_VARIABLE notarization_result)
if(NOT \"\${notarization_result}\" STREQUAL \"0\")
    message(FATAL_ERROR \"macOS notarization failed: \${notarization_result}\")
endif()")

set(CPACK_DMG_VOLUME_NAME ${App_Volumn_Name})
set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/macx/CMakeDMGBackground.tif")
set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/macx/VolumeIcon.icns")
set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/macx/${App_Scpt}")
include(CPack)

