file(GLOB_RECURSE COMPONENT_SRCS
        main.cpp
        level_shifter.cpp
        panel_inputs.cpp
        balancer.cpp
        ../components/esp-input-pwm-duty/src/input_pwm_duty.cpp
        digital_input.cpp
        control_loop.cpp
        )


register_component()