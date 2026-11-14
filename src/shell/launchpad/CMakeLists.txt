# The launchpad without its view: its applications in the user's order, the
# search, the pages, and org.cutefish.Launchpad.
add_library(cutefish-shell-launchpad STATIC
    launchpad.cpp
    launchpad.h
    launchpad_model.cpp
    launchpad_model.h
)
target_link_libraries(cutefish-shell-launchpad PUBLIC cutefish-shell-dock)
