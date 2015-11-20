Name:             pulseaudio-modules-tizen
Summary:          Pulseaudio modules for Tizen
Version:          5.0.15
Release:          0
Group:            Multimedia/Audio
License:          LGPL-2.1+
Source0:          %{name}-%{version}.tar.gz
BuildRequires:    libtool-ltdl-devel
BuildRequires:    libtool
BuildRequires:    intltool
BuildRequires:    pkgconfig(dbus-1)
BuildRequires:    pkgconfig(iniparser)
BuildRequires:    pkgconfig(json-c)
BuildRequires:    pkgconfig(vconf)
BuildRequires:    pkgconfig(libpulse)
BuildRequires:    pkgconfig(pulsecore)
BuildRequires:    pulseaudio
BuildRequires:    m4
%if %{with pulseaudio_dlog}
BuildRequires:    pkgconfig(dlog)
%endif
Requires(post):   /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
This package contains pulseaudio modules for tizen audio system.

%prep
%setup -q

%build
export CFLAGS="%{optflags} -fno-strict-aliasing"

export LD_AS_NEEDED=0
%reconfigure --prefix=%{_prefix} \
        --disable-static \
%if %{with pulseaudio_dlog}
        --enable-dlog \
%endif

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
%{_libdir}/pulse-5.0/modules/module-*.so
%{_libdir}/pulse-5.0/modules/libhal-manager.so
