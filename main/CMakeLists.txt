file(GLOB_RECURSE COMPONENT_SRCS
        main.cpp
        level_shifter.cpp
        panel_inputs.cpp
        max31850.cpp
        ssr_ctlr.cpp
        ssr2_ctlr.cpp
        rmt_duty_map_50hz.cpp
        rmt_duty_map_60hz.cpp
        balancer.cpp
        sync_signal_generator.cpp
        input_power_duty.cpp
        digital_input.cpp
        )


register_component()