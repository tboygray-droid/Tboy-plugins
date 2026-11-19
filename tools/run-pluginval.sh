#!/usr/bin/env bash
# Runs pluginval on the built VST3.
#   PLUGINVAL=/path/to/pluginval  [VST3_VALIDATOR=/path/to/vst3sdk/validator]  tools/run-pluginval.sh [strictness] [build-dir]
# On headless Linux, wrap with:  xvfb-run -a -s "-screen 0 1920x1080x24" tools/run-pluginval.sh
set -euo pipefail
STRICTNESS="${1:-5}"
BUILD_DIR="${2:-build}"
PLUGINVAL="${PLUGINVAL:-pluginval}"
PLUGIN="$(find "$BUILD_DIR" -type d -name 'TgsoundsEQ.vst3' | head -n1)"
[[ -n "$PLUGIN" ]] || { echo "TgsoundsEQ.vst3 not found under $BUILD_DIR" >&2; exit 1; }

ARGS=(--strictness-level "$STRICTNESS" --validate-in-process --timeout-ms 900000)
if [[ -n "${VST3_VALIDATOR:-}" ]]; then
  # pluginval 1.0.x hands the validator ".../Foo.vst3/Contents/<arch>/Foo.so", but VST3 SDK 3.7+
  # validators only accept the bundle directory. This shim maps the path back to the bundle.
  SHIM="$(mktemp)"
  cat > "$SHIM" <<SH
#!/usr/bin/env bash
args=(); for a in "\$@"; do [[ "\$a" == *.vst3/Contents/*/*.so ]] && a="\${a%%.vst3/*}.vst3"; args+=("\$a"); done
exec "$VST3_VALIDATOR" "\${args[@]}"
SH
  chmod +x "$SHIM"
  ARGS+=(--vst3validator "$SHIM")
fi
exec "$PLUGINVAL" "${ARGS[@]}" "$PLUGIN"
