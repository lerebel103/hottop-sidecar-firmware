import { Stage, StageProps } from "aws-cdk-lib";
import { Construct } from "constructs";
import { FirmwareOtaStack } from "./OTA/FirmwareOtaStack";
import { GithubActionsAwsStack } from "./GithubActionsAWSStack";

/**
 * Interface for the properties of FirmwareStageProps.
 * @property owner - The owner of the repository.
 * @property repo - The repository name.
 */
export interface FirmwareStageProps extends StageProps {
  readonly owner: string;
  readonly repo: string;
}

/**
 * Class representing a Firmware Stage.
 * @extends Stage
 */
export class FirmwareDeployStage extends Stage {
  /**
   * Create a Firmware Stage.
   * @param {Construct} scope - The scope of the construct.
   * @param {string} id - The ID of the construct.
   * @param {FirmwareStageProps} props - The properties of the construct.
   */
  constructor(scope: Construct, id: string, props: FirmwareStageProps) {
    super(scope, id, props);

    // Create a new Firmware Deploy Stack.
    const fwResources = new FirmwareOtaStack(this, "build-stack", props);

    // Install GitHub Actions OIDC auth
    const ghProps = {
      repositoryConfig: [{ owner: props.owner, repo: props.repo }],
      otaBucketArn: fwResources.otaBucketArn,
    };

    // Create a new GitHub Actions AWS Stack.
    new GithubActionsAwsStack(this, "auth-stack", ghProps);
  }
}
