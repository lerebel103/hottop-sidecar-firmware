cmake_minimum_required(VERSION 3.5)

set(COMPONENT_SRCDIRS
        src/
        )

set(COMPONENT_ADD_INCLUDEDIRS
        src/
        )

set(COMPONENT_REQUIRES
        esp32-onewire
        )

idf_component_register(
        SRC_DIRS ${COMPONENT_SRCDIRS}
        REQUIRES ${COMPONENT_REQUIRES}
        INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
)