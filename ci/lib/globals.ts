const account = process.env.CDK_DEFAULT_ACCOUNT;
if (account === undefined || account === '') {
    throw new Error('CDK_DEFAULT_ACCOUNT is not defined');
}

const region = process.env.CDK_DEFAULT_REGION;
if (region === undefined || region === '') {
    throw new Error('CDK_DEFAULT_REGION is not defined');
}

console.log(`CDK_DEFAULT_ACCOUNT: ${account}`);
console.log(`CDK_DEFAULT_REGION: ${region}`);

export namespace Globals {
    export var STAGE_NAME: string = process.env.STAGE_NAME || 'dev';
    export var AWS_ACCOUNT = account;
    export var AWS_REGION = region;
    export var THING_MANUFACTURER = 'rebelthings';
    export var THING_TYPE_NAME = 'hottopsidecar';
}
