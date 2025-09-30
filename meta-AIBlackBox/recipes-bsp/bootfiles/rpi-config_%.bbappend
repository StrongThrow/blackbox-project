RPI_EXTRA_CONFIG:append = " \
disable_overscan=1\n \
hdmi_group=2\n \
hdmi_mode=87\n \
hdmi_cvt=800 480 60 6 0 0 0\n \
dtparam=spi=on\n \
dtoverlay=mcp2515-can0,oscillator=8000000,interrupt=12,spimaxfrequency=10000000\n \
"
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"