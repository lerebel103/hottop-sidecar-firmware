import aws_cdk as core
import aws_cdk.assertions as assertions

from hottopsidecar_pipeline.hottopsidecar_pipeline_stack import HottopsidecarPipelineStack

# example tests. To run these tests, uncomment this file along with the example
# resource in hottopsidecar_pipeline/hottopsidecar_pipeline_stack.py
def test_sqs_queue_created():
    app = core.App()
    stack = HottopsidecarPipelineStack(app, "hottopsidecar-pipeline")
    template = assertions.Template.from_stack(stack)

#     template.has_resource_properties("AWS::SQS::Queue", {
#         "VisibilityTimeout": 300
#     })
