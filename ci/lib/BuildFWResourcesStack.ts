import * as cdk from "aws-cdk-lib";
import {Construct} from 'constructs';
import {CodeBuildStep, CodePipeline, CodePipelineSource} from "aws-cdk-lib/pipelines";
import {FileSet} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";
import {ComputeType, LinuxBuildImage, LocalCacheMode, Project, Source} from "aws-cdk-lib/aws-codebuild";
import {Cache, BuildSpec} from "aws-cdk-lib/aws-codebuild";
import {Artifact, Pipeline} from "aws-cdk-lib/aws-codepipeline";
import {CodeBuildAction} from "aws-cdk-lib/aws-codepipeline-actions";
import {Repository} from "aws-cdk-lib/aws-codecommit";

// ESP-IDF version to use, we will use this to pull down the correct Docker image
const espIdfVersion = 'v5.2';
const outDir = 'ci/cdk.out';

export class BuildFWResourcesStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props: cdk.StackProps, sourceFiles: CodePipelineSource) {
        super(scope, id, props);


        /*const buildStep = new CodeBuildStep('build', {
            cache: Cache.local(LocalCacheMode.DOCKER_LAYER, LocalCacheMode.CUSTOM),
            input: sourceFiles?.primaryOutput,
            commands: ['ls -tral', `mkdir -p ${outDir}`],
            primaryOutputDirectory: outDir,
            buildEnvironment: {
                buildImage: LinuxBuildImage.fromDockerRegistry(`espressif/idf:${espIdfVersion}`),
                computeType: ComputeType.SMALL,
                privileged: true
            }
        });*/


        // Creates new pipeline artifacts
        const sourceArtifact = new Artifact("SourceArtifact");
        const buildArtifact = new Artifact("BuildArtifact");

        // Repository.fromRepositoryName()


        // CodeBuild project that builds the firmware
        const buildImage = new Project(this, "BuildFirmware", {
            buildSpec: BuildSpec.fromSourceFilename("app/buildspec.yaml"),
            source: sourceFiles,
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
            stageName: "Build",
            actions: [
                new CodeBuildAction({
                    actionName: "DockerBuildPush",
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
