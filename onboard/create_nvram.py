#!/usr/bin/env python3
import json
import os
import subprocess

import boto3
import random

import requests

iot_client = boto3.client('iot')
sm_client = boto3.client('secretsmanager')

ROOT_CERT_URL = 'https://www.amazontrust.com/repository/AmazonRootCA1.pem'
NVRAM_IN_FILE = 'nvram.in'
NVRAM_BIN_FILE = 'nvram.bin'
NVRAM_SIZE = str(512 * 1024)

env = 'dev'
policy_name = 'RebelHeaterPowah_certificate_provisioning_policy'
prov_template = 'RebelHeaterPowah_provisioning_templ'
rotate_template = 'RebelHeaterPowah_rotation_templ'


def generate_nvram_in(*, working_dir, stage, thing_type, prov_templ, rotate_templ, ats_endpoint, jobs_endpoint, ca_cert, provisioning_cert, private_key):
    print("Generating nvram")
    # First generate nvram
    #
    # Need to add provisioniong template name
    #
    with open(os.path.join(working_dir, NVRAM_IN_FILE), 'w') as f:
        f.write('key,type,encoding,value\n')
        f.write('identity,namespace,,\n')
        f.write('stage_name,data,string,{}\n'.format(stage))
        f.write('prov_templ,data,string,{}\n'.format(prov_templ))
        f.write('rotate_templ,data,string,{}\n'.format(rotate_templ))
        f.write('thing_type,data,string,{}\n'.format(thing_type))
        f.write('ats_ep,data,string,{}\n'.format(ats_endpoint))
        f.write('jobs_ep,data,string,{}\n'.format(jobs_endpoint))
        f.write(f'ca_cert,data,string,"{ca_cert}"\n')
        f.write(f'prov_cert,data,string,"{provisioning_cert}"\n')
        f.write(f'prov_key,data,string,"{private_key}"\n')


def generate_nvram_bin(working_dir):
    print("Generating NVRAM content")
    stage = 'dev'
    thing_type = 'HottopSidecar'

    # Get root CA
    ca_cert = requests.get(ROOT_CERT_URL).content.decode('utf-8')

    ats_endpoint = iot_client.describe_endpoint(endpointType='iot:Data-ATS')['endpointAddress'].strip()
    jobs_endpoint = iot_client.describe_endpoint(endpointType='iot:Jobs')['endpointAddress'].strip()

    principals = iot_client.list_policy_principals(policyName=policy_name)['principals']
    idx = 0
    if len(principals) > 1:
        idx = random.randrange(0, len(principals)-1)
    principal = principals[idx]
    principal_name = principal.split('/')[-1]

    # get the cert
    cert_info = iot_client.describe_certificate(certificateId=principal_name)
    provisioning_cert = cert_info['certificateDescription']['certificatePem']

    # now retrieve secrets for this principal
    secret = sm_client.get_secret_value(SecretId=f'{env}/iot/provisioning_claim_certificate/{principal_name}')
    secret_json = json.loads(secret['SecretString'])
    private_key = fix_b64_content(secret_json['private.pem.key'])
    # public_key = strip_b64_content(secret_json['public.pem.key'].split('-----')[2])

    generate_nvram_in(
        working_dir=working_dir,
        stage=stage,
        prov_templ=prov_template,
        rotate_templ=rotate_template,
        ats_endpoint=ats_endpoint,
        thing_type=thing_type,
        jobs_endpoint=jobs_endpoint,
        ca_cert=ca_cert,
        provisioning_cert=provisioning_cert,
        private_key=private_key)

    # Convert to bin file ready for flashing later
    generator = f"{os.environ['IDF_PATH']}/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
    subprocess.call([generator, "generate",
                     os.path.join(working_dir, NVRAM_IN_FILE),
                     os.path.join(working_dir, NVRAM_BIN_FILE),
                     NVRAM_SIZE])


def flash_nvram(working_dir):
    subprocess.call(["esptool.py", "write_flash", "0x00475000", os.path.join(working_dir, NVRAM_BIN_FILE)])


def fix_b64_content(cert):
    splits = cert.split('-----')
    preamb = f"-----BEGIN RSA PRIVATE KEY-----"
    content = splits[2].replace(' ', '\n')
    epilogue = f"-----END RSA PRIVATE KEY-----\n"
    return f"{preamb}{content}{epilogue}"


def main():
    print(f"Retrieving onboarding details {policy_name}")

    working_dir = "/tmp"
    # if not os.path.exists(os.path.join(working_dir, NVRAM_BIN_FILE)):
    generate_nvram_bin(working_dir)

    flash_nvram(working_dir)


if __name__ == "__main__":
    main()
