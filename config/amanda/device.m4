# SYNOPSIS
#
#   AMANDA_CHECK_DEVICES
#
# OVERVIEW
#
#
AC_DEFUN([AMANDA_CHECK_DEVICES], [
    AC_REQUIRE([AMANDA_TAPE_DEVICE])

    amanda_devices=' file diskflat null tape'
    missing_devices=''
    AMANDA_DEVICES=$amanda_devices
    AC_SUBST(AMANDA_DEVICES)
])


# SYNOPSIS
#
#   AMANDA_SHOW_DEVICES_SUMMARY
#
# OVERVIEW
#
#   Show a summary of the settings from this file.
#
AC_DEFUN([AMANDA_SHOW_DEVICES_SUMMARY],
[
    echo "Amanda Devices:$amanda_devices$missing_devices"
])
