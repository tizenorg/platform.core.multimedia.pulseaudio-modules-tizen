# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# This file is part of PulseAudio.
#
# Copyright 2004-2008 Lennart Poettering
# Copyright 2006-2007 Pierre Ossman <ossman@cendio.se> for Cendio AB
#
# PulseAudio is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# PulseAudio is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with PulseAudio; if not, write to the Free Software Foundation,
# Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.

AC_PREREQ(2.63)

AC_INIT([pulseaudio-module-tizen],[0.1])
AC_CONFIG_SRCDIR([src/module-tizenaudio-policy.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability subdir-objects silent-rules color-tests dist-xz tar-ustar])

#### Checks for programs. ####

# mkdir -p

AC_PROG_MKDIR_P

# CC

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
# Only required if you want the WebRTC canceller -- no runtime dep on
# libstdc++ otherwise
AC_PROG_CXX
AC_PROG_GCC_TRADITIONAL
AC_USE_SYSTEM_EXTENSIONS

# M4

AC_CHECK_PROGS([M4], gm4 m4, no)
AS_IF([test "x$M4" = "xno"], AC_MSG_ERROR([m4 missing]))

# pkg-config

PKG_PROG_PKG_CONFIG

#### Compiler flags ####

AX_APPEND_COMPILE_FLAGS(
    [-Wall -W -Wextra -pipe -Wno-long-long -Wno-overlength-strings -Wunsafe-loop-optimizations -Wundef -Wformat=2 -Wlogical-op -Wsign-compare -Wformat-security -Wmissing-include-dirs -Wformat-nonliteral -Wold-style-definition -Wpointer-arith -Winit-self -Wdeclaration-after-statement -Wfloat-equal -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Wmissing-declarations -Wmissing-noreturn -Wshadow -Wendif-labels -Wcast-align -Wstrict-aliasing -Wwrite-strings -Wno-unused-parameter -ffast-math -fno-common -fdiagnostics-show-option],
    [], [-pedantic -Werror])

# Only enable fastpath asserts when doing a debug build, e.g. from bootstrap.sh.
AS_CASE([" $CFLAGS "], [*" -O0 "*], [], [AX_APPEND_FLAG(["-DFASTPATH"], [CPPFLAGS])])

# Only set _FORTIFY_SOURCE when optimizations are enabled. If optimizations
# are disabled, _FORTIFY_SOURCE doesn't do anything, and causes tons of
# warnings during compiling on some distributions (at least Fedora).
AS_CASE([" $CFLAGS "], [*" -O0 "*], [], [AX_APPEND_FLAG(["-D_FORTIFY_SOURCE=2"], [CPPFLAGS])])


#### Linker flags ####

# Use immediate (now) bindings; avoids the funky re-call in itself.
# The -z now syntax is lifted from Sun's linker and works with GNU's too, other linkers might be added later.
AX_APPEND_LINK_FLAGS([-Wl,-z,now], [IMMEDIATE_LDFLAGS])
AC_SUBST([IMMEDIATE_LDFLAGS])

# On ELF systems we don't want the libraries to be unloaded since we don't clean them up properly,
# so we request the nodelete flag to be enabled.
# On other systems, we don't really know how to do that, but it's welcome if somebody can tell.
AX_APPEND_LINK_FLAGS([-Wl,-z,nodelete], [NODELETE_LDFLAGS])
AC_SUBST([NODELETE_LDFLAGS])

# Check for the proper way to build libraries that have no undefined symbols
case $host in
    # FreeBSD (et al.) does not complete linking for shared objects when pthreads
    # are requested, as different implementations are present.
    *-freebsd* | *-openbsd*) ;;
    *)
        for possible_flag in "-Wl,--no-undefined" "-Wl,-z,defs"; do
            AX_CHECK_LINK_FLAG([$possible_flag], [NOUNDEFINED_LDFLAGS="$possible_flag"; break])
        done
    ;;
esac
AC_SUBST([NOUNDEFINED_LDFLAGS])


#### Atomic operations ####

# Native atomic operation support
AC_ARG_ENABLE([atomic-arm-linux-helpers],
    AS_HELP_STRING([--disable-atomic-arm-linux-helpers],[use inline asm or libatomic_ops instead]))

AC_ARG_ENABLE([atomic-arm-memory-barrier],
    AS_HELP_STRING([--enable-atomic-arm-memory-barrier],[only really needed in SMP arm systems]))

if test "x$enable_atomic_arm_memory_barrier" = "xyes"; then
    AC_DEFINE_UNQUOTED(ATOMIC_ARM_MEMORY_BARRIER_ENABLED, 1, [Enable memory barriers])
fi

# If everything else fails use libatomic_ops
need_libatomic_ops=yes

AC_CACHE_CHECK([whether $CC knows __sync_bool_compare_and_swap()],
    pulseaudio_cv_sync_bool_compare_and_swap, [
    AC_LINK_IFELSE(
        [AC_LANG_PROGRAM([], [[int a = 4; __sync_bool_compare_and_swap(&a, 4, 5);]])],
        [pulseaudio_cv_sync_bool_compare_and_swap=yes],
        [pulseaudio_cv_sync_bool_compare_and_swap=no])
    ])

if test "$pulseaudio_cv_sync_bool_compare_and_swap" = "yes" ; then
    AC_DEFINE([HAVE_ATOMIC_BUILTINS], 1, [Have __sync_bool_compare_and_swap() and friends.])
    need_libatomic_ops=no
else
    # HW specific atomic ops stuff
    AC_MSG_CHECKING([architecture for native atomic operations])
    case $host in
        arm*)
            AC_MSG_RESULT([arm])
            AC_MSG_CHECKING([whether we can use Linux kernel helpers])
            # The Linux kernel helper functions have been there since 2.6.16. However
            # compile time checking for kernel version in cross compile environment
            # (which is usually the case for arm cpu) is tricky (or impossible).
            if test "x$os_is_linux" = "x1" && test "x$enable_atomic_arm_linux_helpers" != "xno"; then
                AC_MSG_RESULT([yes])
                AC_DEFINE_UNQUOTED(ATOMIC_ARM_LINUX_HELPERS, 1, [special arm linux implementation])
                need_libatomic_ops=no
            else
                AC_MSG_RESULT([no])
                AC_CACHE_CHECK([compiler support for arm inline asm atomic operations],
                    pulseaudio_cv_support_arm_atomic_ops, [
                    AC_COMPILE_IFELSE(
                        [AC_LANG_PROGRAM([], [[
                            volatile int a=0;
                            int o=0, n=1, r;
                            asm volatile ("ldrex    %0, [%1]\n"
                                          "subs  %0, %0, %2\n"
                                          "strexeq %0, %3, [%1]\n"
                                          : "=&r" (r)
                                          : "r" (&a), "Ir" (o), "r" (n)
                                          : "cc");
                            return (a==1 ? 0 : -1);
                        ]])],
                        [pulseaudio_cv_support_arm_atomic_ops=yes],
                        [pulseaudio_cv_support_arm_atomic_ops=no])
                ])
                AS_IF([test "$pulseaudio_cv_support_arm_atomic_ops" = "yes"], [
                    AC_DEFINE([ATOMIC_ARM_INLINE_ASM], 1, [Have ARM atomic instructions.])
                    need_libatomic_ops=no
                ])
            fi
        ;;
        *-netbsdelf5*)
            AC_MSG_RESULT([yes])
            need_libatomic_ops=no
        ;;
        *-freebsd*)
            AC_MSG_RESULT([yes])
            need_libatomic_ops=no
        ;;
        *)
            AC_MSG_RESULT([unknown])
        ;;
    esac
fi

# If we're on ARM, check for the ARMV6 instructions we need */
case $host in
  arm*)
    AC_CACHE_CHECK([support for required armv6 instructions],
      pulseaudio_cv_support_armv6,
      [AC_COMPILE_IFELSE(
         [AC_LANG_PROGRAM([],
           [[volatile int a = -60000, b = 0xaaaabbbb, c = 0xccccdddd;
             asm volatile ("ldr r0, %2 \n"
                           "ldr r2, %3 \n"
                           "ldr r3, %4 \n"
                           "ssat r1, #8, r0 \n"
                           "str r1, %0 \n"
                           "pkhbt r1, r3, r2, LSL #8 \n"
                           "str r1, %1 \n"
                           : "=m" (a), "=m" (b)
                           : "m" (a), "m" (b), "m" (c)
                           : "r0", "r1", "r2", "r3", "cc");
             return (a == -128 && b == 0xaabbdddd) ? 0 : -1;
           ]])],
         [pulseaudio_cv_support_armv6=yes],
         [pulseaudio_cv_support_armv6=no])
      ])
    AS_IF([test "$pulseaudio_cv_support_armv6" = "yes"], [
        AC_DEFINE([HAVE_ARMV6], 1, [Have ARMv6 instructions.])
      ])
  ;;
  *)
  ;;
esac

#### libtool stuff ####

LT_PREREQ(2.4)
LT_INIT([dlopen win32-dll disable-static])

dnl As an extra safety device, check for lt_dladvise_init() which is
dnl only implemented in libtool 2.x, and refine as we go if we have
dnl refined requirements.
dnl
dnl Check the header files first since the system may have a
dnl libltdl.so for runtime, but no headers, and we want to bail out as
dnl soon as possible.
dnl
dnl We don't need any special variable for this though, since the user
dnl can give the proper place to find libltdl through the standard
dnl variables like LDFLAGS and CPPFLAGS.

AC_CHECK_HEADER([ltdl.h],
    [AC_CHECK_LIB([ltdl], [lt_dladvise_init], [LIBLTDL=-lltdl], [LIBLTDL=])],
    [LIBLTDL=])

AS_IF([test "x$LIBLTDL" = "x"],
    [AC_MSG_ERROR([Unable to find libltdl version 2. Makes sure you have libtool 2.4 or later installed.])])
AC_SUBST([LIBLTDL])


###################################
#   Basic environment checks      #
###################################

#### Checks for header files. ####

# ISO
AC_HEADER_STDC

# POSIX
AC_CHECK_HEADERS_ONCE([arpa/inet.h glob.h grp.h netdb.h netinet/in.h \
    netinet/in_systm.h netinet/tcp.h poll.h pwd.h sched.h \
    sys/mman.h sys/select.h sys/socket.h sys/wait.h \
    sys/uio.h syslog.h sys/dl.h dlfcn.h linux/sockios.h])
AC_CHECK_HEADERS([netinet/ip.h], [], [],
                 [#include <sys/types.h>
                  #if HAVE_NETINET_IN_H
                  # include <netinet/in.h>
                  #endif
                  #if HAVE_NETINET_IN_SYSTM_H
                  # include <netinet/in_systm.h>
                  #endif
                 ])
AC_CHECK_HEADERS([sys/resource.h], [HAVE_SYS_RESOURCE_H=1], [HAVE_SYS_RESOURCE_H=0])
AC_SUBST(HAVE_SYS_RESOURCE_H)
AC_CHECK_HEADERS([sys/un.h], [HAVE_AF_UNIX=1], [HAVE_AF_UNIX=0])
AM_CONDITIONAL(HAVE_AF_UNIX, test "x$HAVE_AF_UNIX" = "x1")
AC_SUBST(HAVE_AF_UNIX)

# Linux
AC_CHECK_HEADERS([linux/input.h], [HAVE_EVDEV=1], [HAVE_EVDEV=0])
AM_CONDITIONAL([HAVE_EVDEV], [test "x$HAVE_EVDEV" = "x1"])

AC_CHECK_HEADERS_ONCE([sys/prctl.h])

#### Check for libs ####

# ISO
AC_SEARCH_LIBS([pow], [m])

# POSIX
AC_SEARCH_LIBS([sched_setscheduler], [rt])
AC_SEARCH_LIBS([dlopen], [dl])
AC_SEARCH_LIBS([shm_open], [rt])
AC_SEARCH_LIBS([inet_ntop], [nsl])
AC_SEARCH_LIBS([timer_create], [rt])
AC_SEARCH_LIBS([pthread_setaffinity_np], [pthread])
AC_SEARCH_LIBS([pthread_getname_np], [pthread])
AC_SEARCH_LIBS([pthread_setname_np], [pthread])

# BSD
AC_SEARCH_LIBS([connect], [socket])
AC_SEARCH_LIBS([backtrace], [execinfo ubacktrace])


#### Check for functions ####

# ISO
AC_CHECK_FUNCS_ONCE([lrintf strtof])

# POSIX
AC_FUNC_FORK
AC_FUNC_GETGROUPS
AC_CHECK_FUNCS_ONCE([chmod chown fstat fchown fchmod clock_gettime getaddrinfo getgrgid_r getgrnam_r \
    getpwnam_r getpwuid_r gettimeofday getuid mlock nanosleep \
    pipe posix_fadvise posix_madvise posix_memalign setpgid setsid shm_open \
    sigaction sleep symlink sysconf uname pthread_setaffinity_np pthread_getname_np pthread_setname_np])
AC_CHECK_FUNCS([mkfifo], [HAVE_MKFIFO=1], [HAVE_MKFIFO=0])
AC_SUBST(HAVE_MKFIFO)
AM_CONDITIONAL(HAVE_MKFIFO, test "x$HAVE_MKFIFO" = "x1")

# X/OPEN
AC_CHECK_FUNCS_ONCE([readlink])

# SUSv2
AC_CHECK_FUNCS_ONCE([ctime_r usleep])

# SUSv3
AC_CHECK_FUNCS_ONCE([strerror_r])

# BSD
AC_CHECK_FUNCS_ONCE([lstat])

# Non-standard
AC_CHECK_FUNCS_ONCE([setresuid setresgid setreuid setregid seteuid setegid ppoll strsignal sig2str strtof_l pipe2 accept4])

AC_FUNC_ALLOCA

AC_CHECK_FUNCS([regexec], [HAVE_REGEX=1], [HAVE_REGEX=0])
AM_CONDITIONAL(HAVE_REGEX, [test "x$HAVE_REGEX" = "x1"])

# Large File-Support (LFS)
AC_SYS_LARGEFILE
# Check for open64 to know if the current system does have open64() and similar functions
AC_CHECK_FUNCS_ONCE([open64])


###################################
#      External libraries         #
###################################

PKG_CHECK_MODULES(PA, libpulse)
AC_SUBST(PA_CFLAGS)
AC_SUBST(PA_LIBS)

PKG_CHECK_MODULES(PACORE, pulsecore)
AC_SUBST(PACORE_CFLAGS)
AC_SUBST(PACORE_LIBS)
AC_SUBST(PACORE_LDFLAGS)

#### [lib]iconv ####

AM_ICONV

#### json parsing ####

PKG_CHECK_MODULES(LIBJSON, [ json-c >= 0.11 ])

#### Sound file ####
PKG_CHECK_MODULES(INIPARSER, iniparser)
AC_SUBST(INIPARSER_CFLAGS)
AC_SUBST(INIPARSER_LIBS)

PKG_CHECK_MODULES(VCONF, vconf)
AC_SUBST(VCONF_CFLAGS)
AC_SUBST(VCONF_LIBS)

dnl use dlog --------------------------------------------------------------------------
AC_ARG_ENABLE(dlog, AC_HELP_STRING([--enable-dlog], [using dlog]),
[
 case "${enableval}" in
	 yes) USE_DLOG=yes ;;
	 no)  USE_DLOG=no ;;
	 *)   AC_MSG_ERROR(bad value ${enableval} for --enable-dlog) ;;
 esac
 ],[USE_DLOG=no])

if test "x$USE_DLOG" = "xyes"; then
PKG_CHECK_MODULES(DLOG, dlog)
AC_SUBST(DLOG_CFLAGS)
AC_SUBST(DLOG_LIBS)
fi
AM_CONDITIONAL(USE_DLOG, test "x$USE_DLOG" = "xyes")
dnl end --------------------------------------------------------------------

#### D-Bus support (optional) ####

AC_ARG_ENABLE([dbus],
    AS_HELP_STRING([--disable-dbus],[Disable optional D-Bus support]))

AS_IF([test "x$enable_dbus" != "xno"],
    [PKG_CHECK_MODULES(DBUS, [ dbus-1 >= 1.4.12 ], HAVE_DBUS=1, HAVE_DBUS=0)],
    HAVE_DBUS=0)

AS_IF([test "x$enable_dbus" = "xyes" && test "x$HAVE_DBUS" = "x0"],
    [AC_MSG_ERROR([*** D-Bus not available or too old version])])

AS_IF([test "x$HAVE_DBUS" = "x1"],
    [
        save_CFLAGS="$CFLAGS"; CFLAGS="$CFLAGS $DBUS_CFLAGS"
        save_LIBS="$LIBS"; LIBS="$LIBS $DBUS_LIBS"
        AC_CHECK_FUNCS(dbus_watch_get_unix_fd)
        CFLAGS="$save_CFLAGS"
        LIBS="$save_LIBS"
    ])

AC_SUBST(HAVE_DBUS)
AM_CONDITIONAL([HAVE_DBUS], [test "x$HAVE_DBUS" = x1])
AS_IF([test "x$HAVE_DBUS" = "x1"], AC_DEFINE([HAVE_DBUS], 1, [Have D-Bus.]))

PA_MACHINE_ID="${sysconfdir}/machine-id"
AX_DEFINE_DIR(PA_MACHINE_ID, PA_MACHINE_ID, [D-Bus machine-id file])
PA_MACHINE_ID_FALLBACK="${localstatedir}/lib/dbus/machine-id"
AX_DEFINE_DIR(PA_MACHINE_ID_FALLBACK, PA_MACHINE_ID_FALLBACK,
	      [Fallback machine-id file])

#### ladspa filters for samsung #####
PKG_CHECK_MODULES(AUDIOFILTERS_SEC_SA, audiofilters-sec-sa)
AC_SUBST(AUDIOFILTERS_SEC_SA_CFLAGS)
AC_SUBST(AUDIOFILTERS_SEC_SA_LIBS)

PKG_CHECK_MODULES(AUDIOFILTERS_SEC_SRC, audiofilters-sec-src)
AC_SUBST(AUDIOFILTERS_SEC_SRC_CFLAGS)
AC_SUBST(AUDIOFILTERS_SEC_SRC_LIBS)

###################################
#            Output               #
###################################

AC_DEFINE_UNQUOTED(PA_CFLAGS, "$CFLAGS", [The CFLAGS used during compilation])

AC_CONFIG_FILES([
Makefile
])
AC_OUTPUT
