import {Stage, StageProps} from "aws-cdk-lib";
import {Construct} from 'constructs';
import {FirmwareDeployStack} from "./FirmwareDeployStack";
import {FileSet, IFileSetProducer} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";
import {CodePipelineSource} from "aws-cdk-lib/pipelines";
import {GithubActionsAwsStack, GithubActionsAwsAuthCdkStackProps} from "./GithubActionsAWSStack";

export class FirmwareStage extends Stage {
    constructor(scope: Construct, id: string, props: StageProps, sourceFiles: CodePipelineSource) {
        super(scope, id, props);

        const fwResources = new FirmwareDeployStack(this, 'build-stack', props, sourceFiles);

        // Install Github Actions OIDC auth
        const ghProps = {
            repositoryConfig: [
                {owner: 'lerebel103', repo: 'hottop-sidecar-firmware'}
            ],
            otaBucketArn: fwResources.otaBucketArn
        };
        new GithubActionsAwsStack(this, 'auth-stack', ghProps);
    }
}