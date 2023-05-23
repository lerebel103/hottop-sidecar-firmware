file(GLOB_RECURSE COMPONENT_SRCS
        main.cpp
        level_shifter.cpp
        panel_inputs.cpp
        balancer.cpp
        input_pwm_duty.cpp
        digital_input.cpp
        control_loop.cpp
        wifi_connect.cpp
        sntp.cpp
        nvs.cpp
        mqtt.cpp
        mqtt_mutual_auth.cpp
        )


register_component()