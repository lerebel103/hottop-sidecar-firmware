#!/usr/bin/env python3
import os

import aws_cdk as cdk

from hottopsidecar_pipeline.hottopsidecar_pipeline_stack import HottopsidecarPipelineStack

account = os.getenv('CDK_DEFAULT_ACCOUNT')
region = os.getenv('CDK_DEFAULT_REGION')
aws_region = os.getenv('AWS_DEFAULT_REGION')

if account is None or len(account) == 0:
    raise ValueError('CDK_DEFAULT_ACCOUNT is not set')
if region is None or len(region) == 0:
    raise ValueError('CDK_DEFAULT_REGION is not set')
if aws_region is None or (aws_region) == 0:
    raise ValueError('AWS_DEFAULT_REGION is not set')

app = cdk.App()
HottopsidecarPipelineStack(app, "hottopsidecar-pipeline-stack",
                           env=cdk.Environment(account=account, region=region))

app.synth()
