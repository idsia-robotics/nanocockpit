#!/bin/bash

# Exit at the first error
set -e

# Refreshing the Git index is needed to use git-apply
git update-index --refresh

CURRENT_ARCH=$(dpkg --print-architecture)

for PATCH in /tmp/patches/gapsdk/*.patch; do
    # Check whether this patch should be applied in the current configuration
    PATCH_TAG="$(echo $PATCH | sed -n 's/.*@\(.*\).patch/\1/p')"

    if [[ $PATCH_TAG == "all" ]]; then
        echo Patch file \'$PATCH\' always applied
    elif [[ $PATCH_TAG == $CURRENT_ARCH ]]; then
        echo Patch file \'$PATCH\' applied for architecture \'$CURRENT_ARCH\'
    elif dpkg --compare-versions $GAP_SDK_VERSION le $PATCH_TAG 2> /dev/null; then
        echo Patch file \'$PATCH\' applied for GAP SDK version \'$GAP_SDK_VERSION\'
    else
        # Skipping
        continue
    fi
    
    git apply -3 "$PATCH"
done

# Update config.{guess,sub} scripts for aarch64 support
if [[ $CURRENT_ARCH == "arm64" ]]; then
    wget 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.guess;hb=f992bcc08219edb283d2ab31dd3871a4a0e8220e' -O tools/gap8-openocd/jimtcl/autosetup/config.guess && \
    wget 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.sub;hb=f992bcc08219edb283d2ab31dd3871a4a0e8220e' -O tools/gap8-openocd/jimtcl/autosetup/config.sub
fi
