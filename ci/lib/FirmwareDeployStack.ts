import * as cdk from "aws-cdk-lib";
import {Construct} from 'constructs';
import {CodePipelineSource} from "aws-cdk-lib/pipelines";
import {BuildSpec, ComputeType, PipelineProject} from "aws-cdk-lib/aws-codebuild";
import {Artifact, Pipeline, PipelineType} from "aws-cdk-lib/aws-codepipeline";
import {CodeBuildAction, S3SourceAction, S3Trigger} from "aws-cdk-lib/aws-codepipeline-actions";
import {BlockPublicAccess, Bucket, BucketEncryption} from "aws-cdk-lib/aws-s3";
import {Globals} from "./globals";
import {ReadWriteType, Trail} from "aws-cdk-lib/aws-cloudtrail";
import {ParameterDataType, ParameterTier, StringParameter} from "aws-cdk-lib/aws-ssm";
import {Effect, PolicyStatement} from "aws-cdk-lib/aws-iam";

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

        // Create a CodeBuild project
        const project = new PipelineProject(this, 'hottopsidecar-fw-project', {
            description: 'Deploy firmware for OTA Jobs',
            environment: {
                computeType: ComputeType.SMALL,
            },
            buildSpec: BuildSpec.fromSourceFilename('deploy/buildspec.yaml'),
        });



        project.role?.addToPrincipalPolicy(new PolicyStatement({
            effect: Effect.ALLOW,
            actions: ['ssm:GetParameters'],
            resources: [
                `arn:aws:ssm:${Globals.AWS_REGION}:${Globals.AWS_ACCOUNT}:parameter${OTA_BUCKET_PARAMETER_URI}`,
                `arn:aws:ssm:${Globals.AWS_REGION}:${Globals.AWS_ACCOUNT}:parameter/iot/rebelthings/codesign/esp32/pk`,
                `arn:aws:ssm:${Globals.AWS_REGION}:${Globals.AWS_ACCOUNT}:parameter/iot/rebelthings/codesign/esp32/codesign-profile-arn`,
                `arn:aws:ssm:${Globals.AWS_REGION}:${Globals.AWS_ACCOUNT}:parameter/iot/rebelthings/codesign/esp32/certificate-arn`,
            ],
        }));

        // Sign Firmware
        const deployAction = new CodeBuildAction({
            actionName: 'DeployFirmware',
            project: project,
            input: sourceOutput,
        });

        pipeline.addStage({stageName: 'deploy-firmware-OTA', actions: [deployAction]});

    }

}
