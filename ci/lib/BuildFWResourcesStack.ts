import * as cdk from "aws-cdk-lib";
import { Construct } from 'constructs';
import {Bucket} from "aws-cdk-lib/aws-s3";
import {CodeBuildStep, CodePipeline} from "aws-cdk-lib/pipelines";
import {FileSet, IFileSetProducer} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";

const myCachingBucket : Bucket | undefined = undefined;

export class BuildFWResourcesStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props: cdk.StackProps, sourceFiles: FileSet | undefined) {
        super(scope, id, props);

        const pipeline = new CodePipeline(this, 'build-fw', {
            pipelineName: 'build-fw',
            synth: new CodeBuildStep('build', {
                input: sourceFiles,
                commands: ['ls -tral'],
            })
        });

    }
}