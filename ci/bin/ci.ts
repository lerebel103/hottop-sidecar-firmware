#!/usr/bin/env node
import * as cdk from "aws-cdk-lib";
import { CIStack } from "../lib/CIStack";

const app = new cdk.App();

new CIStack(app, "hottopsidecar-pipeline", {
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: process.env.CDK_DEFAULT_REGION,
  },
});
