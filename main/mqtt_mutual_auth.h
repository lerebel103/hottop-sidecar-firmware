#pragma once

struct identity_t {
  char* stage_name;
  char* thing_type;
  char* ats_ep;
  char* jobs_ep;
  char* ca_cert;
  char* prov_cert;
  char* prov_private_key;
  char* device_cert;
  char* device_private_key;
};

int aws_iot(identity_t identity);
