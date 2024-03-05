import * as cdk from "aws-cdk-lib";
import {Construct} from 'constructs';
import {CodeBuildStep, CodePipeline, CodePipelineSource, ShellStep} from "aws-cdk-lib/pipelines";
import {FileSet} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";
import {ComputeType, LinuxBuildImage, LocalCacheMode, Project, Source} from "aws-cdk-lib/aws-codebuild";
import {Cache, BuildSpec} from "aws-cdk-lib/aws-codebuild";
import {Artifact, Pipeline} from "aws-cdk-lib/aws-codepipeline";
import {CodeBuildAction, S3DeployAction, S3SourceAction, S3Trigger} from "aws-cdk-lib/aws-codepipeline-actions";
import {Repository} from "aws-cdk-lib/aws-codecommit";
import {BlockPublicAccess, Bucket, BucketEncryption} from "aws-cdk-lib/aws-s3";
import {Globals} from "./globals";
import {ReadWriteType, Trail} from "aws-cdk-lib/aws-cloudtrail";
import {aws_codepipeline_actions, aws_iam, CfnOutput} from "aws-cdk-lib";
import {CfnAccessKey, Effect} from "aws-cdk-lib/aws-iam";

export class FirmwareResourcesStack extends cdk.Stack {
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

        // User that will allow GitHub to push firmware to the ota bucket
        const otaBucketUser = new aws_iam.User(this, 'hottopsidecar-ota-bucket-user', {
            userName: 'hottopsidecar-ota-bucket-user',
        });
        otaBucketUser.addToPolicy(new aws_iam.PolicyStatement({
                effect: Effect.ALLOW,
                actions: ['s3:PutObject'],
                resources: [`${otaBucket.bucketArn}/newBuilds`, `${otaBucket.bucketArn}/newBuilds/firmware.zip`]
            })
        );

        // Add secret to the user
        const accessKey = new CfnAccessKey(this, 'CfnAccessKey', {
            userName: otaBucketUser.userName,
        });
        // Output the auth info as part of cloud formation output
        new CfnOutput(this, 'accessKeyId', {value: accessKey.ref});
        new CfnOutput(this, 'secretAccessKey', {value: accessKey.attrSecretAccessKey});

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


        // Repository.fromRepositoryName()

        // CodeBuild project that builds the firmware
        /*const buildImage = new Project(this, "Firmware", {
            source: sourceAction,
            environment: {
                privileged: true,
                environmentVariables: {
                    AWS_ACCOUNT_ID: { value: process.env?.CDK_DEFAULT_ACCOUNT || "" },
                    REGION: { value: process.env?.CDK_DEFAULT_REGION || "" },
                    IMAGE_TAG: { value: "latest" },
                },
            },
        });


        // Creates the build stage for CodePipeline
        const buildStage = {
            stageName: "DeployFirmware",
            actions: [
                new CodeBuildAction({
                    actionName: "Sign Firmware",
                    input: new Artifact("SourceArtifact"),
                    project: buildImage,
                    outputs: [buildArtifact],
                }),
            ],
        };

        // Creates an AWS CodePipeline with source, build, and deploy stages
        new Pipeline(this, "hottopsidecar-fw-pipeline", {
            pipelineName: "hottopsidecar-fw-pipeline",
            stages: [buildStage],
        }); */

    }
}
