DESCRIPTION = "Matchbox Window Manager Desktop"
LICENSE = "GPL"
DEPENDS = "gtk+ startup-notification"
RDEPENDS = "matchbox-common"
SECTION = "x11/wm"

SRC_URI = "http://projects.o-hand.com/matchbox/sources/matchbox-desktop/2.0/matchbox-desktop-${PV}.tar.bz2"

EXTRA_OECONF = "--enable-startup-notification"

inherit autotools pkgconfig
