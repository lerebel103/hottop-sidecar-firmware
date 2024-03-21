import * as cdk from "aws-cdk-lib";
import { Construct } from "constructs";
import {
  CodeBuildStep,
  CodePipeline,
  CodePipelineSource,
} from "aws-cdk-lib/pipelines";
import { Cache, BuildSpec } from "aws-cdk-lib/aws-codebuild";
import {
  BlockPublicAccess,
  Bucket,
  BucketEncryption,
} from "aws-cdk-lib/aws-s3";
import { FirmwareDeployStage, FirmwareStageProps } from "./FirmwareDeployStage";
import { Globals } from "./globals";

const cacheBucketName = "hottop-pipeline-cache-bucket";

/**
 * A stack for the CI/CD pipeline
 *
 */
export class SelfUpdateTopStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props: cdk.StackProps) {
    super(scope, id, props);

    const myCachingBucket = new Bucket(this, cacheBucketName, {
      blockPublicAccess: BlockPublicAccess.BLOCK_ALL,
      encryption: BucketEncryption.S3_MANAGED,
      enforceSSL: true,
      versioned: true,
      removalPolicy: cdk.RemovalPolicy.RETAIN,
    });

    // These could or should really be made paramater store variables
    const owner = "lerebel103";
    const repo = "hottop-sidecar-firmware";

    // GitHub source connection
    const sourceArtifact = CodePipelineSource.connection(
      `${owner}/${repo}`,
      "main",
      {
        connectionArn:
          "arn:aws:codestar-connections:ap-southeast-2:407440998404:connection/b17a644f-371b-46ed-9ddf-8578dd6eb898",
        codeBuildCloneOutput: true,
      },
    );

    const pipeline = new CodePipeline(this, "roastapowah-build-pipeline", {
      pipelineName: "roastapowah-build-pipeline",
      synth: new CodeBuildStep("SynthStep", {
        input: sourceArtifact,
        installCommands: ["npm install -g aws-cdk"],
        commands: ["cd ci/", "npm ci", "npm run build", "npx cdk synth"],
        primaryOutputDirectory: "ci/cdk.out",
        cache: Cache.bucket(myCachingBucket),
        partialBuildSpec: BuildSpec.fromObject({
          cache: {
            paths: ["/root/.m2/**/*", "/root/.npm/**/*"],
          },
        }),
      }),
    });
    
    // Here's our standard firmware deploy stage to dev, stg, prd accounts
    const fwStageProps: FirmwareStageProps = { ...props, owner, repo };
    const fwStage = new FirmwareDeployStage(
      this,
      `${Globals.THING_TYPE}-fw-stage`,
      fwStageProps,
    );
    pipeline.addStage(fwStage);
  }
}
