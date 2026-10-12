Name:           edhar-clock
Version:        %{version}
Release:        1%{?dist}
Summary:        The clock of Saunt Edhar, a screensaver from "Anathem"
License:        MIT
Source0:        edhar-clock-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
The great clock in the Mynster of the Concent of Saunt Edhar, from Neal
Stephenson's novel Anathem: a meteorite on a chain that sinks through the day,
and four weights of black stone that tell the year, decade, century and
millennium.

This screensaver sets the clock from the real time and date. At noon four fraas
wind it, which is the aut of Provener, and every so often a whole day is run
through quickly.

On Wayland desktops it starts by itself after five minutes idle.
edhar-clock-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/edhar-clock

%files
%license LICENSE
%doc README.md
%{_bindir}/edhar-clock
%{_bindir}/edhar-clock-screensaver
%{_datadir}/applications/edhar-clock.desktop
%{_datadir}/icons/hicolor/256x256/apps/edhar-clock.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/edhar-clock-screensaver.desktop
