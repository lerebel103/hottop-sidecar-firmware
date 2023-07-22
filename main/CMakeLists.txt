file(GLOB_RECURSE COMPONENT_SRCS
        main.cpp
        level_shifter.cpp
        panel_inputs.cpp
        balancer.cpp
        input_pwm_duty.cpp
        digital_input.cpp
        control_loop.cpp
        nvs.cpp
        reset_button.cpp
        pm_control.cpp
        app_metrics.cpp
        device_info.cpp
        telemetry.cpp
        )


register_component()