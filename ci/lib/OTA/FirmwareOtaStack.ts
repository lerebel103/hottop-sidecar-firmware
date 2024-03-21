import * as cdk from "aws-cdk-lib";
import { Construct } from "constructs";
import {
  BuildSpec,
  Cache,
  ComputeType,
  LinuxBuildImage,
  PipelineProject,
} from "aws-cdk-lib/aws-codebuild";
import { Artifact, Pipeline, PipelineType } from "aws-cdk-lib/aws-codepipeline";
import {
  CodeBuildAction,
  ManualApprovalAction,
  S3SourceAction,
  S3Trigger,
} from "aws-cdk-lib/aws-codepipeline-actions";
import {
  BlockPublicAccess,
  Bucket,
  BucketEncryption,
} from "aws-cdk-lib/aws-s3";
import { Globals } from "../globals";
import { ReadWriteType, Trail } from "aws-cdk-lib/aws-cloudtrail";
import {
  ParameterDataType,
  ParameterTier,
  StringParameter,
} from "aws-cdk-lib/aws-ssm";
import { Effect, PolicyStatement } from "aws-cdk-lib/aws-iam";

const OTA_BUCKET_PARAMETER_URI = `/iot/rebelthings/roastapowah/ota-bucket`;

export class FirmwareOtaStack extends cdk.Stack {
  otaBucketArn: undefined | string = undefined;

  constructor(scope: Construct, id: string, props: cdk.StackProps) {
    super(scope, id, props);

    const myCachingBucket = new Bucket(
      this,
      `${Globals.THING_TYPE}-build-cache`,
      {
        blockPublicAccess: BlockPublicAccess.BLOCK_ALL,
        encryption: BucketEncryption.S3_MANAGED,
        enforceSSL: true,
        versioned: true,
        removalPolicy: cdk.RemovalPolicy.RETAIN,
      },
    );

    // Define the bucket used to receive new firmware builds from GitHub Actions
    const otaBucket = new Bucket(
      this,
      `afr-ota-${Globals.THING_MANUFACTURER}-${Globals.THING_TYPE}-${Globals.STAGE_NAME}`,
      {
        blockPublicAccess: BlockPublicAccess.BLOCK_ALL,
        encryption: BucketEncryption.S3_MANAGED,
        enforceSSL: true,
        versioned: true,
        removalPolicy: cdk.RemovalPolicy.RETAIN,
      },
    );
    this.otaBucketArn = otaBucket.bucketArn;

    // Export the bucket URL
    new cdk.CfnOutput(this, "OTABucketUrl", {
      value: otaBucket.s3UrlForObject(),
      description: `OTA bucket for new firmware builds`,
      exportName: "OTA-Bucket-url",
    });

    // But also store as parameter in SSM
    new StringParameter(this, `${Globals.THING_TYPE}-ota-uri`, {
      parameterName: OTA_BUCKET_PARAMETER_URI,
      stringValue: otaBucket.s3UrlForObject(),
      dataType: ParameterDataType.TEXT,
      description: `The URI of the OTA bucket for ${Globals.THING_TYPE} firmware`,
      tier: ParameterTier.STANDARD,
    });

    // Now monitor for inbound changes on new firmwares landing in the bucket
    const sourceOutput = new Artifact();
    const key = "newBuilds/firmware.zip";
    const trail = new Trail(this, "CloudTrail");
    trail.addS3EventSelector(
      [
        {
          bucket: otaBucket,
          objectPrefix: key,
        },
      ],
      {
        readWriteType: ReadWriteType.WRITE_ONLY,
      },
    );
    const sourceAction = new S3SourceAction({
      actionName: "S3Source",
      bucketKey: key,
      bucket: otaBucket,
      output: sourceOutput,
      trigger: S3Trigger.EVENTS,
    });

    // Create a pipeline
    const pipeline = new Pipeline(this, "roastapowah-fw-pipeline", {
      pipelineName: "roastapowah-fw-pipeline",
      pipelineType: PipelineType.V2,
    });
    pipeline.addStage({
      stageName: "new-firmware",
      actions: [sourceAction],
    });

    // Create a CodeBuild project
    const project = new PipelineProject(this, "roastapowah-fw-project", {
      description: "Deploy firmware for OTA Jobs",
      environment: {
        computeType: ComputeType.SMALL,
        buildImage: LinuxBuildImage.STANDARD_7_0,
      },
      buildSpec: BuildSpec.fromSourceFilename("deploy/buildspec.yaml"),
      cache: Cache.bucket(myCachingBucket),
    });

    project.role?.addToPrincipalPolicy(
      new PolicyStatement({
        effect: Effect.ALLOW,
        actions: ["ssm:GetParameters"],
        resources: [
          `arn:aws:ssm:${this.region}:${this.account}:parameter${OTA_BUCKET_PARAMETER_URI}`,
          `arn:aws:ssm:${this.region}:${this.account}:parameter/iot/rebelthings/codesign/esp32/pk`,
          `arn:aws:ssm:${this.region}:${this.account}:parameter/iot/rebelthings/codesign/esp32/codesign-profile-arn`,
          `arn:aws:ssm:${this.region}:${this.account}:parameter/iot/rebelthings/codesign/esp32/certificate-arn`,
        ],
      }),
    );

    project.role?.addToPrincipalPolicy(
      new PolicyStatement({
        effect: Effect.ALLOW,
        actions: ["s3:*"],
        resources: [`${otaBucket.bucketArn}`, `${otaBucket.bucketArn}/*`],
      }),
    );
    project.role?.addToPrincipalPolicy(
      new PolicyStatement({
        effect: Effect.ALLOW,
        actions: ["signer:StartSigningJob"],
        resources: [`arn:aws:signer:${this.region}:${this.account}:*`],
      }),
    );
    // Sign Firmware
    const signAction = new CodeBuildAction({
      actionName: "sign-firmware",
      project: project,
      input: sourceOutput,
      runOrder: 1,
    });

    const devProject = new PipelineProject(
      this,
      "roastapowah-fw-project-dev",
      {
        description: "Deploy firmware for OTA Jobs in Staging",
        environment: {
          computeType: ComputeType.SMALL,
          buildImage: LinuxBuildImage.STANDARD_7_0,
        },
        buildSpec: BuildSpec.fromSourceFilename("buildspec.yaml"),
        cache: Cache.bucket(myCachingBucket),
      },
    );
    const devAction = new CodeBuildAction({
      actionName: "OTA-deploy-dev",
      project: devProject,
      input: sourceOutput,
      runOrder: 2,
    });

    const stgGate = new ManualApprovalAction({
      actionName: "StagingGate",
      runOrder: 1,
    });
    const stgProject = new PipelineProject(
      this,
      "roastapowah-fw-project-stg",
      {
        description: "Deploy firmware for OTA Jobs in Staging",
        environment: {
          computeType: ComputeType.SMALL,
          buildImage: LinuxBuildImage.STANDARD_7_0,
        },
        buildSpec: BuildSpec.fromSourceFilename("buildspec.yaml"),
        cache: Cache.bucket(myCachingBucket),
      },
    );
    const stgAction = new CodeBuildAction({
      actionName: "OTA-deploy-stg",
      project: stgProject,
      input: sourceOutput,
      runOrder: 2,
    });

    const prdGate = new ManualApprovalAction({
      actionName: "ProductionGate",
      runOrder: 1,
    });
    const prdProject = new PipelineProject(
      this,
      "roastapowah-fw-project-prd",
      {
        description: "Deploy firmware for OTA Jobs in Production",
        environment: {
          computeType: ComputeType.SMALL,
          buildImage: LinuxBuildImage.STANDARD_7_0,
        },
        buildSpec: BuildSpec.fromSourceFilename("buildspec.yaml"),
        cache: Cache.bucket(myCachingBucket),
      },
    );
    const prdAction = new CodeBuildAction({
      actionName: "OTA-deploy-prd",
      project: prdProject,
      input: sourceOutput,
      runOrder: 2,
    });

    pipeline.addStage({
      stageName: "deploy-dev",
      actions: [signAction, devAction],
    });
    pipeline.addStage({
      stageName: "deploy-stg",
      actions: [stgGate, stgAction],
    });
    pipeline.addStage({
      stageName: "deploy-prd",
      actions: [prdGate, prdAction],
    });
  }
}
