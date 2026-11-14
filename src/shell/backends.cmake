# The shell's backends, one static library per component: models and D-Bus
# services without views, on the toolkit and the framework's plain clients.
find_package(CutefishClients REQUIRED COMPONENTS upower powerprofiles audio networkmanager)

add_subdirectory(src/shell/common)
add_subdirectory(src/shell/notification)
add_subdirectory(src/shell/menubar)
add_subdirectory(src/shell/dock)
add_subdirectory(src/shell/launchpad)
add_subdirectory(src/shell/clipboard)
