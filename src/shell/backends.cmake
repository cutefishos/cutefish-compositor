find_package(CutefishClients REQUIRED COMPONENTS upower powerprofiles audio networkmanager)

add_subdirectory(src/shell/common)
add_subdirectory(src/shell/notification)
add_subdirectory(src/shell/menubar)
add_subdirectory(src/shell/dock)
add_subdirectory(src/shell/launchpad)
add_subdirectory(src/shell/clipboard)
