import { Stage, StageProps } from "aws-cdk-lib";
import { Construct } from 'constructs';
import {BuildFWResourcesStack} from "./BuildFWResourcesStack";

export class BuildFirmwareStage extends Stage {
    constructor(scope: Construct, id: string, props?: StageProps) {
        super(scope, id, props);

        //***********Instantiate the resource stack***********
        new BuildFWResourcesStack(this, 'hottopsidecar-build-fw-stack');
    }
}