import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import {CodeBuildStep, CodePipeline, CodePipelineSource} from "aws-cdk-lib/pipelines";
import {BuildFirmwareStage} from "./BuildFWStage";

/**
 * A stack for the CI/CD pipeline
 *
 */
export class CIStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    const pipeline = new CodePipeline(this, 'hottopsidecar-build-pipeline', {
      pipelineName: 'hottopsidecar-build-pipeline',
      synth: new CodeBuildStep('SynthStep', {
        input: CodePipelineSource.connection(
            'lerebel103/hottop-sidecar-firmware',
            'main',
            {
              connectionArn:
                  'arn:aws:codestar-connections:ap-southeast-2:407440998404:connection/b17a644f-371b-46ed-9ddf-8578dd6eb898'
            }
        ),
        installCommands: ['npm install -g aws-cdk'],
          commands: ['cd ci/', 'mkdir cdk.out', 'npm ci', 'npm run build', 'npx cdk synth']
      })
    });

    // ====== Add stages to the pipeline ======
    const buildFirmwareStage = new BuildFirmwareStage(this, "build-firmware-stage");
    pipeline.addStage(buildFirmwareStage);
  }
}
