# The menu bar's app menus: the user's menu, the active application's
# name and its exported menus, and the desktop's menus.
find_package(CutefishClients REQUIRED COMPONENTS accounts)

add_library(cutefish-shell-app-menus STATIC
    src/shell/menubar/app_menus_model.cpp
    src/shell/menubar/app_menus_model.h
    src/shell/menubar/app_menus_view.cpp
    src/shell/menubar/app_menus_view.h
)
target_link_libraries(cutefish-shell-app-menus PUBLIC cutefish-shell cutefish-shell-menubar
    cutefish-shell-menubar-view Cutefish::AccountsClient)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-app-menus)
