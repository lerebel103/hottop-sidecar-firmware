import { Stage, StageProps } from "aws-cdk-lib";
import { Construct } from "constructs";
import { FirmwareDeployStack } from "./FirmwareDeployStack";
import { GithubActionsAwsStack } from "./GithubActionsAWSStack";

export class FirmwareStage extends Stage {
  constructor(scope: Construct, id: string, props: StageProps) {
    super(scope, id, props);

    const fwResources = new FirmwareDeployStack(this, "build-stack", props);

    const owner = process.env.GH_OWNER || "";
    const repo = process.env.GH_REPOSITORY || "";
    if (owner.length == 0 || repo.length == 0) {
      throw new Error("GH_OWNER and GH_REPOSITORY must be set for desired GitHub connection.");
    }

    // Install GitHub Actions OIDC auth
    const ghProps = {
      repositoryConfig: [{ owner, repo }],
      otaBucketArn: fwResources.otaBucketArn,
    };
    new GithubActionsAwsStack(this, "auth-stack", ghProps);
  }
}
