savedcmd_protogpu_drv.mod := printf '%s\n'   protogpu_drv.o | awk '!x[$$0]++ { print("./"$$0) }' > protogpu_drv.mod
