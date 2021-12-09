# Fail on any error.
set -e

# Trivial repackaging of the build artefacts: The collector gets integrated into Orbit.zip.
cd "$KOKORO_GFILE_DIR"
unzip Collector.zip
unzip OrbitUI.zip
zip -r Orbit.zip Orbit/
