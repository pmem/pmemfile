Name:		pmemfile
Version:	0.1
Release:	0%{?dist}
Summary:	Persistent memory-backed, userspace implementation of POSIX-like API.
License:	BSD
URL:		http://github.com/marcinslusarz/pmemfile
Source0:	pmemfile-%{version}.tar.gz
#Source0:	https://github.com/marcinslusarz/pmemfile/archive/%{version}.tar.gz#/pmemfile-%{version}.tar.gz

BuildRequires:	glibc-devel
BuildRequires:	cmake
BuildRequires:	pkgconfig
BuildRequires:	libpmemobj-devel
BuildRequires:	libsyscall_intercept-devel

#ExclusiveArch: x86_64

%description
XXX

%package -n libpmemfile-posix
Summary: Persistent memory-backed, userspace implementation of POSIX-like API
Group: System Environment/Libraries
%description -n libpmemfile-posix
XXX

%files -n libpmemfile-posix
%defattr(-,root,root,-)
%{_libdir}/libpmemfile-posix.so.*
%license LICENSE
%doc README.md

%package -n libpmemfile-posix-devel
Summary: Development files for libpmemfile-posix library
Group: Development/Libraries
Requires: libpmemfile-posix = %{version}-%{release}
%description -n libpmemfile-posix-devel
Development files for libpmemfile-posix library

%files -n libpmemfile-posix-devel
%defattr(-,root,root,-)
%{_libdir}/libpmemfile-posix.so
%{_libdir}/libpmemfile-posix.a
%{_libdir}/pkgconfig/libpmemfile-posix.pc
%{_includedir}/libpmemfile-posix.h
%{_includedir}/libpmemfile-posix-stubs.h
%{_mandir}/man3/libpmemfile-posix.3.gz
%license LICENSE
%doc

%package -n libpmemfile
Summary: Persistent memory-backed, transaparent (LD_PRELOAD) userspace implementation of POSIX-like API
Group: System Environment/Libraries
%description -n libpmemfile
XXX

%files -n libpmemfile
%defattr(-,root,root,-)
%{_libdir}/libpmemfile.so.*
%{_libdir}/libpmemfile.so
%{_mandir}/man1/libpmemfile.1.gz
%license LICENSE
%doc

%package -n pmemfile-tools
Summary: Tools for pmemfile
Group: System Environment/Libraries
%description -n pmemfile-tools
XXX

%files -n pmemfile-tools
%defattr(-,root,root,-)
%{_bindir}/mkfs.pmemfile
%{_mandir}/man1/mkfs.pmemfile.1.gz
%license LICENSE
%doc

%prep
%setup -q -n %{name}-%{version}

%build
mkdir build && cd build
%cmake .. -DCMAKE_BUILD_TYPE=Release -DTESTS_USE_FORCED_PMEM=1
make %{?_smp_mflags}

%install
cd build
make install DESTDIR=%{buildroot}

%check
cd build
ctest -V %{?_smp_mflags} --output-on-failure

%post   -n libpmemfile-posix -p /sbin/ldconfig
%postun -n libpmemfile-posix -p /sbin/ldconfig

%changelog
* Tue Feb 14 2017 Marcin Ślusarz <marcin.slusarz@intel.com> - 0.1-1
- Initial RPM release
