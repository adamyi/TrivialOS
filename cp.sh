set -e

TFTPROOT=/var/tftpboot/${USER}

echo "cp ${PWD}/images/sos-image-arm-odroidc2 ${TFTPROOT}"
cp ${PWD}/images/sos-image-arm-odroidc2 ${TFTPROOT}
echo "cp ${PWD}/apps/* ${TFTPROOT}"
cp ${PWD}/apps/* ${TFTPROOT}
