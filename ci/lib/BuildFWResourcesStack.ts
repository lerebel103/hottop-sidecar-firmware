import * as cdk from "aws-cdk-lib";
import {Construct} from 'constructs';
import {CodeBuildStep, CodePipeline} from "aws-cdk-lib/pipelines";
import {FileSet, IFileSetProducer} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";
import {ComputeType, LinuxBuildImage, LocalCacheMode} from "aws-cdk-lib/aws-codebuild";
import {Cache, BuildSpec} from "aws-cdk-lib/aws-codebuild";

export class BuildFWResourcesStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props: cdk.StackProps, sourceFiles: FileSet | undefined) {
        super(scope, id, props);



        const pipeline = new CodePipeline(this, 'build-fw', {
            // Turn this on because the pipeline uses Docker image assets
            dockerEnabledForSelfMutation: true,
            pipelineName: 'build-fw',
            synth: new CodeBuildStep('build', {
                cache: Cache.local(LocalCacheMode.DOCKER_LAYER, LocalCacheMode.CUSTOM),
                input: sourceFiles,
                commands: ['ls -tral'],
                primaryOutputDirectory: 'ci/cdk.out',
                buildEnvironment: {
                    buildImage: LinuxBuildImage.fromDockerRegistry('espressif/idf:v5.2'),
                    computeType: ComputeType.SMALL,
                    privileged: true
                }
            })
        });
    }
}
