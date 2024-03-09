import * as cdk from "aws-cdk-lib";
import {Construct} from 'constructs';
import {CodeBuildStep, CodePipeline, CodePipelineSource, ShellStep} from "aws-cdk-lib/pipelines";
import {FileSet} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";
import {ComputeType, LinuxBuildImage, LocalCacheMode, Project, Source} from "aws-cdk-lib/aws-codebuild";
import {Cache, BuildSpec} from "aws-cdk-lib/aws-codebuild";
import {Artifact, Pipeline, PipelineType} from "aws-cdk-lib/aws-codepipeline";
import {CodeBuildAction, S3DeployAction, S3SourceAction, S3Trigger} from "aws-cdk-lib/aws-codepipeline-actions";
import {Repository} from "aws-cdk-lib/aws-codecommit";
import {BlockPublicAccess, Bucket, BucketEncryption} from "aws-cdk-lib/aws-s3";
import {Globals} from "./globals";
import {ReadWriteType, Trail} from "aws-cdk-lib/aws-cloudtrail";
import {aws_codepipeline_actions, aws_iam, CfnOutput} from "aws-cdk-lib";
import {CfnAccessKey, Effect} from "aws-cdk-lib/aws-iam";

export class FirmwareResourcesStack extends cdk.Stack {

    otaBucketArn: undefined | string = undefined;

    constructor(scope: Construct, id: string, props: cdk.StackProps, sourceFiles: CodePipelineSource) {
        super(scope, id, props);

        // Define the bucket used to receive new firmware builds from GitHub Actions
        const otaBucket = new Bucket(this, `afr-ota-${Globals.THING_MANUFACTURER}-${Globals.THING_TYPE_NAME}-${Globals.STAGE_NAME}`, {
            bucketName: `afr-ota-${Globals.THING_MANUFACTURER}-${Globals.THING_TYPE_NAME}-${Globals.STAGE_NAME}`,
            blockPublicAccess: BlockPublicAccess.BLOCK_ALL,
            encryption: BucketEncryption.S3_MANAGED,
            enforceSSL: true,
            versioned: true,
            removalPolicy: cdk.RemovalPolicy.RETAIN,
        });
        this.otaBucketArn = otaBucket.bucketArn;

        new cdk.CfnOutput(this, 'OTABucketUrl', {
            value: otaBucket.s3UrlForObject(),
            description: `OTA bucket for new firmware builds`,
            exportName: 'OTA-Bucket-url',
        });

        // Now monitor for inbound changes on new firmwares landing in the bucket
        const sourceOutput = new Artifact();
        const key = 'newBuilds/firmware.zip';
        const trail = new Trail(this, 'CloudTrail');
        trail.addS3EventSelector([{
            bucket: otaBucket,
            objectPrefix: key,
        }], {
            readWriteType: ReadWriteType.WRITE_ONLY,
        });
        const sourceAction = new S3SourceAction({
            actionName: 'S3Source',
            bucketKey: key,
            bucket: otaBucket,
            output: sourceOutput,
            trigger: S3Trigger.EVENTS,
        });

        // Create a pipeline
        const pipeline = new Pipeline(this, "hottopsidecar-fw-pipeline", {
            pipelineName: "hottopsidecar-fw-pipeline",
            pipelineType: PipelineType.V2,
        });
        pipeline.addStage({
            stageName: "new-fw-received-trigger",
            actions: [sourceAction],
        });

        const deploySignedFiremwareStage = new S3DeployAction({
            actionName: 'Dummy',
            bucket: otaBucket,
            input: sourceOutput,
        });

        pipeline.addStage({stageName: 'dummy-stage', actions: [deploySignedFiremwareStage]});


    }
}
