import * as cdk from 'aws-cdk-lib'
import { Construct } from 'constructs'
import {aws_iam, aws_iam as iam} from 'aws-cdk-lib'
import {Globals} from "./globals";
import {Effect} from "aws-cdk-lib/aws-iam";

export interface GithubActionsAwsAuthCdkStackProps extends cdk.StackProps {
    readonly repositoryConfig: { owner: string; repo: string; filter?: string }[];
    readonly otaBucketArn: string | undefined;
}

export class GithubActionsAwsStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props: GithubActionsAwsAuthCdkStackProps) {
        super(scope, id, props);

        const githubDomain = 'https://token.actions.githubusercontent.com';

        const githubProvider = new iam.OpenIdConnectProvider(this, 'GithubActionsProvider', {
            url: githubDomain,
            clientIds: ['sts.amazonaws.com'],
        });

        const iamRepoDeployAccess = props.repositoryConfig.map(
            r => `repo:${r.owner}/${r.repo}:environment:${Globals.STAGE_NAME}:${r.filter ?? '*'}`
        ).pop();

        const conditions: iam.Conditions = {
            StringEquals: {
                'token.actions.githubusercontent.com:sub': iamRepoDeployAccess,
                'token.actions.githubusercontent.com:aud': 'sts.amazonaws.com'
            },
        }

        const role = new iam.Role(this, 'gitHubDeployRole', {
            assumedBy: new iam.WebIdentityPrincipal(githubProvider.openIdConnectProviderArn, conditions),
            description: 'This role is used via GitHub Actions to deploy with AWS CDK or Terraform on the target AWS account',
            maxSessionDuration: cdk.Duration.hours(12),
        });

        role.addToPolicy(new aws_iam.PolicyStatement({
            effect: Effect.ALLOW,
            actions: ['s3:PutObject'],
            resources: [`${props.otaBucketArn}/newBuilds`, `${props.otaBucketArn}/newBuilds/firmware.zip`]
        }));

        new cdk.CfnOutput(this, 'GithubActionOidcIamRoleArn', {
            value: role.roleArn,
            description: `Arn for AWS IAM role with Github oidc auth for ${iamRepoDeployAccess}`,
            exportName: 'GithubActionOidcIamRoleArn',
        });

        cdk.Tags.of(this).add('component', 'CdkGithubActionsOidcIamRole');
    }
}