import * as cdk from 'aws-cdk-lib';
import {Construct} from 'constructs';
import {CodeBuildStep, CodePipeline, CodePipelineSource} from "aws-cdk-lib/pipelines";
import {BuildFirmwareStage} from "./BuildFWStage";

/**
 * A stack for the CI/CD pipeline
 *
 */
export class CIStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props: cdk.StackProps) {
        super(scope, id, props);

        // GitHub source connection
        const sourceArtifact = CodePipelineSource.connection(
            'lerebel103/hottop-sidecar-firmware',
            'main',
            {
                connectionArn:
                    'arn:aws:codestar-connections:ap-southeast-2:407440998404:connection/b17a644f-371b-46ed-9ddf-8578dd6eb898'
            }
        );


        const pipeline = new CodePipeline(this, 'hottopsidecar-build-pipeline', {
            pipelineName: 'hottopsidecar-build-pipeline',
            synth: new CodeBuildStep('SynthStep', {
                input: sourceArtifact,
                installCommands: ['npm install -g aws-cdk'],
                commands: ['cd ci/', 'npm ci', 'npm run build', 'npx cdk synth'],
                primaryOutputDirectory: 'ci/cdk.out'
            })
        });

        // ====== Add stages to the pipeline ======
        const buildFirmwareStage = new BuildFirmwareStage(this, "hottopsidecar-fw-stage",
            props, sourceArtifact.primaryOutput);

        pipeline.addStage(buildFirmwareStage);
    }
}
