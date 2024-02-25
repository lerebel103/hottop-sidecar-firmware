import * as cdk from "aws-cdk-lib";
import {Construct} from 'constructs';
import {CodeBuildStep, CodePipeline} from "aws-cdk-lib/pipelines";
import {FileSet} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";
import {ComputeType, LinuxBuildImage, LocalCacheMode} from "aws-cdk-lib/aws-codebuild";
import {Cache, BuildSpec} from "aws-cdk-lib/aws-codebuild";

// ESP-IDF version to use, we will use this to pull down the correct Docker image
const espIdfVersion = 'v5.2';
const outDir = 'ci/cdk.out';

export class BuildFWResourcesStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props: cdk.StackProps, sourceFiles: FileSet | undefined) {
        super(scope, id, props);


        const buildStep = new CodeBuildStep('build', {
            cache: Cache.local(LocalCacheMode.DOCKER_LAYER, LocalCacheMode.CUSTOM),
            input: sourceFiles?.primaryOutput,
            commands: ['ls -tral', `mkdir -p ${outDir}`],
            primaryOutputDirectory: outDir,
            buildEnvironment: {
                buildImage: LinuxBuildImage.fromDockerRegistry(`espressif/idf:${espIdfVersion}`),
                computeType: ComputeType.SMALL,
                privileged: true
            }
        });


        /*const pipeline = new CodePipeline(this, 'build-fw', {
            // Turn this on because the pipeline uses Docker image assets
            dockerEnabledForSelfMutation: true,
            pipelineName: 'hottopsidecar-build-fw',
            synth: new CodeBuildStep('Synth', {
                commands: [],
            })
        });*/


    }
}
