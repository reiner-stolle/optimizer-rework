#!/usr/bin/env bash
set -euo pipefail

PROTO_SRC_DIR="proto"
CPP_OUT_DIR="cpp/generated"
PY_OUT_DIR="python/generated"     # Python package directory

rm -rf "$CPP_OUT_DIR" "$PY_OUT_DIR"
mkdir -p "$CPP_OUT_DIR" "$PY_OUT_DIR"

# Generate C++ and Python from all .proto under proto/
# (No gRPC; just plain protobuf)
while IFS= read -r -d '' f; do
  protoc -I"$PROTO_SRC_DIR" --cpp_out="$CPP_OUT_DIR" --python_out="$PY_OUT_DIR" "$f"
done < <(find "$PROTO_SRC_DIR" -name '*.proto' -print0)

# Make headers available via include/ and sources via src/ for convenience
mkdir -p "$CPP_OUT_DIR"/include "$CPP_OUT_DIR"/src
find "$CPP_OUT_DIR" -name '*.pb.h' -exec mv {} "$CPP_OUT_DIR"/include/ \;
find "$CPP_OUT_DIR" -name '*.pb.cc' -exec mv {} "$CPP_OUT_DIR"/src/ \;

echo "Regeneration complete."
