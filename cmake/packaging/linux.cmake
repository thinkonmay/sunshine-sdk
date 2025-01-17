# linux specific packaging

install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/assets/"
        DESTINATION "${SUNSHINE_ASSETS_DIR}")
# copy assets to build directory, for running without install
file(COPY "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/assets/"
        DESTINATION "${CMAKE_BINARY_DIR}/assets")
if(${SUNSHINE_BUILD_APPIMAGE} OR ${SUNSHINE_BUILD_FLATPAK})
    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/60-sunshine.rules"
            DESTINATION "${SUNSHINE_ASSETS_DIR}/udev/rules.d")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.service"
            DESTINATION "${SUNSHINE_ASSETS_DIR}/systemd/user")
else()
    find_package(Systemd)
    find_package(Udev)

    install(FILES "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/60-sunshine.rules"
            DESTINATION "${UDEV_RULES_INSTALL_DIR}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunshine.service"
            DESTINATION "${SYSTEMD_USER_UNIT_INSTALL_DIR}")
endif()

# Post install
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")

# Dependencies
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "\
            ${CPACK_DEB_PLATFORM_PACKAGE_DEPENDS} \
            libboost-filesystem${Boost_VERSION}, \
            libboost-locale${Boost_VERSION}, \
            libboost-log${Boost_VERSION}, \
            libboost-program-options${Boost_VERSION}, \
            libcap2, \
            libcurl4, \
            libdrm2, \
            libevdev2, \
            libnuma1, \
            libopus0, \
            libpulse0, \
            libva2, \
            libva-drm2, \
            libvdpau1, \
            libwayland-client0, \
            libx11-6, \
            miniupnpc, \
            openssl | libssl3")
set(CPACK_RPM_PACKAGE_REQUIRES "\
            ${CPACK_RPM_PLATFORM_PACKAGE_REQUIRES} \
            boost-filesystem >= ${Boost_VERSION}, \
            boost-locale >= ${Boost_VERSION}, \
            boost-log >= ${Boost_VERSION}, \
            boost-program-options >= ${Boost_VERSION}, \
            libcap >= 2.22, \
            libcurl >= 7.0, \
            libdrm >= 2.4.97, \
            libevdev >= 1.5.6, \
            libopusenc >= 0.2.1, \
            libva >= 2.14.0, \
            libvdpau >= 1.5, \
            libwayland-client >= 1.20.0, \
            libX11 >= 1.7.3.1, \
            miniupnpc >= 2.2.4, \
            numactl-libs >= 2.0.14, \
            openssl >= 3.0.2, \
            pulseaudio-libs >= 10.0")

# This should automatically figure out dependencies, doesn't work with the current config
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)