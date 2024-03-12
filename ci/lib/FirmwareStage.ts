import { Stage, StageProps } from "aws-cdk-lib";
import { Construct } from "constructs";
import { FirmwareDeployStack } from "./FirmwareDeployStack";
import { GithubActionsAwsStack } from "./GithubActionsAWSStack";

export class FirmwareStage extends Stage {
  constructor(scope: Construct, id: string, props: StageProps) {
    super(scope, id, props);

    const fwResources = new FirmwareDeployStack(this, "build-stack", props);

    // Install GitHub Actions OIDC auth
    const ghProps = {
      repositoryConfig: [
        { owner: "lerebel103", repo: "hottop-sidecar-firmware" },
      ],
      otaBucketArn: fwResources.otaBucketArn,
    };
    new GithubActionsAwsStack(this, "auth-stack", ghProps);
  }
}
