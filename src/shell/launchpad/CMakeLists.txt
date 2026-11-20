add_library(cutefish-shell-launchpad STATIC
    launchpad.cpp
    launchpad.h
    launchpad_model.cpp
    launchpad_model.h
)
target_link_libraries(cutefish-shell-launchpad PUBLIC cutefish-shell-dock)
