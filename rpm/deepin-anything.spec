%bcond_with check
%global _unpackaged_files_terminate_build 0 
%global debug_package   %{nil}
%global common_description %{expand: Something like everything, but nothing is really like anything...}
%global dname dkms
%global lname libs
%global sname server

Name:          deepin-anything
Version:       5.0.8
Release:       2
Summary:       Something like everything, but nothing is really like anything...
License:       GPLv3
URL:           https://uos-packages.deepin.com/uos/pool/main/d/deepin-anything/
Source0:       %{name}-%{version}.orig.tar.xz


BuildRequires: qt5-qtbase-devel
BuildRequires: dtkcore-devel
BuildRequires: udisks2-qt5
BuildRequires: udisks2-qt5-devel
BuildRequires: libmount
BuildRequires: libmount-devel


%description
%{common_description}

%package -n %{name}-%{dname}
Summary:    %{summary}
%description -n %{name}-%{dname}


%package -n %{name}-%{lname}
Summary:    %{summary}
%description -n %{name}-%{lname}

%package -n %{name}-%{sname}
Summary:    %{summary}
%description -n %{name}-%{sname}

%package -n %{name}-devel
Summary: Development package for %name

%description devel
This package provides header files and libraries for %name.

%prep
%setup
sed -i 's|lib/|lib64/|g' Makefile

%build
export PATH=$PATH:%{_libdir}/qt5/bin
%{__make}

%install
%make_install
mkdir -p %{?buildroot}/usr/lib/modules-load.d/
mkdir -p %{buildroot}/usr/lib/systemd/system/
#mv %{?buildroot}%{_libdir}/modules-load.d/anything.conf %{?buildroot}/usr/lib/modules-load.d/
install -Dm644 server/tool/deepin-anything-tool.service %{buildroot}/usr/lib/systemd/system/
install -Dm644 server/monitor/deepin-anything-monitor.service %{buildroot}/usr/lib/systemd/system/

%files -n  %{name}-%{dname}
%exclude /usr/lib64/modules-load.d/anything.conf
%exclude /lib/systemd/system/deepin-anything-monitor.service
%exclude /lib/systemd/system/deepin-anything-tool.service
%exclude /usr/lib/sysusers.d/deepin-anything-monitor.conf
%{_usrsrc}/deepin-anything-0.0/*

%files -n  %{name}-%{lname}
%{_libdir}/libanything.so.*

%files -n  %{name}-%{sname}
%{_bindir}/deepin-anything-monitor
%{_bindir}/deepin-anything-tool
%{_bindir}/deepin-anything-tool-ionice
%{_libdir}/libdeepin-anything-server-lib.so.*
%{_datadir}/dbus-1/interfaces/com.deepin.anything.xml
%{_datadir}/dbus-1/system-services/com.deepin.anything.service
%{_sysconfdir}/dbus-1/system.d/com.deepin.anything.conf
/usr/lib/systemd/system/deepin-anything-monitor.service
/usr/lib/systemd/system/deepin-anything-tool.service

%files devel
%{_libdir}/libanything.so
%{_libdir}/libdeepin-anything-server-lib.so
%dir %{_includedir}/%{name}*/
%dir %{_includedir}/%name/index/
%{_includedir}/%{name}*/*.h
%{_includedir}/%name/index/*.h
%{_libdir}/pkgconfig/deepin-anything-server-lib.pc

%changelog
* Wed Mar 17 2021 uoser <uoser@uniontech.com> - 5.0.1-2
- add devel 

* Wed Feb 07 2018 TagBuilder <tagbuilder@linuxdeepin.com> - 5.0.1-1
- Project init.
