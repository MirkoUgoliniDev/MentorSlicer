cmake_minimum_required(VERSION 3.13)
project(libslic3r_gui)

include(PrecompiledHeader)

set(SLIC3R_GUI_SOURCES
    pchheader.cpp
    pchheader.hpp
    GUI/Widgets/Button.cpp
    GUI/Widgets/Button.hpp
    GUI/Widgets/CheckBox.cpp
    GUI/Widgets/CheckBox.hpp
    GUI/Widgets/RadioBox.hpp
    GUI/Widgets/RadioBox.cpp
    GUI/Widgets/ProgressDialog.hpp
    GUI/Widgets/ProgressDialog.cpp
    GUI/Widgets/RoundedRectangle.hpp
    GUI/Widgets/RoundedRectangle.cpp
    GUI/Widgets/ComboBox.cpp
    GUI/Widgets/ComboBox.hpp
    GUI/Widgets/SideButton.hpp
    GUI/Widgets/SideButton.cpp
    GUI/Widgets/SideMenuPopup.hpp
    GUI/Widgets/SideMenuPopup.cpp
    GUI/Widgets/DropDown.cpp
    GUI/Widgets/DropDown.hpp
    GUI/Widgets/PopupWindow.cpp
    GUI/Widgets/PopupWindow.hpp
    GUI/Widgets/Label.cpp
    GUI/Widgets/Label.hpp
    GUI/Widgets/Scrollbar.cpp
    GUI/Widgets/Scrollbar.hpp
    GUI/Widgets/ScrolledWindow.cpp
    GUI/Widgets/ScrolledWindow.hpp
    GUI/Widgets/StaticBox.cpp
    GUI/Widgets/StaticBox.hpp
    GUI/Widgets/SwitchButton.cpp
    GUI/Widgets/SwitchButton.hpp
    GUI/Widgets/SpinInput.cpp
    GUI/Widgets/SpinInput.hpp
    GUI/Widgets/StaticLine.cpp
    GUI/Widgets/StaticLine.hpp
    GUI/Widgets/StateColor.cpp
    GUI/Widgets/StateColor.hpp
    GUI/Widgets/StateHandler.cpp
    GUI/Widgets/StateHandler.hpp
    GUI/Widgets/TabCtrl.cpp
    GUI/Widgets/TabCtrl.hpp
    GUI/Widgets/TextInput.cpp
    GUI/Widgets/TextInput.hpp
    GUI/Widgets/TextInputCtrl.cpp
    GUI/Widgets/TextInputCtrl.hpp
    GUI/Widgets/Scrollbar.cpp
    GUI/Widgets/Scrollbar.hpp
    GUI/Widgets/ScrolledWindow.cpp
    GUI/Widgets/ScrolledWindow.hpp
    GUI/Widgets/ProgressBar.cpp
    GUI/Widgets/ProgressBar.hpp
    GUI/Widgets/WebView.cpp
    GUI/Widgets/WebView.hpp
    GUI/Widgets/AmsColors.hpp
    GUI/MixedFilamentDialog.cpp
    GUI/MixedFilamentDialog.hpp
    GUI/ColorDecomposeDialog.cpp
    GUI/ColorDecomposeDialog.hpp
    GUI/ColorDecomposeSupport.cpp
    GUI/ColorDecomposeSupport.hpp
    GUI/GradientCurveEditor.cpp
    GUI/GradientCurveEditor.hpp
    GUI/Widgets/TextDisplay.hpp
    GUI/Widgets/TextDisplay.cpp
    GUI/Widgets/HoverBorderIcon.hpp
    GUI/Widgets/HoverBorderIcon.cpp
    GUI/Widgets/HoverBorderBox.hpp
    GUI/Widgets/HoverBorderBox.cpp
    GUI/Widgets/NumberEntryDialog.hpp
    GUI/Widgets/NumberEntryDialog.cpp
    GUI/Widgets/ColorSlider.hpp
    GUI/Widgets/ColorSlider.cpp
    GUI/AboutDialog.cpp
    GUI/AboutDialog.hpp
    GUI/ThirdPartyLibraries.cpp
    GUI/ThirdPartyLibraries.hpp
    GUI/DailyTips.cpp
    GUI/DailyTips.hpp
    GUI/BackgroundSlicingProcess.cpp
    GUI/BackgroundSlicingProcess.hpp
    GUI/BitmapCache.cpp
    GUI/BitmapCache.hpp
    GUI/3DScene.cpp
    GUI/3DScene.hpp
    GUI/format.hpp
    GUI/GLShadersManager.hpp
    GUI/GLShadersManager.cpp
    GUI/GLShader.cpp
    GUI/GLShader.hpp
    GUI/GLCanvas3D.hpp
    GUI/GLCanvas3D.cpp
    GUI/GLEnums.hpp
    GUI/LinuxDisplayBackend.cpp
    GUI/LinuxDisplayBackend.hpp
    GUI/SceneRaycaster.hpp
    GUI/SceneRaycaster.cpp
    GUI/OpenGLManager.hpp
    GUI/OpenGLManager.cpp
    GUI/Selection.hpp
    GUI/Selection.cpp
    GUI/SlicingProgressNotification.cpp
    GUI/SlicingProgressNotification.hpp
    GUI/Gizmos/GLGizmosManager.cpp
    GUI/Gizmos/GLGizmosManager.hpp
    GUI/Gizmos/GLGizmosCommon.cpp
    GUI/Gizmos/GLGizmosCommon.hpp
    GUI/Gizmos/GLGizmoBase.cpp
    GUI/Gizmos/GLGizmoBase.hpp
    GUI/Gizmos/GLGizmoEmboss.cpp
    GUI/Gizmos/GLGizmoEmboss.hpp
    GUI/Gizmos/GLGizmoMove.cpp
    GUI/Gizmos/GLGizmoMove.hpp
    GUI/Gizmos/GLGizmoRotate.cpp
    GUI/Gizmos/GLGizmoRotate.hpp
    GUI/Gizmos/GLGizmoScale.cpp
    GUI/Gizmos/GLGizmoScale.hpp
    GUI/Gizmos/GLGizmoFdmSupports.cpp
    GUI/Gizmos/GLGizmoFdmSupports.hpp
    GUI/Gizmos/GLGizmoFlatten.cpp
    GUI/Gizmos/GLGizmoFlatten.hpp
    GUI/Gizmos/GLGizmoFuzzySkin.cpp
    GUI/Gizmos/GLGizmoFuzzySkin.hpp
    GUI/Gizmos/GLGizmoCut.cpp
    GUI/Gizmos/GLGizmoCut.hpp
    GUI/Gizmos/GLGizmoHollow.cpp
    GUI/Gizmos/GLGizmoHollow.hpp
    GUI/Gizmos/GLGizmoPainterBase.cpp
    GUI/Gizmos/GLGizmoPainterBase.hpp
    GUI/Gizmos/GLGizmoSimplify.cpp
    GUI/Gizmos/GLGizmoSimplify.hpp
    GUI/Gizmos/GLGizmoSVG.cpp
    GUI/Gizmos/GLGizmoSVG.hpp
    GUI/Gizmos/GLGizmoMmuSegmentation.cpp
    GUI/Gizmos/GLGizmoMmuSegmentation.hpp
    GUI/Gizmos/GLGizmoClone.hpp
    GUI/Gizmos/GLGizmoClone.cpp
	GUI/Gizmos/GLGizmoMeasure.cpp
    GUI/Gizmos/GLGizmoMeasure.hpp
    GUI/Gizmos/GLGizmoBrimEars.cpp
    GUI/Gizmos/GLGizmoBrimEars.hpp
    GUI/Gizmos/GLGizmoSeam.cpp
    GUI/Gizmos/GLGizmoSeam.hpp
    GUI/Gizmos/GLGizmoMeshBoolean.cpp
    GUI/Gizmos/GLGizmoMeshBoolean.hpp
    GUI/UIHelpers/MeshBooleanUI.cpp
    GUI/UIHelpers/MeshBooleanUI.hpp
    GUI/UIHelpers/TextEllipsis.cpp
    GUI/UIHelpers/TextEllipsis.hpp
    GUI/GLSelectionRectangle.cpp
    GUI/GLSelectionRectangle.hpp
    GUI/Gizmos/GizmoObjectManipulation.cpp
    GUI/Gizmos/GizmoObjectManipulation.hpp
    GUI/Gizmos/GLGizmoDrill.cpp
    GUI/Gizmos/GLGizmoDrill.hpp
    GUI/GLModel.hpp
    GUI/GLModel.cpp
    GUI/MeshHollow.hpp
    GUI/MeshHollow.cpp
    GUI/GLTexture.hpp
    GUI/GLTexture.cpp
    GUI/GLToolbar.hpp
    GUI/GLToolbar.cpp
    GUI/IMToolbar.hpp
    GUI/IMToolbar.cpp
    GUI/HybridIndexBuffer.hpp
    GUI/HybridIndexBuffer.cpp
    GUI/DiskVectorImp.hpp
    GUI/DiskVectorImp.cpp
    GUI/HybridVertexBuffer.hpp
    GUI/HybridVertexBuffer.cpp
    GUI/GCodeViewer.hpp
    GUI/GCodeViewer.cpp
    GUI/Preferences.cpp
    GUI/Preferences.hpp
    GUI/ConfigRelateDialog.cpp
    GUI/ConfigRelateDialog.hpp
    GUI/PresetHints.cpp
    GUI/PresetHints.hpp
    GUI/GUI.cpp
    GUI/GUI.hpp
    GUI/GUI_Init.cpp
    GUI/GUI_Init.hpp
    GUI/GUI_Preview.cpp
    GUI/GUI_Preview.hpp
    GUI/GUI_App.cpp
    GUI/BaseColorSpectraCache.cpp
    GUI/BaseColorSpectraCache.hpp
    GUI/SupportMeshCreator.hpp
    GUI/SupportMeshCreator.cpp
    GUI/GUI_App.hpp
    GUI/LegacyTransitiveIncludes.hpp
    GUI/GUI_Utils.cpp
    GUI/GUI_Utils.hpp
    GUI/GUI_Geometry.cpp
    GUI/GUI_Geometry.hpp
    GUI/I18N.cpp
    GUI/I18N.hpp
    GUI/IconManager.cpp
    GUI/IconManager.hpp
    GUI/MainFrame.cpp
    GUI/MainFrame.hpp
    GUI/BBLTopbar.cpp
    GUI/BBLTopbar.hpp
    GUI/Plater.cpp
    GUI/Plater.hpp
    GUI/PartPlate.cpp
    GUI/PartPlate.hpp
    GUI/UserNotification.hpp
    GUI/PresetComboBoxes.hpp
    GUI/PresetComboBoxes.cpp
    GUI/OfficialFilamentColorDialog.hpp
    GUI/OfficialFilamentColorDialog.cpp
    GUI/BitmapComboBox.hpp
    GUI/BitmapComboBox.cpp
    GUI/EditGCodeDialog.hpp
    GUI/EditGCodeDialog.cpp
    GUI/SavePresetDialog.hpp
    GUI/SavePresetDialog.cpp
    GUI/GUI_Colors.hpp
    GUI/GUI_Colors.cpp
    GUI/PhysicalPrinterDialog.hpp
    GUI/PhysicalPrinterDialog.cpp
    GUI/PrintHostDialogs.cpp
    GUI/PrintHostDialogs.hpp
    GUI/GUI_Factories.cpp
    GUI/GUI_Factories.hpp
    GUI/GUI_ObjectList.cpp
    GUI/GUI_ObjectList.hpp
    GUI/GUI_ObjectLayers.cpp
    GUI/GUI_ObjectLayers.hpp
    GUI/GUI_ObjectSettings.cpp
    GUI/GUI_ObjectSettings.hpp
    GUI/GUI_ObjectTable.cpp
    GUI/GUI_ObjectTable.hpp
    GUI/GUI_ObjectTableSettings.cpp
    GUI/GUI_ObjectTableSettings.hpp
    GUI/MeshUtils.cpp
    GUI/MeshUtils.hpp
    GUI/TickCode.cpp
    GUI/TickCode.hpp
    GUI/Tab.cpp
    GUI/Tab.hpp
    GUI/ParamsDialog.cpp
    GUI/ParamsDialog.hpp
	GUI/UITour.cpp
    GUI/UITour.hpp
    GUI/ParamsPanel.cpp
    GUI/ParamsPanel.hpp
    GUI/PrintHostDialogs.cpp
    GUI/PrintHostDialogs.hpp
    GUI/HintNotification.hpp
    GUI/HintNotification.cpp
    GUI/WebViewDialog.cpp
    GUI/WebViewDialog.hpp
    GUI/print_manage/PrinterMgr.cpp
    GUI/print_manage/PrinterMgr.hpp
    GUI/print_manage/App/PrinterMgrView.cpp
    GUI/print_manage/App/PrinterMgrView.hpp
    GUI/print_manage/App/DeviceMessageFilter.cpp
    GUI/print_manage/App/DeviceMessageFilter.h
    GUI/print_manage/App/WebSocketProxy.cpp
    GUI/print_manage/App/WebSocketProxySession.cpp
    GUI/print_manage/App/WebSocketProxy.hpp
    GUI/print_manage/Device/LanPrinterInterface.hpp
    GUI/print_manage/Device/LanPrinterInterface.cpp
    GUI/print_manage/Device/LanDeviceProbe.hpp
    GUI/print_manage/Device/LanDeviceProbe.cpp
    GUI/print_manage/Device/Klipper4408Interface.hpp
    GUI/print_manage/Device/Klipper4408Interface.cpp
    GUI/print_manage/Device/KlipperInterface.hpp
    GUI/print_manage/Device/KlipperInterface.cpp
    GUI/print_manage/RemotePrinterManager.hpp
    GUI/print_manage/RemotePrinterManager.cpp
    GUI/print_manage/utils/mdns.h
    GUI/print_manage/utils/mdns.c
    GUI/print_manage/utils/cxmdns.h
    GUI/print_manage/utils/cxmdns.cpp
    GUI/print_manage/data/DataType.hpp
    GUI/print_manage/data/DataType.cpp
    GUI/print_manage/data/DataCenter.hpp
    GUI/print_manage/data/DataCenter.cpp
    GUI/print_manage/AppMgr.hpp
    GUI/print_manage/AppMgr.cpp
    GUI/print_manage/Routes/Routes.hpp
    GUI/print_manage/Routes/Routes.cpp
    GUI/print_manage/Routes/AppRoutes.hpp
    GUI/print_manage/Routes/AppRoutes.cpp
    GUI/print_manage/Routes/SysRoutes.hpp
    GUI/print_manage/Routes/SysRoutes.cpp
    GUI/print_manage/Routes/DeviceMgrRoutes.hpp
    GUI/print_manage/Routes/DeviceMgrRoutes.cpp
    GUI/print_manage/TypeDefine.hpp
    GUI/print_manage/AppUtils.hpp
    GUI/print_manage/AppUtils.cpp
    GUI/print_manage/Utils.hpp
    GUI/print_manage/Utils.cpp
    GUI/print_manage/App/SendToPrinter.hpp
    GUI/print_manage/App/SendToPrinter.cpp
    GUI/print_manage/PrinterBoxFilamentPanel.hpp
    GUI/print_manage/PrinterBoxFilamentPanel.cpp
    GUI/WebGuideDialog.hpp
    GUI/WebGuideDialog.cpp
    GUI/ConfigManipulation.cpp
    GUI/ConfigManipulation.hpp
    GUI/Field.cpp
    GUI/Field.hpp
    GUI/FileArchiveDialog.cpp
    GUI/FileArchiveDialog.hpp
    GUI/OptionsGroup.cpp
    GUI/OptionsGroup.hpp
    GUI/OG_CustomCtrl.cpp
    GUI/OG_CustomCtrl.hpp
    GUI/MarkdownTip.cpp
    GUI/MarkdownTip.hpp
    GUI/2DBed.cpp
    GUI/2DBed.hpp
    GUI/3DBed.cpp
    GUI/3DBed.hpp
    GUI/CrealityBedModelMapping.hpp
    GUI/CrealityBedModelMapping.cpp
    GUI/Camera.cpp
    GUI/Camera.hpp
    GUI/CameraUtils.cpp
    GUI/CameraUtils.hpp
    GUI/wxExtensions.cpp
    GUI/wxExtensions.hpp
    GUI/ObjColorDialog.cpp
    GUI/ObjColorDialog.hpp
    GUI/TextureImportDialog.hpp
    GUI/TextureImportDialog.cpp
    GUI/WipeTowerDialog.cpp
    GUI/RammingChart.cpp
    GUI/RammingChart.hpp
    GUI/RemovableDriveManager.cpp
    GUI/RemovableDriveManager.hpp
    GUI/SurfaceDrag.cpp
    GUI/SurfaceDrag.hpp
    GUI/TextLines.cpp
    GUI/TextLines.hpp
    GUI/TransmittanceDialog.hpp
    GUI/TransmittanceDialog.cpp
    GUI/PlateSettingsDialog.cpp
    GUI/PlateSettingsDialog.hpp
    GUI/ImGuiWrapper.hpp
    GUI/ImGuiWrapper.cpp
    GUI/DeviceImageProxy.hpp
    GUI/PrinterCoverRoute.hpp
    GUI/DeviceImageProxy.cpp
    GUI/HttpServer.hpp
    GUI/HttpServer.cpp
    Config/Version.cpp
    Config/Version.hpp
    Config/DispConfig.cpp
    Config/DispConfig.h
    Utils/ASCIIFolding.cpp
    Utils/ASCIIFolding.hpp
    GUI/ConfigWizard.hpp
    GUI/MsgDialog.cpp
    GUI/MsgDialog.hpp
    GUI/Downloader.hpp
    GUI/Downloader.cpp
    GUI/DownloaderFileGet.hpp
    GUI/DownloaderFileGet.cpp
    GUI/Jobs/Job.hpp
    GUI/Jobs/Worker.hpp
    GUI/Jobs/BoostThreadWorker.hpp
    GUI/Jobs/BoostThreadWorker.cpp
    GUI/Jobs/BusyCursorJob.hpp
    GUI/Jobs/PlaterWorker.hpp
    GUI/Jobs/ArrangeJob.hpp
    GUI/Jobs/ArrangeJob.cpp
    GUI/Jobs/CreateFontNameImageJob.cpp
    GUI/Jobs/CreateFontNameImageJob.hpp
    GUI/Jobs/CreateFontStyleImagesJob.cpp
    GUI/Jobs/CreateFontStyleImagesJob.hpp
    GUI/Jobs/EmbossJob.cpp
    GUI/Jobs/EmbossJob.hpp
    GUI/Jobs/OrientJob.hpp
    GUI/Jobs/OrientJob.cpp
    GUI/simple/gpu/GpuOrient.hpp
    GUI/simple/gpu/GpuOrient.cpp
    GUI/simple/gpu/GpuOrientMetal.hpp
    GUI/simple/gpu/GpuOrientMetalKernels.hpp
    GUI/Jobs/RotoptimizeJob.hpp
    GUI/Jobs/RotoptimizeJob.cpp
    GUI/Jobs/FillBedJob.hpp
    GUI/Jobs/FillBedJob.cpp
    GUI/Jobs/SLAImportJob.hpp
    GUI/Jobs/SLAImportJob.cpp
    GUI/Jobs/ProgressIndicator.hpp
    GUI/Jobs/NotificationProgressIndicator.hpp
    GUI/Jobs/NotificationProgressIndicator.cpp
    GUI/Jobs/BooleanOperationJob.hpp
    GUI/Jobs/BooleanOperationJob.cpp
    GUI/Jobs/ThreadSafeQueue.hpp
    GUI/Jobs/SLAImportDialog.hpp
    GUI/PhysicalPrinterDialog.hpp
    GUI/PhysicalPrinterDialog.cpp
    GUI/ProgressStatusBar.hpp
    GUI/ProgressStatusBar.cpp
    GUI/Mouse3DController.cpp
    GUI/Mouse3DController.hpp
    GUI/IMSlider.cpp
    GUI/IMSlider.hpp
    GUI/Notebook.cpp
    GUI/Notebook.hpp
    GUI/TabButton.cpp
    GUI/TabButton.hpp
    GUI/Tabbook.cpp
    GUI/Tabbook.hpp
    GUI/ObjectDataViewModel.cpp
    GUI/ObjectDataViewModel.hpp
    GUI/InstanceCheck.cpp
    GUI/InstanceCheck.hpp
    GUI/Search.cpp
    GUI/Search.hpp
    GUI/NotificationManager.cpp
    GUI/NotificationManager.hpp
    GUI/UnsavedChangesDialog.cpp
    GUI/UnsavedChangesDialog.hpp
    GUI/ExtraRenderers.cpp
    GUI/ExtraRenderers.hpp
    GUI/ProjectDirtyStateManager.hpp
    GUI/ProjectDirtyStateManager.cpp
    GUI/DesktopIntegrationDialog.cpp
    GUI/DesktopIntegrationDialog.hpp
    GUI/DragCanvas.cpp
    GUI/DragCanvas.hpp
    GUI/EnableLiteModeDialog.cpp
    GUI/EnableLiteModeDialog.hpp
    GUI/BonjourDialog.cpp
    GUI/BonjourDialog.hpp
    GUI/ReleaseNote.hpp
    GUI/ReleaseNote.cpp
    GUI/SingleChoiceDialog.hpp
    GUI/SingleChoiceDialog.cpp
    GUI/BonjourDialog.hpp
    GUI/BonjourDialog.cpp
    GUI/BedShapeDialog.hpp
    GUI/BedShapeDialog.cpp
    GUI/CreatePresetsDialog.hpp
    GUI/CreatePresetsDialog.cpp
    GUI/KBShortcutsDialog.hpp
    GUI/KBShortcutsDialog.cpp
    GUI/RenderEnums.hpp
    GUI/GCodeRenderer/AdvancedRenderer.hpp
    GUI/GCodeRenderer/AdvancedRenderer.cpp
    GUI/GCodeRenderer/BaseRenderer.hpp
    GUI/GCodeRenderer/BaseRenderer.cpp
    GUI/GCodeRenderer/LegacyRenderer.hpp
    GUI/GCodeRenderer/LegacyRenderer.cpp
    Utils/Http.cpp
    Utils/Http.hpp
    Utils/DeviceTlsPolicy.cpp
    Utils/DeviceTlsPolicy.hpp
    Utils/FixModelByWin10.cpp
    Utils/FixModelByImatiSTL.cpp
    Utils/FixModelByWin10.hpp
    Utils/EmbossStyleManager.cpp
    Utils/EmbossStyleManager.hpp
    Utils/FontConfigHelp.cpp
    Utils/FontConfigHelp.hpp
    Utils/Bonjour.cpp
    Utils/Bonjour.hpp
    Utils/FileHelp.cpp
    Utils/FileHelp.hpp
    Utils/PresetUpdater.cpp
    Utils/PresetUpdater.hpp
    Utils/Process.cpp
    Utils/Process.hpp
    Utils/RaycastManager.cpp
    Utils/RaycastManager.hpp
    Utils/UndoRedo.cpp
    Utils/UndoRedo.hpp
    Utils/TCPConsole.cpp
    Utils/TCPConsole.hpp
    Utils/ColorSpaceConvert.hpp
    Utils/ColorSpaceConvert.cpp
    Utils/OctoPrint.hpp
    Utils/OctoPrint.cpp
    Utils/PrintHost.hpp
    Utils/PrintHost.cpp
    Utils/OctoPrint.cpp
    Utils/OctoPrint.hpp
    Utils/PrintHost.cpp
    Utils/PrintHost.hpp
    Utils/SerialMessageType.hpp
    Utils/SerialMessage.hpp
    Utils/MKS.cpp
    Utils/MKS.hpp
    Utils/ESP3D.cpp
    Utils/ESP3D.hpp
    Utils/WxFontUtils.cpp
    Utils/WxFontUtils.hpp
    Utils/Duet.cpp
    Utils/Duet.hpp
    Utils/FlashAir.cpp
    Utils/FlashAir.hpp
    Utils/AstroBox.cpp
    Utils/AstroBox.hpp
    Utils/Repetier.cpp
    Utils/Repetier.hpp
    GUI/calib_dlg.hpp
    GUI/calib_dlg.cpp
    GUI/PrinterCloudAuthDialog.cpp
    GUI/PrinterCloudAuthDialog.hpp
    Utils/Obico.cpp
    Utils/Obico.hpp
    Utils/Flashforge.cpp
    Utils/Flashforge.hpp
	Utils/FlushVolume.cpp
	Utils/FlushVolume.hpp
    GUI/FilamentPanel.h
    GUI/FilamentPanel.cpp
    GUI/SiderBar.h
    GUI/SiderBar.cpp
    GUI/GLToolbarCollapse.h
    GUI/GLToolbarCollapse.cpp
    GUI/GLToolbarProcess.h
    GUI/GLToolbarProcess.cpp
	GUI/LoadOldPresets.hpp
	GUI/LoadOldPresets.cpp
	GUI/LoadOldPresetsFromFolder.hpp
	GUI/LoadOldPresetsFromFolder.cpp
	GUI/PrinterPresetConfig.hpp
	GUI/PrinterPresetConfig.cpp
	GUI/Check3mfVendor.hpp
	GUI/Check3mfVendor.cpp
	GUI/AccelerationAndSpeedLimitDialog.hpp
	GUI/AccelerationAndSpeedLimitDialog.cpp
    Config/OptionConfig.h
    Config/OptionConfig.cpp
    Utils/ProfileFamilyLoader.hpp
    Utils/ProfileFamilyLoader.cpp
    GUI/StepMeshDialog.hpp
    GUI/StepMeshDialog.cpp
	GUI/PhysicalPrinter.hpp
    GUI/PhysicalPrinter.cpp
)

# SlicerBridge: preset, config, slicing and overhang services used by the Print Advisor.
list(APPEND SLIC3R_GUI_SOURCES
        GUI/simple/bridge/SlicerAction.hpp
        GUI/simple/bridge/SlicerBridge.hpp
        GUI/simple/bridge/SlicerBridge.cpp
        GUI/simple/bridge/SlicerBridgeState.cpp
        GUI/simple/bridge/SlicerBridgeActionsConfig.cpp
        GUI/simple/bridge/SlicerBridgeActionsProcess.cpp
        )

if (WIN32)
    list(APPEND SLIC3R_GUI_SOURCES
            GUI/dark_mode/dark_mode.hpp
            GUI/dark_mode/IatHook.hpp
            GUI/dark_mode/UAHMenuBar.hpp
            GUI/dark_mode.hpp
            GUI/dark_mode.cpp
        )
endif ()

if (APPLE)
    list(APPEND SLIC3R_GUI_SOURCES
            Utils/RetinaHelperImpl.mm
            Utils/MacDarkMode.mm
            Utils/MacDarkMode.hpp
            Utils/mouse_scheme.mm
            Utils/mouse_scheme.hpp
            GUI/RemovableDriveManagerMM.mm
            GUI/RemovableDriveManagerMM.h
            GUI/Mouse3DHandlerMac.mm
            GUI/InstanceCheckMac.mm
            GUI/InstanceCheckMac.h
            GUI/simple/gpu/GpuOrientMetal.mm
        )
    FIND_LIBRARY(DISKARBITRATION_LIBRARY DiskArbitration)
    FIND_LIBRARY(METAL_LIBRARY Metal REQUIRED)
    set_source_files_properties(
        GUI/simple/gpu/GpuOrientMetal.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
endif ()

include(${CMAKE_CURRENT_SOURCE_DIR}/GUI/simple/printAdvisor/PrintAdvisor.cmake) # PRINT-ADVISOR
add_library(libslic3r_gui STATIC ${SLIC3R_GUI_SOURCES})
target_include_directories(libslic3r_gui PRIVATE Utils)

# Embedding MSVC debug information with /Z7 makes this large static library
# exceed the COFF archive size limit. Keep program-database debug information
# for this target and bypass ccache, which does not support /Zi.
if (MSVC AND (CMAKE_C_COMPILER_LAUNCHER MATCHES "ccache" OR
              CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache"))
    target_compile_options(libslic3r_gui PRIVATE /Zi)
    set_property(TARGET libslic3r_gui PROPERTY C_COMPILER_LAUNCHER "${SLIC3R_MSVC_UTF8_LAUNCHER}")
    set_property(TARGET libslic3r_gui PROPERTY CXX_COMPILER_LAUNCHER "${SLIC3R_MSVC_UTF8_LAUNCHER}")
endif ()

# AUE custom preview in GCodeViewer is development-oriented.
# The switch is declared at top-level CMake as SLIC3R_DEV_AUE_CUSTOM_PREVIEW.
if (SLIC3R_DEV_AUE_CUSTOM_PREVIEW)
    target_compile_definitions(libslic3r_gui PRIVATE ENABLE_AUE_CUSTOM_PREVIEW=1)
else ()
    target_compile_definitions(libslic3r_gui PRIVATE ENABLE_AUE_CUSTOM_PREVIEW=0)
endif ()

if (HAS_IMATISTL)
    if (IMATISTL_INCLUDE_DIR)
        target_include_directories(libslic3r_gui PRIVATE ${IMATISTL_INCLUDE_DIR} ${IMATISTL_INCLUDE_DIR}/Kernel ${IMATISTL_INCLUDE_DIR}/TMesh ${IMATISTL_INCLUDE_DIR}/ImatiSTL)
    endif()
    target_compile_definitions(libslic3r_gui PRIVATE USE_HYBRID_KERNEL USE_CGAL_LAZYNT)
endif()

if (WIN32)
    target_include_directories(libslic3r_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/WebView2/include)
endif()

target_include_directories(libslic3r_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GUI)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SLIC3R_GUI_SOURCES})

encoding_check(libslic3r_gui)

target_link_libraries(libslic3r_gui libslic3r cereal::cereal imgui imguizmo glad OpenGL::GL hidapi ${wxWidgets_LIBRARIES} glfw  libcurl OpenSSL::SSL OpenSSL::Crypto noise::noise)
if (HAS_IMATISTL)
    if (IMATISTL_TARGET)
        target_link_libraries(libslic3r_gui ${IMATISTL_TARGET})
    elseif (IMATISTL_LIBRARY)
        target_link_libraries(libslic3r_gui ${IMATISTL_LIBRARY} ${IMATISTL_DEP_LIBRARIES})
    endif()
endif()
if(WIN32)
    target_link_libraries(libslic3r_gui iphlpapi)
endif()


find_package(FFmpeg)
if(FFMPEG_VERSION)
        target_link_libraries(libslic3r_gui webrtc_video)
        add_definitions(-DENABLE_FFMPEG=1)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (FLATPAK)
        target_link_libraries(libslic3r_gui -lvdpau -lva -lva-drm -lva-x11)
    endif()
endif()

if (MSVC)
    target_link_libraries(libslic3r_gui Setupapi.lib)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    FIND_LIBRARY(WAYLAND_SERVER_LIBRARIES NAMES wayland-server)
    FIND_LIBRARY(WAYLAND_EGL_LIBRARIES    NAMES wayland-egl)
    FIND_LIBRARY(WAYLAND_CLIENT_LIBRARIES NAMES wayland-client)
    find_package(CURL REQUIRED)
    target_link_libraries(libslic3r_gui ${DBUS_LIBRARIES})
    target_link_libraries(libslic3r_gui
        OpenGL::EGL
        ${WAYLAND_SERVER_LIBRARIES}
        ${WAYLAND_EGL_LIBRARIES}
        ${WAYLAND_CLIENT_LIBRARIES}
        ${CURL_LIBRARIES}
    )
elseif (APPLE)
    target_link_libraries(libslic3r_gui ${DISKARBITRATION_LIBRARY} ${METAL_LIBRARY})
endif()

if (SLIC3R_STATIC)
    # FIXME: This was previously exported by wx-config but the wxWidgets
    # cmake build forgets this and the build fails in debug mode (or on raspberry release)
    target_compile_definitions(libslic3r_gui PUBLIC -DwxDEBUG_LEVEL=0)
endif()

if (HAVE_SPNAV)
    target_link_libraries(libslic3r_gui spnav)
endif()

if (SLIC3R_STATIC AND NOT SLIC3R_STATIC_EXCLUDE_CURL AND UNIX AND NOT APPLE)
    target_compile_definitions(libslic3r_gui PRIVATE OPENSSL_CERT_OVERRIDE)
endif ()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r_gui pchheader.hpp FORCEINCLUDE)
endif ()

# We need to implement some hacks for wxWidgets and touch the underlying GTK
# layer and sub-libraries. This forces us to use the include locations and
# link these libraries.
if (UNIX AND NOT APPLE)
    find_package(GTK${SLIC3R_GTK} REQUIRED)
    if (FLATPAK)
        # I don't know why this is needed, but for whatever reason slic3r isn't
        # linking to X11 and webkit2gtk. force it.
        find_package(X11 REQUIRED)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(webkit2gtk REQUIRED webkit2gtk-4.1)
        target_link_libraries (libslic3r_gui ${X11_LIBRARIES} ${webkit2gtk_LIBRARIES})
    endif()
    target_include_directories(libslic3r_gui PRIVATE ${GTK${SLIC3R_GTK}_INCLUDE_DIRS})
    target_link_libraries(libslic3r_gui ${GTK${SLIC3R_GTK}_LIBRARIES} fontconfig)


endif ()

# Add a definition so that we can tell we are compiling slic3r.
target_compile_definitions(libslic3r_gui PRIVATE SLIC3R_CURRENTLY_COMPILING_GUI_MODULE)
