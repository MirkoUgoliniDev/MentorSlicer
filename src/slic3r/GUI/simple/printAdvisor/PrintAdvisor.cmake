# Print Advisor sources (ours, not Creality's). Included with one line from
# src/slic3r/CMakeLists.txt, before libslic3r_gui is created.
# Paths are relative to src/slic3r (include() keeps CMAKE_CURRENT_SOURCE_DIR).

# Device telemetry, the background monitor, the proxy link, the hooks called from Creality code,
# the local API server and its routes.
list(APPEND SLIC3R_GUI_SOURCES
    GUI/print_manage/App/DeviceTelemetryCache.cpp
    GUI/print_manage/App/DeviceTelemetryCache.h
    GUI/print_manage/App/PrintAdvisorMonitor.cpp
    GUI/print_manage/App/PrintAdvisorMonitor.hpp
    GUI/print_manage/App/WebSocketProxyAdvisor.cpp
    GUI/print_manage/App/WebSocketProxyAdvisor.hpp
    GUI/simple/printAdvisor/PrintAdvisorHooks.cpp
    GUI/simple/printAdvisor/PrintAdvisorHooks.hpp
    GUI/simple/printAdvisor/PrintAdvisorPageBridge.cpp
    GUI/simple/printAdvisor/PrintAdvisorPageBridge.hpp
    GUI/simple/printAdvisor/AdvisorBranding.cpp
    GUI/simple/printAdvisor/AdvisorBranding.hpp
    GUI/simple/printAdvisor/PrintAdvisorServer.hpp
    GUI/simple/printAdvisor/PrintAdvisorServer.cpp
    GUI/simple/printAdvisor/AdvisorConfirmDialog.hpp
    GUI/simple/printAdvisor/AdvisorConfirmDialog.cpp
    GUI/simple/printAdvisor/AdvisorInfoDialog.hpp
    GUI/simple/printAdvisor/AdvisorInfoDialog.cpp
    GUI/simple/printAdvisor/PrintAdvisorInternal.hpp
    GUI/simple/printAdvisor/PrintAdvisorInternal.cpp
    GUI/simple/printAdvisor/PrintAdvisorPrinterControl.cpp
    GUI/simple/printAdvisor/PrintAdvisorPlate.cpp
    GUI/simple/printAdvisor/PrintAdvisorSupports.cpp
    GUI/simple/printAdvisor/PrintAdvisorCamera.cpp
    GUI/simple/printAdvisor/PrintAdvisorVerdict.cpp
)
