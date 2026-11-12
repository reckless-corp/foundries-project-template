#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

builddir="${PWD}/.build"
export KAS_WORK_DIR="${KAS_WORK_DIR:-${builddir}/work}"
export DL_DIR="${DL_DIR:-${builddir}/downloads}"
export SSTATE_DIR="${SSTATE_DIR:-${builddir}/sstate-cache}"
mkdir -p "${DL_DIR}" "${SSTATE_DIR}" "${KAS_WORK_DIR}"

if (( $# < 1 || $# > 2 )); then
    echo "Usage: $0 <kas-file.yml> [action]"
    echo "Available kas files:" $(ls *.yml)
    exit 2
fi

KAS_CONTAINER="${builddir}/kas-container-5.5"
if [ ! -x "${KAS_CONTAINER}" ] ; then
    echo "Downloading kas-container to ${KAS_CONTAINER}"
    wget -O ${KAS_CONTAINER} https://raw.githubusercontent.com/siemens/kas/refs/tags/5.5/kas-container
    chmod +x "${KAS_CONTAINER}"
fi

kasfile="$1"
action="${2:-build}"

if [ ! -f "${kasfile}" ] ; then
	echo "ERROR: No such file ${kasfile}"
	exit 1
fi

set -x
exec sh "${KAS_CONTAINER}" "${action}" "${kasfile}"
