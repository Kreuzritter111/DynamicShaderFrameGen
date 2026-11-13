# Legacy entry point: override paths with CMake cache variables.
# New builds should use the windows-release preset and the vcpkg manifest.
if(DEFINED VCPKG_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${VCPKG_PREFIX}")
endif()
