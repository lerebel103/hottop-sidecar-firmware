#!/usr/bin/env node
import 'source-map-support/register';
import * as cdk from 'aws-cdk-lib';
import { CIStack } from '../lib/CIStack';
import {Globals} from "../lib/globals";

const app = new cdk.App();

new CIStack(app, 'hottopsidecar-pipeline', {
  env: { account: Globals.AWS_ACCOUNT, region: Globals.AWS_REGION },
});