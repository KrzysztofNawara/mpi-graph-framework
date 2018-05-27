#!/usr/bin/env bash

REL_DIR="$(dirname "${BASH_SOURCE[0]}")"
DIR="$(readlink -e $REL_DIR)"
MACHINE="prometheus.cyfronet.pl"
LOGIN_STR="plgblueeyedhush@$MACHINE"
REMOTE_DIR="ml-graphs/graphx/"

PASS=`cat $DIR/pass`

if [ ! -z "$1" ]; then
    echo "Creating directory"
    sshpass -p "$PASS" ssh "$LOGIN_STR" mkdir -p "$REMOTE_DIR"
fi

echo "Syncing files"
pushd "$DIR" &> /dev/null
sshpass -p "$PASS" rsync -avzr \
    target/scala-2.11/*.jar \
    "$LOGIN_STR":"$REMOTE_DIR"
popd &> /dev/null

