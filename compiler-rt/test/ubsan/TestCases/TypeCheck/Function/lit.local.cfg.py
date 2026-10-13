if config.target_os not in ["Darwin", "FreeBSD", "Linux", "NetBSD"]:
    config.unsupported = True
# Work around "Cannot represent a difference across sections"
if config.target_arch == "powerpc64":
    config.unsupported = True
# Work around "library ... not found: needed by main executable" in qemu.
if config.android and config.target_arch not in ["x86", "x86_64"]:
    config.unsupported = True
# A function pointer is the address of a descriptor, which has no signature
# before it, and the driver refuses -fsanitize=function.
if config.target_arch == "ia64":
    config.unsupported = True
