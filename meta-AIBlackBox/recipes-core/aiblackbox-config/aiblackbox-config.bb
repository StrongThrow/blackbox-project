SUMMARY = "AI BlackBox default config"
LICENSE = "CLOSED"

SRC_URI = "file://config.json"
S = "${WORKDIR}"

inherit allarch

do_install() {
    install -d ${D}${sysconfdir}/aiblackbox
    install -m 0644 ${WORKDIR}/config.json ${D}${sysconfdir}/aiblackbox/config.json
}

FILES:${PN} += "${sysconfdir}/aiblackbox/config.json"
