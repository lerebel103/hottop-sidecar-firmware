import * as cdk from "aws-cdk-lib";
import {Construct} from 'constructs';
import {CodeBuildStep, CodePipeline, CodePipelineSource, ShellStep} from "aws-cdk-lib/pipelines";
import {FileSet} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";
import {ComputeType, LinuxBuildImage, LocalCacheMode, Project, Source} from "aws-cdk-lib/aws-codebuild";
import {BuildSpec} from "aws-cdk-lib/aws-codebuild";
import {Artifact, Pipeline, PipelineType} from "aws-cdk-lib/aws-codepipeline";
import {CodeBuildAction, S3SourceAction, S3Trigger} from "aws-cdk-lib/aws-codepipeline-actions";
import {BlockPublicAccess, Bucket, BucketEncryption} from "aws-cdk-lib/aws-s3";
import {Globals} from "./globals";
import {ReadWriteType, Trail} from "aws-cdk-lib/aws-cloudtrail";
import {ParameterDataType, ParameterTier, StringParameter} from "aws-cdk-lib/aws-ssm";

const OTA_BUCKET_PARAMETER_URI = `/iot/rebelthings/hottopsidecar/ota-bucket`;

export class FirmwareDeployStack extends cdk.Stack {

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

        // Export the bucket URL
        new cdk.CfnOutput(this, 'OTABucketUrl', {
            value: otaBucket.s3UrlForObject(),
            description: `OTA bucket for new firmware builds`,
            exportName: 'OTA-Bucket-url',
        });

        // But also store as parameter in SSM
        new StringParameter(this, `${Globals.THING_TYPE_NAME}-ota-uri`, {
            parameterName: OTA_BUCKET_PARAMETER_URI,
            stringValue: otaBucket.s3UrlForObject(),
            dataType: ParameterDataType.TEXT,
            description: `The URI of the OTA bucket for ${Globals.THING_TYPE_NAME} firmware`,
            tier: ParameterTier.STANDARD,
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
            stageName: "new-firmware",
            actions: [sourceAction],
        });

        // Sign Firmware
        const signAction = new CodeBuildAction({
            actionName: 'SignFirmware',
            project: new Project(this, 'SignFirmware', {
                projectName: 'SignFirmware',
                buildSpec: BuildSpec.fromObject({
                    version: '0.2',
                    phases: {
                        install: {
                            commands: [
                                'echo "Signing firmware to device"',
                            ],
                        },
                        build: {
                            commands: [
                                'echo "Firmware Signed to device"',
                            ],
                        },
                    },
                }),
                environment: {
                    buildImage: LinuxBuildImage.STANDARD_5_0,
                    computeType: ComputeType.SMALL,
                },
            }),
            input: sourceOutput,
        });


        // Deploy the firmware to the device
        const deployAction = new CodeBuildAction({
            actionName: 'DeployFirmware',
            project: new Project(this, 'DeployFirmware', {
                projectName: 'DeployFirmware',
                buildSpec: BuildSpec.fromObject({
                    version: '0.2',
                    phases: {
                        install: {
                            commands: [
                                'echo "Deploying firmware to device"',
                                'ls -tral'
                            ],
                        },
                        build: {
                            commands: [
                                'echo "Firmware deployed to device"',
                            ],
                        },
                    },
                }),
                environment: {
                    buildImage: LinuxBuildImage.STANDARD_5_0,
                    computeType: ComputeType.SMALL,
                },
            }),
            input: sourceOutput,
        });
        
        
        pipeline.addStage({stageName: 'deploy-firmware-OTA', actions: [signAction, deployAction]});


    }
}
