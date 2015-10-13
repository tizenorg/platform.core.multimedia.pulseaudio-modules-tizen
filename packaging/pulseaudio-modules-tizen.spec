%define pulseversion  5.0
%define udev_dir %{_prefix}/lib/udev

Name:             pulseaudio-modules-tizen
Summary:          Improved Linux sound server
Version:          5.0
Release:          46
Group:            Multimedia/Audio
License:          LGPL-2.1+
URL:              http://pulseaudio.org
Source0:          http://www.freedesktop.org/software/pulseaudio/releases/%{name}-%{version}.tar.gz
BuildRequires:    libtool-ltdl-devel
BuildRequires:    libtool
BuildRequires:    intltool
BuildRequires:    pkgconfig(alsa)
BuildRequires:    pkgconfig(dbus-1)
BuildRequires:    pkgconfig(iniparser)
BuildRequires:    pkgconfig(libudev)
BuildRequires:    pkgconfig(json)
BuildRequires:    pkgconfig(vconf)
BuildRequires:    pkgconfig(libpulse)
BuildRequires:    pkgconfig(pulsecore)
BuildRequires:    pulseaudio
BuildRequires:    m4
BuildRequires:    systemd-devel
BuildRequires:    libcap-devel
%if %{with pulseaudio_dlog}
BuildRequires:    pkgconfig(dlog)
%endif
Requires:         udev
Requires(post):   /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
This package contains pulseaudio modules for tizen audio system.

%prep
%setup -q

%build
export CFLAGS="%{optflags} -fno-strict-aliasing -D__TIZEN__ -D__TIZEN_BT__ -D__TIZEN_LOG__ -DTIZEN_MICRO -DBLUETOOTH_APTX_SUPPORT"
%if 0%{?sec_build_binary_debug_enable}
export CFLAGS+=" -DTIZEN_DEBUG_ENABLE"
export CXXFLAGS="$CXXFLAGS -DTIZEN_DEBUG_ENABLE"
export FFLAGS="$FFLAGS -DTIZEN_DEBUG_ENABLE"
%endif

export LD_AS_NEEDED=0
%reconfigure --prefix=%{_prefix} \
        --disable-static \
        --enable-alsa \
        --enable-systemd \
        --with-database=tdb \
%if %{with pulseaudio_dlog}
        --enable-dlog \
%endif
        --with-udev-rules-dir=%{udev_dir}/rules.d \
        --with-system-user=pulse \
        --with-system-group=pulse \
        --with-access-group=pulse-access

%__make %{?_smp_mflags} V=1

%install
%make_install

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%files
%manifest pulseaudio-modules-tizen.manifest
%defattr(-,root,root,-)
%license LICENSE.LGPL-2.1+
%{_libdir}/pulse-%{version}/modules/module-tizenaudio-sink.so
%{_libdir}/pulse-%{version}/modules/module-tizenaudio-source.so
%{_libdir}/pulse-%{version}/modules/module-sound-player.so
%{_libdir}/pulse-%{version}/modules/module-policy.so
