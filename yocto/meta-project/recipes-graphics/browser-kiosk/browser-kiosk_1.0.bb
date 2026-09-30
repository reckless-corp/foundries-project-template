# SPDX-License-Identifier: MIT
SUMMARY = "Fullscreen browser for the device's local web application"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/COPYING.MIT;md5=3da9cfbcb788c80a0384361b4de20420"

SRC_URI = "file://browser-kiosk file://browser-kiosk.service \
           file://browser-kiosk.default file://kiosk.ini file://kiosk.conf"
S = "${UNPACKDIR}"

inherit allarch systemd features_check
REQUIRED_DISTRO_FEATURES = "systemd wayland opengl pam"

RDEPENDS:${PN} = "cog wpewebkit weston-init curl dbus"
RCONFLICTS:${PN} = "matrix-console"
SYSTEMD_SERVICE:${PN} = "browser-kiosk.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -Dm0755 ${S}/browser-kiosk ${D}${bindir}/browser-kiosk
    install -Dm0644 ${S}/browser-kiosk.service ${D}${systemd_system_unitdir}/browser-kiosk.service
    install -Dm0644 ${S}/kiosk.conf ${D}${systemd_system_unitdir}/weston.service.d/kiosk.conf
    install -Dm0644 ${S}/kiosk.ini ${D}${sysconfdir}/xdg/weston/kiosk.ini
    install -Dm0644 ${S}/browser-kiosk.default ${D}${sysconfdir}/default/browser-kiosk
    sed -i -e 's|@BINDIR@|${bindir}|g' -e 's|@SYSCONFDIR@|${sysconfdir}|g' \
        -e 's|@RUNTIMEDIR@|${runtimedir}|g' \
        ${D}${systemd_system_unitdir}/browser-kiosk.service \
        ${D}${systemd_system_unitdir}/weston.service.d/kiosk.conf
}

FILES:${PN} += "${systemd_system_unitdir}/weston.service.d/kiosk.conf"
CONFFILES:${PN} += "${sysconfdir}/default/browser-kiosk ${sysconfdir}/xdg/weston/kiosk.ini"
