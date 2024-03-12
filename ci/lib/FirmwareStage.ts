import { Stage, StageProps } from "aws-cdk-lib";
import { Construct } from "constructs";
import { FirmwareDeployStack } from "./FirmwareDeployStack";
import { GithubActionsAwsStack } from "./GithubActionsAWSStack";

export interface FirmwareStageProps extends StageProps {
  readonly owner: string;
  readonly repo: string;
}

export class FirmwareStage extends Stage {
  constructor(scope: Construct, id: string, props: FirmwareStageProps) {
    super(scope, id, props);

    const fwResources = new FirmwareDeployStack(this, "build-stack", props);

    // Install GitHub Actions OIDC auth
    const ghProps = {
      repositoryConfig: [{ owner: props.owner, repo: props.repo }],
      otaBucketArn: fwResources.otaBucketArn,
    };
    new GithubActionsAwsStack(this, "auth-stack", ghProps);
  }
}
