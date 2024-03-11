#!/usr/bin/env sh
# Sign the specified firmware file and saves it to the destination file, using the specified code signing profile.
# Usage: sign-firmware.sh <local-deploy-folder> <bucketArn> <destination-folder-key> <code-signing-profile>

set -e

# Check if the required parameters are provided
if [ "$#" -ne 4 ]; then
    echo "Usage: sign-firmware.sh  <firmware-folder> <bucket_name> <destination-folder-key> <code-signing-profile>"
    exit 1
fi

local_deploy_folder=$1
bucket_name=$(echo "$2" | sed -e "s/s3:\/\///g")
destination_folder_key=$3
code_signing_profile=$(echo "$4" | sed -e "s/.*\///g")

# extract firmware version
firmware_version=$(cat $local_deploy_folder/firmware/firmware_version.txt)
hardware_revision=$(cat $local_deploy_folder/firmware/hardware_revision.txt)
root_firmware_key=$destination_folder_key/$firmware_version/$hardware_revision/
source_firmware_key=$root_firmware_key/hottop-sidecar-firmware.bin
signed_firmware_key=$root_firmware_key/signed/hottop-sidecar-firmware.bin

# First we copy the firmware files to the target folders
aws s3 cp --recursive "$local_deploy_folder/firmware/" "s3://$bucket_name/$root_firmware_key"

# Sign the firmware file
#echo "Signing the firmware file..."
aws signer start-signing-job \
    --source "{\"s3\":{\"bucketName\":\"$bucket_name\", \"key\":\"$source_firmware_key\", \"version\":\"latest\"}}" \
    --destination "{\"s3\":{\"bucketName\":\"$bucket_name\", \"prefix\":\"$signed_firmware_key\"}}" \
    --profile-name "$code_signing_profile"
