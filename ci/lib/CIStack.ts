import * as cdk from 'aws-cdk-lib';
import {Construct} from 'constructs';
import {CodeBuildStep, CodePipeline, CodePipelineSource} from "aws-cdk-lib/pipelines";
import {Cache, BuildSpec} from "aws-cdk-lib/aws-codebuild";
import {BlockPublicAccess, Bucket, BucketEncryption} from "aws-cdk-lib/aws-s3";
import {BuildFirmwareStage} from "./BuildFWStage";

let myCachingBucket : Bucket | undefined = undefined;

/**
 * A stack for the CI/CD pipeline
 *
 */
export class CIStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props: cdk.StackProps) {
        super(scope, id, props);

         myCachingBucket = new Bucket(this, "hottop-pipeline-cache-bucket", {
            blockPublicAccess: BlockPublicAccess.BLOCK_ALL,
            encryption: BucketEncryption.S3_MANAGED,
            enforceSSL: true,
            versioned: true,
            removalPolicy: cdk.RemovalPolicy.RETAIN,
        });

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
                primaryOutputDirectory: 'ci/cdk.out',
                cache: Cache.bucket(myCachingBucket),
                partialBuildSpec: BuildSpec.fromObject(
                    {
                        "cache": {
                            "paths": [
                                "ci/cdk.out/**/*",
                                "/root/.m2/**/*",
                                "/root/.npm/**/*",
                            ]
                        },
                    }
                ),
            })
        });

        // ====== Add stages to the pipeline ======
        const buildFirmwareStage = new BuildFirmwareStage(this, "hottopsidecar-fw-stage",
            props, sourceArtifact.primaryOutput);

        pipeline.addStage(buildFirmwareStage);
    }
}
