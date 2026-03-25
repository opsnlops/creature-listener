#!/bin/bash
#
# Build the .deb package locally.
# Requires: cmake, ninja, debhelper, devscripts, fakeroot,
#           libcurl4-openssl-dev, libssl-dev, libasound2-dev
#

set -e
dpkg-buildpackage -us -uc -b
echo ""
echo "Package built. Look for .deb files in the parent directory:"
ls -la ../creature-listener_*.deb
