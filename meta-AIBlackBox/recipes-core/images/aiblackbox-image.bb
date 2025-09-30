require recipes-core/images/core-image-base.bb

EXTRA_IMAGE_FEATURES += " ssh-server-openssh debug-tweaks"

LICENSE_FLAGS_ACCEPTED += "commercial"

IMAGE_INSTALL:append = " \
  v4l-utils \
  gstreamer1.0 \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-base-videoconvert \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-good-jpeg \
  gstreamer1.0-plugins-good-isomp4 \
  gstreamer1.0-plugins-good-video4linux2 \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-bad-videoparsersbad \
  gstreamer1.0-plugins-bad-v4l2codecs \
  gstreamer1.0-plugins-ugly \
  gstreamer1.0-plugins-ugly-x264 \
  gstreamer1.0-libav \
  gstreamer1.0-plugins-bad-kms \
  x264 \
  opencv opencv-apps \
  cjson \
  nlohmann-json \
  python3 python3-core python3-venv python3-pip python3-setuptools python3-wheel \
  python3-numpy python3-pyyaml python3-can \
  can-utils iproute2 i2c-tools \
  libdrm libdrm-tests \
  librealsense2 librealsense2-debug-tools librealsense2-dbg \
  aiblackbox-can aiblackbox-diagnostics libhardware \
  tcpdump \
  python3-core python3-modules \
  python3-numpy \
  python3-pygobject \
  gobject-introspection \
"
IMAGE_INSTALL:append = " aibb-spi0-mcp2515 "
IMAGE_INSTALL:append = " kernel-module-spidev kernel-module-mcp251x kernel-module-can kernel-module-can-raw kernel-module-can-dev "
IMAGE_CLASSES += "rpi-config"
IMAGE_FSTYPES += " rpi-sdimg "
KERNEL_MODULE_AUTOLOAD += " mcp251x can can-raw can-dev "
