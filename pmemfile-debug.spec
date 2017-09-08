Name:		pmemfile
Version:	0.1
Release:	0%{?dist}
Summary:	Persistent memory-backed, userspace implementation of POSIX-like API.
License:	BSD
URL:		http://github.com/pmem/pmemfile
Source0:	pmemfile-%{version}.tar.gz
#Source0:	https://github.com/pmem/pmemfile/archive/%{version}.tar.gz#/pmemfile-%{version}.tar.gz

BuildRequires:	glibc-devel
BuildRequires:	cmake
BuildRequires:	pkgconfig
BuildRequires:	libpmemobj-devel
BuildRequires:	libsyscall_intercept-devel

#ExclusiveArch: x86_64

%description
XXX

%package -n libpmemfile-posix-debug
Summary: Persistent memory-backed, userspace implementation of POSIX-like API
Group: System Environment/Libraries
%description -n libpmemfile-posix-debug
XXX

%files -n libpmemfile-posix-debug
%defattr(-,root,root,-)
%{_libdir}/pmemfile_debug/libpmemfile-posix.so.*
%{_libdir}/pmemfile_debug/libpmemfile-posix.so
%{_libdir}/pmemfile_debug/libpmemfile-posix.a
%license LICENSE

%package -n libpmemfile-debug
Summary: Persistent memory-backed, transaparent (LD_PRELOAD) userspace implementation of POSIX-like API
Group: System Environment/Libraries
%description -n libpmemfile-debug
XXX

%files -n libpmemfile-debug
%defattr(-,root,root,-)
%{_libdir}/pmemfile_debug/libpmemfile.so.*
%{_libdir}/pmemfile_debug/libpmemfile.so
%license LICENSE
%doc

%prep
%setup -q -n %{name}-%{version}

%build
mkdir dbg_build && cd dbg_build
%cmake .. -DCMAKE_BUILD_TYPE=Debug -DTESTS_USE_FORCED_PMEM=1
make %{?_smp_mflags}

%install
cd dbg_build
make install DESTDIR=%{buildroot}

%check
cd dbg_build
ctest %{?_smp_mflags} -E preload_unix --output-on-failure

%changelog
* Tue Feb 14 2017 Marcin Ślusarz <marcin.slusarz@intel.com> - 0.1-1
- Initial RPM release
