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
import { Effect, PolicyStatement } from "aws-cdk-lib/aws-iam";
import { RemovalPolicy } from "aws-cdk-lib";
import { RetentionDays } from "aws-cdk-lib/aws-logs";

// This can be found as an output parameter to the rebelthings-iot stack
const DEPLOY_BUCKET_IMPORT_KEY = "RebelthingsIoTStack:OtaBucketArn";

export class FirmwareOtaStack extends cdk.Stack {
  deployBucketArn: undefined | string = undefined;

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
        removalPolicy: RemovalPolicy.DESTROY,
        autoDeleteObjects: true,
      },
    );

    // Find the deployment bucket from the rebelthings-iot stack
    this.deployBucketArn = cdk.Fn.importValue(DEPLOY_BUCKET_IMPORT_KEY);
    const deployBucket = Bucket.fromBucketArn(
      this,
      "deployBucket",
      this.deployBucketArn,
    );

    // Now monitor for inbound changes on new firmwares landing in the bucket
    const sourceOutput = new Artifact();
    const key = `deploy/${Globals.THING_TYPE}/firmware.zip`;
    const trail = new Trail(this, "CloudTrail", {
      cloudWatchLogsRetention: RetentionDays.THREE_MONTHS,
    });
    trail.addS3EventSelector(
      [
        {
          bucket: deployBucket,
          objectPrefix: key,
        },
      ],
      {
        readWriteType: ReadWriteType.WRITE_ONLY,
      },
    );
    trail.applyRemovalPolicy(RemovalPolicy.DESTROY);

    const sourceAction = new S3SourceAction({
      actionName: "S3Source",
      bucketKey: key,
      bucket: deployBucket,
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
          `arn:aws:ssm:${this.region}:${this.account}:parameter/iot/rebelthings/ota-bucket`,
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
        resources: [`${deployBucket.bucketArn}`, `${deployBucket.bucketArn}/*`],
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

    const devProject = new PipelineProject(this, "roastapowah-fw-project-dev", {
      description: "Deploy firmware for OTA Jobs in Staging",
      environment: {
        computeType: ComputeType.SMALL,
        buildImage: LinuxBuildImage.STANDARD_7_0,
      },
      buildSpec: BuildSpec.fromSourceFilename("buildspec.yaml"),
      cache: Cache.bucket(myCachingBucket),
    });
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
    const stgProject = new PipelineProject(this, "roastapowah-fw-project-stg", {
      description: "Deploy firmware for OTA Jobs in Staging",
      environment: {
        computeType: ComputeType.SMALL,
        buildImage: LinuxBuildImage.STANDARD_7_0,
      },
      buildSpec: BuildSpec.fromSourceFilename("buildspec.yaml"),
      cache: Cache.bucket(myCachingBucket),
    });
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
    const prdProject = new PipelineProject(this, "roastapowah-fw-project-prd", {
      description: "Deploy firmware for OTA Jobs in Production",
      environment: {
        computeType: ComputeType.SMALL,
        buildImage: LinuxBuildImage.STANDARD_7_0,
      },
      buildSpec: BuildSpec.fromSourceFilename("buildspec.yaml"),
      cache: Cache.bucket(myCachingBucket),
    });
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
