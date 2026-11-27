#!/bin/sh
# check_sdk.sh — Xcode build phase (app + AUv3). App Store / TestFlight builds
# of Vektur Modular must be built with the iOS 27 / macOS 27 SDK or later
# (Xcode 27+). Release builds (archives) fail below that; Debug only warns.
# The deployment target (minimum OS, iOS 17 / macOS 14) is unaffected.
MIN=${VK_MIN_SDK_MAJOR:-27}
major=$(printf '%s' "${SDK_VERSION:-0}" | cut -d. -f1)
if [ "$major" -lt "$MIN" ]; then
  msg="Built with the ${PLATFORM_NAME} ${SDK_VERSION} SDK (Xcode ${XCODE_PRODUCT_BUILD_VERSION:-?}). TestFlight builds need the ${MIN} SDK or later: install Xcode ${MIN} or newer."
  if [ "${CONFIGURATION}" = "Release" ]; then echo "error: $msg"; exit 1; else echo "warning: $msg"; fi
fi
echo "SDK check: ${PLATFORM_NAME} ${SDK_VERSION} (minimum ${MIN}) — ok"
