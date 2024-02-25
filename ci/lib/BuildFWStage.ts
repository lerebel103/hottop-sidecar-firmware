import { Stage, StageProps } from "aws-cdk-lib";
import { Construct } from 'constructs';
import {BuildFWResourcesStack} from "./BuildFWResourcesStack";
import {FileSet, IFileSetProducer} from "aws-cdk-lib/pipelines/lib/blueprint/file-set";

export class BuildFirmwareStage extends Stage {
    constructor(scope: Construct, id: string, props: StageProps, sourceFiles: FileSet | undefined) {
        super(scope, id, props);

        //***********Instantiate the resource stack***********
        new BuildFWResourcesStack(this, 'build-stack', props, sourceFiles);
    }
}