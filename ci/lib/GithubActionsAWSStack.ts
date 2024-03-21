import * as cdk from "aws-cdk-lib";
import { Construct } from "constructs";
import { aws_iam as iam } from "aws-cdk-lib";
import { Effect } from "aws-cdk-lib/aws-iam";

/**
 * Interface for the properties of GithubActionsAwsAuthCdkStackProps.
 * @property repositoryConfig - Array of GitHub repository configurations.
 * @property otaBucketArn - The ARN of the OTA bucket.
 */
export interface GithubActionsAwsAuthCdkStackProps extends cdk.StackProps {
  readonly repositoryConfig: { owner: string; repo: string; filter?: string }[];
  readonly otaBucketArn: string | undefined;
}

/**
 * Class representing building the resources required to connect to AWS from GH Actions.
 * @extends cdk.Stack
 */
export class GithubActionsAwsStack extends cdk.Stack {
  /**
   * Create the Stack.
   * @param {Construct} scope - The scope of the construct.
   * @param {string} id - The ID of the construct.
   * @param {GithubActionsAwsAuthCdkStackProps} props - The properties of the construct.
   */
  constructor(
    scope: Construct,
    id: string,
    props: GithubActionsAwsAuthCdkStackProps,
  ) {
    super(scope, id, props);

    const githubDomain = "https://token.actions.githubusercontent.com";

    // Create a new OpenID Connect provider for GitHub.
    const githubProvider = new iam.OpenIdConnectProvider(
      this,
      "GithubActionsProvider",
      {
        url: githubDomain,
        clientIds: ["sts.amazonaws.com"],
      },
    );

    // Get the repository deploy access.
    const iamRepoDeployAccess = props.repositoryConfig
      .map((r) => `repo:${r.owner}/${r.repo}:${r.filter ?? "*"}`)
      .pop();

    // Define the conditions for the IAM role.
    const conditions: iam.Conditions = {
      StringLike: {
        "token.actions.githubusercontent.com:sub": iamRepoDeployAccess,
      },
      StringEquals: {
        "token.actions.githubusercontent.com:aud": "sts.amazonaws.com",
      },
    };

    // Create a new IAM role.
    const role = new iam.Role(this, "gitHubDeployRole", {
      assumedBy: new iam.WebIdentityPrincipal(
        githubProvider.openIdConnectProviderArn,
        conditions,
      ),
      description:
        "This role is used via GitHub Actions to deploy with AWS CDK on the target AWS account",
      maxSessionDuration: cdk.Duration.hours(12),
    });

    // Add a policy to the IAM role to allow access to S3, so artefacts can be uploaded.
    role.addToPolicy(
      new iam.PolicyStatement({
        effect: Effect.ALLOW,
        actions: ["s3:PutObject"],
        resources: [
          `${props.otaBucketArn}/newBuilds`,
          `${props.otaBucketArn}/newBuilds/firmware.zip`,
        ],
      }),
    );

    // Create a new CloudFormation output.
    new cdk.CfnOutput(this, "GithubActionOidcIamRoleArn", {
      value: role.roleArn,
      description: `Arn for AWS IAM role with Github oidc auth for ${iamRepoDeployAccess}`,
      exportName: "GithubActionOidcIamRoleArn",
    });

    cdk.Tags.of(this).add("component", "CdkGithubActionsOidcIamRole");
  }
}
