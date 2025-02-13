# Lixom

This repository contains proof-of-concept code for Lixom, a technique leveraging Execute-Only Memory (XOM) on x86_64 to protect cryptographic secrets from unauthorized access.
For detailed information, please refer to the paper _"Lixom: Protecting Encryption Keys with Execute-Only Memory"_ by Hornetz, Gerlach and Schwarz.

### Structure of this Repository
This repository contains the following sub-components:
* `xen` - A patched version of the [Xen hypervisor](https://xenproject.org/projects/hypervisor/), which allows HVM guests to set up execute-only segments, and implements the register clearing mechanism described in the paper.
* `modxom` - A Linux kernel module that enables user mode programs to interface with the Xen patches.
* `libxom` - A user-mode C library that provides a simple API for setting up and tearing down execute-only ranges. If used without the Xen patches or outside a virtual machine, it will fall back to using protection keys to enforce XOM.
* `openssl-provider` - An [OpenSSL provider library](https://docs.openssl.org/3.4/man7/provider/) that implements various cryptographic algorithms in a way that uses Lixom's protection mechanisms.
* `demos` - A series of demo programs that use the above components.

## Configurations

Lixom can be used in one of two configurations:

* **Lixom (with EPT)**: This mode provides the strongest security guarantees. It relies on our hypervisor patches and is only available in hardware-accelerated VMs of our modified Xen hypervisor. Furthermore, you will need an Intel CPU for this. AMD is not supported, as their CPUs do not implement Extended Page Tables (EPT), which our patches heavily rely on.
* **Lixom-Light (with MPK)**: This mode only requires Memory Protection Keys, which are widely supported on recent CPUs (check whether `lscpu` reports the `pku` capability flag in Linux to confirm that your CPU supports it). It is easier to deploy, but does not provide the hypervisor-specific features of Lixom with EPT.

## Building and Setup
### Setting up the Xen hypervisor (only for Lixom with EPT)
The following is a short guide on how to set up the Xen hypervisor on Debian-based Linux distributions. For other build environments, see [Xen's official guide](https://wiki.xenproject.org/wiki/Compiling_Xen_From_Source). To prevent version conflicts or inconsistencies, you should use Debian 12 or Ubuntu 24.04 LTS.

First, install the build dependencies with
```shell
sudo apt-get update
sudo apt-get install ovmf libsystemd-dev
sudo apt-get build-dep xen edk2
```
Then, run the following commands in the `xen` sub-folder of this repository to build and install the hypervisor.
```shell
./configure --enable-ovmf --enable-systemd --disable-stubdom --disable-docs --prefix=/usr
make dist -j$(nproc)
cd dist
sudo ./install.sh
sudo update-grub
```

You should now be able to boot into Xen. Next, you must set up an HVM guest, as dom0 does not use hardware acceleration.
There is a nice guide on how to do this in the [Debian Wiki](https://wiki.debian.org/Xen/InstallDebianGuest).
If you have problems launching your VM, it may be useful to restrict the memory available to dom0 in the Xen command line.
Once you have a working HVM guest, proceed with building Lixom.

### Building Lixom
First, install Lixom's build dependencies. On Debian-based Linux distributions, you can do this with
```shell
sudo apt-get install libssl-dev libcurl-dev linux-headers-amd64 g++ cmake
```
Then, build Lixom by running the following in the repository folder
```shell
cmake -B build -S . && cmake --build build
```
You can now find the binaries in the `build` directory.
If you want to use EPT, make sure to load the kernel module with `insmod build/modxom/modxom.ko` before running the demos.

## Demos
* `demos/demo_libxom.c` - A small demo program showing how to use libxom.
* `demos/demo_https.c` - A demo program that uses the OpenSSL provider's AES implementation to download a web page with HTTPS.

Make sure that `libxom.so` and `libxom_provider.so` are in your working directory when launching the demos.

## Using the OpenSSL Provider Library
The OpenSSL provider implements AES-128-CTR, AES-128-GCM, and HMAC-SHA256.
You can either load this library explicitly in your code (as is done in `demos/demo_https.c`), or use an [OpenSSL configuration file](https://docs.openssl.org/3.4/man5/config/) to use it without modifying your code.
If you simply want to try the module out, you can also use it with the OpenSSL command line utilities.
For example, you can copy the `openssl-provider/openssl.conf` file into your build directory and run
```shell
OPENSSL_CONF=$(readlink -f openssl.conf) XMODULE=$(readlink -f libxom_provider.so) openssl dgst -sha256 -hmac "secret_key" <some file>
```
to generate an HMAC-SHA256 token for a file using the Lixom implementation.

## Warning
Lixom is intended as a research tool. You should not rely on it for security or use it in a production environment