BIN_TARGETS := \
s6-linux-init-single \
s6-linux-init-halt \
s6-linux-init-poweroff \
s6-linux-init-reboot \
s6-linux-init-shutdown \
s6-linux-init-shutdownd \
s6-linux-init-logouthookd \
s6-linux-init \
s6-linux-init-telinit \
s6-linux-init-maker

LIB_DEFS := S6_LINUX_INIT=s6_linux_init
