%bcond_with check
%global _unpackaged_files_terminate_build 0 
%global debug_package   %{nil}
%global common_description %{expand: Something like everything, but nothing is really like anything...}
%global dname dkms
%global lname libs
%global sname server

Name:          deepin-anything
Version:       6.0.7
Release:       1
Summary:       Something like everything, but nothing is really like anything...
License:       GPLv3
URL:           https://github.com/linuxdeepin/deepin-anything
Source0:       %{url}/archive/refs/tags/%{version}.tar.gz


BuildRequires: qt5-qtbase-devel
BuildRequires: udisks2-qt5
BuildRequires: udisks2-qt5-devel
BuildRequires: libmount
BuildRequires: libmount-devel
BuildRequires: pcre-devel libnl3-devel


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
sed -i 's|lib/$(DEB_HOST_MULTIARCH)|lib64/|g' src/Makefile

%build
export PATH=$PATH:%{_libdir}/qt5/bin
%{__make}

%install
%make_install
mkdir -p %{?buildroot}/usr/lib/modules-load.d/

%files -n  %{name}-%{dname}
%exclude /usr/lib/modules-load.d/anything.conf
%{_usrsrc}/deepin-anything-0.0/*

%files -n  %{name}-%{lname}
%{_libdir}/libanything.so.*

%files -n  %{name}-%{sname}
%{_libdir}/libdeepin-anything-server-lib.so.*
%{_datadir}/dbus-1/interfaces/com.deepin.anything.xml
%{_sysconfdir}/dbus-1/system.d/com.deepin.anything.conf

%files devel
%{_libdir}/libanything.so
%{_libdir}/libdeepin-anything-server-lib.so
%dir %{_includedir}/%{name}*/
%dir %{_includedir}/%name/index/
%{_includedir}/%{name}*/*.h
%{_includedir}/%name/index/*.h
%{_libdir}/pkgconfig/deepin-anything-server-lib.pc

%changelog
* Mon Apr 24 2023 uoser <uoser@uniontech.com> - 6.0.7-1
- new struct and remove tool&monitor service.

* Wed Mar 17 2021 uoser <uoser@uniontech.com> - 5.0.1-2
- add devel 

* Wed Feb 07 2018 TagBuilder <tagbuilder@linuxdeepin.com> - 5.0.1-1
- Project init.
