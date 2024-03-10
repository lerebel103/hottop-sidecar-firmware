#!/usr/bin/env sh
# Sign the specified firmware file and saves it to the destination file, using the specified code signing profile.
# Usage: sign-firmware.sh <local-firmware-folder> <bucketArn> <destination-folder-key> <code-signing-profile>

set -e

# Check if the required parameters are provided
if [ "$#" -ne 4 ]; then
    echo "Usage: sign-firmware.sh  <firmware-folder> <bucket_name> <destination-folder-key> <code-signing-profile>"
    exit 1
fi

local_firmware_folder=$1
bucket_name=$(echo "$2" | sed -e "s/s3:\/\///g")
destination_folder_key=$3
code_signing_profile=$(echo "$4" | sed -e "s/.*\///g")

# extract firmware version
firmware_version=$(cat ./deploy/firwmare/firmware_version.txt)
hardware_revision=$(cat ./deploy/firwmare/hardware_revision.txt)
source_firmware_key=$destination_folder_key/$firmware_version/$hardware_revision/hottop-sidecar-firmware.bin
signed_firmware_key=$destination_folder_key/$firmware_version/$hardware_revision/signed/hottop-sidecar-firmware.bin

# First we copy the firmware files to the target folders
aws s3 cp -r $local_firmware_folder/* s3://$bucket_name/

# Sign the firmware file
#echo "Signing the firmware file..."
aws signer start-signing-job \
    --source "{\"s3\":{\"bucketName\":\"$bucket_name\", \"key\":\"$source_firmware_key\", \"version\":\"latest\"}}" \
    --destination "{\"s3\":{\"bucketName\":\"$bucket_name\", \"prefix\":\"$signed_firmware_key\"}}" \
    --profile-name "$code_signing_profile"