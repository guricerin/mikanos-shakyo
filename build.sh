#!/bin/bash 
set -e

readonly SCRIPT_ROOT=$(cd $(dirname ${0}); pwd)
readonly KERNEL_PATH=$SCRIPT_ROOT/kernel
readonly LOADER_PATH=$HOME/edk2
readonly OSBOOK_DEVENV_PATH=$HOME/osbook/devenv

build_loader() {
	cd $LOADER_PATH && source ./edksetup.sh && build
    cd $SCRIPT_ROOT
}

build_kernel() {
    make ${MAKE_OPTS:-} -C $KERNEL_PATH
}

build_apps() {
    for MK in $(ls ${SCRIPT_ROOT}/apps/*/Makefile); do
        local -r APP_DIR=$(dirname $MK)
        local -r APP=$(basename $APP_DIR)
        make ${MAKE_OPTS:-} -C $APP_DIR $APP
    done
}

main() {
    build_loader
    build_kernel
    build_apps

    if [ "${1:-}" = "run" ]; then  
        MIKANOS_DIR=$PWD $HOME/osbook/devenv/run_mikanos.sh
    fi
}

main $1