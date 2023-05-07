file(GLOB_RECURSE COMPONENT_SRCS
        main.cpp
        level_shifter.cpp
        panel_inputs.cpp
        max31850.cpp
        balancer.cpp
        input_power_duty.cpp
        digital_input.cpp
        control_loop.cpp
        )


register_component()