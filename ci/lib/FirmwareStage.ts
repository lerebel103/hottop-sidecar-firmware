import { Stage, StageProps } from "aws-cdk-lib";
import { Construct } from 'constructs';
import {FirmwareResourcesStack} from "./FirmwareResourcesStack";
import {FileSet, IFileSetProducer} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";
import {CodePipelineSource} from "aws-cdk-lib/pipelines";

export class FirmwareStage extends Stage {
    constructor(scope: Construct, id: string, props: StageProps, sourceFiles: CodePipelineSource) {
        super(scope, id, props);

        //***********Instantiate the resource stack***********
        new FirmwareResourcesStack(this, 'build-stack', props, sourceFiles);
    }
}