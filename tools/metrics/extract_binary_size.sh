#!/bin/bash
# Extract binary size metrics for PeerTalk libraries
# Output: JSON to stdout

set -e

# Get library sizes (if they exist)
LIB_DIR="${LIB_DIR:-build/lib}"

LIBPEERTALK_SIZE=0
LIBPTLOG_SIZE=0
TOTAL_OBJ_SIZE=0
CORE_SIZE=0
POSIX_SIZE=0
LOG_SIZE=0

if [ -f "$LIB_DIR/libpeertalk.a" ]; then
    LIBPEERTALK_SIZE=$(stat -c%s "$LIB_DIR/libpeertalk.a" 2>/dev/null || stat -f%z "$LIB_DIR/libpeertalk.a" 2>/dev/null || echo "0")
fi

if [ -f "$LIB_DIR/libptlog.a" ]; then
    LIBPTLOG_SIZE=$(stat -c%s "$LIB_DIR/libptlog.a" 2>/dev/null || stat -f%z "$LIB_DIR/libptlog.a" 2>/dev/null || echo "0")
fi

# Sum object file sizes by category
OBJ_DIR="${OBJ_DIR:-build/obj}"

if [ -d "$OBJ_DIR/src/core" ]; then
    CORE_SIZE=$(find "$OBJ_DIR/src/core" -name "*.o" -exec stat -c%s {} \; 2>/dev/null | awk '{s+=$1} END {print s+0}' || \
                find "$OBJ_DIR/src/core" -name "*.o" -exec stat -f%z {} \; 2>/dev/null | awk '{s+=$1} END {print s+0}')
fi

if [ -d "$OBJ_DIR/src/posix" ]; then
    POSIX_SIZE=$(find "$OBJ_DIR/src/posix" -name "*.o" -exec stat -c%s {} \; 2>/dev/null | awk '{s+=$1} END {print s+0}' || \
                 find "$OBJ_DIR/src/posix" -name "*.o" -exec stat -f%z {} \; 2>/dev/null | awk '{s+=$1} END {print s+0}')
fi

if [ -d "$OBJ_DIR/src/log" ]; then
    LOG_SIZE=$(find "$OBJ_DIR/src/log" -name "*.o" -exec stat -c%s {} \; 2>/dev/null | awk '{s+=$1} END {print s+0}' || \
               find "$OBJ_DIR/src/log" -name "*.o" -exec stat -f%z {} \; 2>/dev/null | awk '{s+=$1} END {print s+0}')
fi

TOTAL_OBJ_SIZE=$((CORE_SIZE + POSIX_SIZE + LOG_SIZE))

# Count source files and lines
SRC_FILES=0
SRC_LINES=0
HEADER_FILES=0
HEADER_LINES=0

if [ -d "src" ]; then
    SRC_FILES=$(find src -name "*.c" 2>/dev/null | wc -l)
    SRC_LINES=$(find src -name "*.c" -exec cat {} \; 2>/dev/null | wc -l || echo "0")
fi

if [ -d "include" ]; then
    HEADER_FILES=$(find include -name "*.h" 2>/dev/null | wc -l)
    HEADER_LINES=$(find include -name "*.h" -exec cat {} \; 2>/dev/null | wc -l || echo "0")
fi

# Ensure clean integers
SRC_FILES=$(echo $SRC_FILES | awk '{print $1}')
SRC_LINES=$(echo $SRC_LINES | awk '{print $1}')
HEADER_FILES=$(echo $HEADER_FILES | awk '{print $1}')
HEADER_LINES=$(echo $HEADER_LINES | awk '{print $1}')

cat <<EOF
{
  "libpeertalk_bytes": $LIBPEERTALK_SIZE,
  "libptlog_bytes": $LIBPTLOG_SIZE,
  "total_lib_bytes": $((LIBPEERTALK_SIZE + LIBPTLOG_SIZE)),
  "core_obj_bytes": $CORE_SIZE,
  "posix_obj_bytes": $POSIX_SIZE,
  "log_obj_bytes": $LOG_SIZE,
  "total_obj_bytes": $TOTAL_OBJ_SIZE,
  "source_files": $SRC_FILES,
  "source_lines": $SRC_LINES,
  "header_files": $HEADER_FILES,
  "header_lines": $HEADER_LINES,
  "total_lines": $((SRC_LINES + HEADER_LINES))
}
EOF
