add_library(cutefish-shell-clipboard STATIC
    clipboard_history.cpp
    clipboard_history.h
    clipboard_monitor.cpp
    clipboard_monitor.h
    clipboard_store.cpp
    clipboard_store.h
)
target_link_libraries(cutefish-shell-clipboard PUBLIC cutefish-shell-common)
