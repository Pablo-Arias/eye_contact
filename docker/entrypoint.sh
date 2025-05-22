#!/usr/bin/env bash
set -e
if [[ "$1" == "" ]]; then
  echo "Usage: docker run … <gst-launch-1.0 pipeline …>"
  echo "Example: gst-launch-1.0 videotestsrc ! eyecontact ! autovideosink"
  exit 1
fi
exec "$@"