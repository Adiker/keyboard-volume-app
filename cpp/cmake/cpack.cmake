# CPack Packaging Configuration

# General package settings
set(CPACK_PACKAGE_NAME "keyboard-volume-app")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_RELEASE "1")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Per-app volume control via keyboard with OSD overlay (Qt6)")
set(CPACK_PACKAGE_VENDOR "Adiker")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Adiker/keyboard-volume-app")
set(CPACK_PACKAGE_CONTACT "Adiker <https://github.com/Adiker>")
set(CPACK_PACKAGE_LICENSE "GPL-2.0-or-later")

# Use /usr as the install prefix inside the package
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Path to the license file
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
endif()

# Enable DEB and RPM generators
set(CPACK_GENERATOR "DEB;RPM")

# --- Debian (.deb) Generator Settings ---
set(CPACK_DEBIAN_PACKAGE_NAME "keyboard-volume-app")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
# Dynamically determine library dependencies using dpkg-shlibdeps
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Explicitly depend on dynamically loaded Qt QPA platform plugins
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins, qt6-wayland")

# --- RPM (.rpm) Generator Settings ---
set(CPACK_RPM_PACKAGE_NAME "keyboard-volume-app")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
# Dynamically determine RPM dependencies
set(CPACK_RPM_PACKAGE_AUTOREQ ON)

include(CPack)
