#!/usr/bin/env bash
# The version lives in three files; the Library Manager reads one, PlatformIO another, the firmware the third.
# With a tag name as $1 (v0.9.0-beta.1), the tag must name the same version.
set -euo pipefail
cd "$(dirname "$0")/.."

properties=$(sed -n 's/^version=//p' library.properties)
manifest=$(python3 -c 'import json; print(json.load(open("library.json"))["version"])')
firmware=$(sed -n 's/^#define WBSKT_SDK_VERSION "\(.*\)"$/\1/p' src/wbskt/Version.h)

echo "library.properties $properties"
echo "library.json       $manifest"
echo "Version.h          $firmware"

if [[ "$properties" != "$manifest" || "$properties" != "$firmware" ]]; then
  echo "::error::The three versions differ."
  exit 1
fi

if [[ $# -gt 0 && "$1" != "v$properties" ]]; then
  echo "::error::Tag $1 does not match version $properties (expected v$properties)."
  exit 1
fi
