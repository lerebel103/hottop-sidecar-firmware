import * as cdk from "aws-cdk-lib";
import {Construct} from 'constructs';
import {CodeBuildStep, CodePipeline, CodePipelineSource} from "aws-cdk-lib/pipelines";
import {FileSet} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";
import {ComputeType, LinuxBuildImage, LocalCacheMode, Project, Source} from "aws-cdk-lib/aws-codebuild";
import {Cache, BuildSpec} from "aws-cdk-lib/aws-codebuild";
import {Artifact, Pipeline} from "aws-cdk-lib/aws-codepipeline";
import {CodeBuildAction} from "aws-cdk-lib/aws-codepipeline-actions";
import {Repository} from "aws-cdk-lib/aws-codecommit";
import {BlockPublicAccess, Bucket, BucketEncryption} from "aws-cdk-lib/aws-s3";
import {Globals} from "./globals";

export class FirmwareResourcesStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props: cdk.StackProps, sourceFiles: CodePipelineSource) {
        super(scope, id, props);

        // Define the bucket used to receive
        const otaBucket = new Bucket(this, `afr-ota-${Globals.THING_MANUFACTURER}-${Globals.THING_TYPE_NAME}-${Globals.STAGE_NAME}`, {
            bucketName: `afr-ota-${Globals.THING_MANUFACTURER}-${Globals.THING_TYPE_NAME}-${Globals.STAGE_NAME}`,
            blockPublicAccess: BlockPublicAccess.BLOCK_ALL,
            encryption: BucketEncryption.S3_MANAGED,
            enforceSSL: true,
            versioned: true,
            removalPolicy: cdk.RemovalPolicy.RETAIN,
        });



        // Creates new pipeline artifacts
        /*const sourceArtifact = new Artifact("SourceArtifact");
        const buildArtifact = new Artifact("BuildArtifact");

        // Repository.fromRepositoryName()

        // CodeBuild project that builds the firmware
        const buildImage = new Project(this, "Firmware", {
            source: Source.s3({}),
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
