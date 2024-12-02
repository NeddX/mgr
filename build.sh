#!/usr/bin/env bash

PRESET=linux-any-debug

panic() {
    local timestamp=$(date "+%Y/%m/%d %H:%M:%S")
    echo "[$timestamp] (Panic): $1"
    exit -1
}

log() {
   local timestamp=$(date "+%Y/%m/%d %H:%M:%S")
   echo "[$timestamp] (Info): $1"
}

cmake --preset=$PRESET || panic "Failed to generate cmake files."
cmake --build builds/$PRESET || panic "Failed to build."
cmake --install builds/$PRESET || panic "Failed to install."
