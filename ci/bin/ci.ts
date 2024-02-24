#!/usr/bin/env node
import 'source-map-support/register';
import * as cdk from 'aws-cdk-lib';
import { CIStack } from '../lib/CIStack';

const app = new cdk.App();

/* Some sanity checks on target environment variables */
const account = process.env.CDK_DEFAULT_ACCOUNT;
if (account === undefined || account === '') {
  throw new Error('CDK_DEFAULT_ACCOUNT is not defined');
}

const region = process.env.CDK_DEFAULT_REGION;
if (region === undefined || region === '') {
    throw new Error('CDK_DEFAULT_REGION is not defined');
}

const default_region = process.env.AWS_DEFAULT_REGION;
if (default_region === undefined || default_region === '') {
    throw new Error('AWS_DEFAULT_REGION is not defined');
}

new CIStack(app, 'hottopsidecar-pipeline', {
  env: { account: account, region: region },
});