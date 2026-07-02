#!/usr/bin/env bash

#
# Upload the .tar.gz and .xml artifacts to cloudsmith
#

export PATH="${PATH}:/Users/distiller/.local/bin"

if [ -f ~/.config/local-build.rc ]; then source ~/.config/local-build.rc; fi

if [ -z "$CLOUDSMITH_API_KEY" ]; then
    echo 'Warning: $CLOUDSMITH_API_KEY is not available, giving up.'
    echo 'Metadata: [ncdf]-0.5-msvc-wx32-10.0.26200.xml'
    echo 'Tarball: [ncdf]-0.5.0.0+2606301034.db2f43e_msvc-wx32-10.0.26200-x86.tar.gz'
    echo 'Version: 0.5.0.0+2606301034.db2f43e'
    exit 0
fi

echo "Using CLOUDSMITH_API_KEY: ${CLOUDSMITH_API_KEY:0:4}..."

if [ -f ~/.uploadrc ]; then source ~/.uploadrc; fi
set -xe

cloudsmith push raw --no-wait-for-sync \
    --name [ncdf]-0.5-msvc-wx32-10.0.26200-metadata \
    --version 0.5.0.0+2606301034.db2f43e \
    --summary "Plugin metadata for automatic installation" \
    --republish \
    opencpn/ncdf-alpha [ncdf]-0.5-msvc-wx32-10.0.26200.xml

cloudsmith push raw --no-wait-for-sync \
    --name [ncdf]-0.5-msvc-wx32-10.0.26200-tarball \
    --version 0.5.0.0+2606301034.db2f43e \
    --summary "Plugin tarball for automatic installation" \
    --republish \
    opencpn/ncdf-alpha [ncdf]-0.5.0.0+2606301034.db2f43e_msvc-wx32-10.0.26200-x86.tar.gz
