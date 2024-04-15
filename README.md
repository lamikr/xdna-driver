# AMD XDNA™️ Driver for Linux®️
This repository is for the AMD XDNA™️ Driver (amdxdna.ko) for Linux®️ and XRT SHIM library development.

## Table of Contents
- [Introduction](#introduction)
- [System Requirements](#system-requirements)
- [Linux compilation and installation](#linux-compilation-and-installation)
- [Clone](#clone)
- [Build](#build)
- [Test](#test)
- [Q&A](#qa)
- [Contributor Guidelines](#contributor-guidelines)

## Introduction
This repository is for supporting XRT on AMD XDNA devices. From this repository, you can build a XRT plugin DEB package.
On a machine with XDNA device, with both XRT and XRT plugin packages installed, user can start using XDNA device on Linux.

## System Requirements
To run AI applications, your system needs
* Processor:
  - To run AI applications (test machine): RyzenAI processor
  - To build this repository (build machine): Any x86 processors, but recommend AMD processor :wink:
* Operating System: Ubuntu >= 22.04
* Linux Kernel: v6.8 with IOMMU SVA support (see below)
* Installed XRT base package (or you can install it along the
  following recipe)
  - To make sure the XRT base package works with the plug-in package, better build it from `xrt` submodule in this repo (`<root-of-source-tree>/xrt`)
  - Refer to https://github.com/Xilinx/XRT for more detailed information.

*Important*: IOMMU SVA in Linux kernel support is required.

## Linux compilation and installation

You need to manually build 6.8 Linux kernel packages by following below steps.

The 6.8 Linux kernel with SVA source code can be downloaded from _v6.8-iommu-sva-part4-v7_ on https://github.com/AMD-SW/linux
``` bash
# Assuming you have knowledge of kernel compilation,
# this is just refreshing up a few key points.

git clone --depth=1 --branch v6.8-iommu-sva-part4-v7 git@github.com:AMD-SW/linux
cd linux

# Usually, when people compile kernel from source code, they use current config.
cp /boot/config-`uname -r` <your_build_dir>/.config   # (Option step, if you know how to do it better)
# Open <your_build_dir>/.config and add "CONFIG_DRM_ACCEL=y" #Required by XDNA Driver
# Or run instead
scripts/config --file .config --enable DRM_ACCEL

# Otherwise if you do not have a `.config` file modern enough, you can
# get one for Debian/Ubuntu with
https://gist.github.com/keryell/a0d0c020f81128d0f0071f16c9022000/raw/069d79a53fd20193ea4c9fa469d84ffc334229bb/.config

# Use below command to build kernel packages. Once build is done, DEB packages are at the parent directory of <your_build_dir>
make -j `nproc` bindeb-pkg
# The exact names will depend on your configuration
sudo apt reinstall ../linux-headers-6.8.5+iommu-sva-part4-v7+_6.8.5-00095-g88132f705404-2_amd64.deb ../linux-image-6.8.5+iommu-sva-part4-v7+_6.8.5-00095-g88132f705404-2_amd64.deb ../linux-libc-dev_6.8.5-00095-g88132f705404-2_amd64.deb
```

## Clone

```
git clone git@github.com:amd/xdna-driver.git
cd <root-of-source-tree>
# get code for submodules
git submodule update --init --recursive
```

## Build

### Prerequisite

* If this is your first time building this module,
  follow below steps to resolve the dependencies or at least look at
  the file content
``` bash
#requires root permissions to run the script
sudo su
cd <root-of-source-tree>
./tools/amdxdna_deps.sh
# exit from root
exit
```

### Steps to create release build DEB package:

``` bash
cd <root-of-source-tree>/build

# If you do not have XRT installed yet:
cd xrt/build
./build.sh
# To adapt according to your OS & version
sudo apt reinstall ./Release/xrt_202410.2.17.0_23.10-amd64-xrt.deb ./Release/xrt_202410.2.17.0_23.10-amd64-xbflash2.deb
cd ../../build

# Start XDNA driver release build
./build.sh -release

# Create DEB package for existed release or debug build.
./build.sh -package
# To adapt according to your OS & version
sudo apt reinstall ./Release/xrt_plugin.2.17.0_ubuntu23.10-x86_64-amdxdna.deb
```
You will find `xrt_plugin\*-amdxdna.deb` in Release/ folder. This package includes:
* The `.so` library files, which will be installed into `/opt/xilinx/xrt/lib` folder
* The XDNA driver and DKMS script, which build, install and load
  `amdxdna.ko` driver when installing the .DEB package on target machine
* The firmware binary files, which will be installed to `/usr/lib/firmware/amdnpu` folder

## Test

If you haven't read [System Requirements](#system-requirements), double check it.

``` bash
source /opt/xilinx/xrt/setup.sh
cd <root-of-source-tree>/build

# Build the test program
./build.sh -example

# Run the test
./example_build/example_noop_test ../tools/bins/1502_00/validate.xclbin
```

## Q&A

### Q: I want to debug my application, how to build library with `-g`?

A: We have debug version of library, which is compiled with `-g` option. You can run `./build.sh -debug` or `./build.sh`.
To create a debug DEB package, run `./build.sh -package` afterward.

### Q: I'm developing amdxdna.ko driver module. How to enable XDNA_DBG() print?

A: XDNA_DBG() relies on Linux's CONFIG_DYNAMIC_DEBUG framework, see Linux's [dynamic debug howto page](https://www.kernel.org/doc/html/v6.8/admin-guide/dynamic-debug-howto.html) for details.
TL;DR, run `sudo insmod amdxdna.ko dyndbg=+pf` to enable XDNA_DBG() globally, where +pf means enable debug printing and print the function name.

### Q: When install XRT plugin DEB package, apt-get/dpkg tool failed. What to do next?

A: Create a debug DEB package, see above question. Then install debug DEB package in your environment. This time, you will have more verbose log. Share this log with us.

## Contributor Guidelines
1. Read [Getting Started](#getting-started)
2. Read [System Requirements](#system-requirements)
3. Run Linux checkpatch.pl before commit and create pull request, see [Checkpatch](#checkpatch)

### Checkpatch
There is a pre-commit script for this purpose.
``` bash
cp amd-aie/tools/pre-commit <root-of-source-tree>/.git/hooks/
```
`git commit` will reject the commit if error/warning is found, until you make `checkpatch.pl` happy.
