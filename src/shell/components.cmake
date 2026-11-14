# The shell's components drawn in the compositor, one static library each,
# on src/shell (internal windows) and their backends.

# The desktop background: the shared wallpaper decode, one node per output
# and the menu bar's backdrop rules.
find_package(CutefishClients REQUIRED COMPONENTS appearance)
add_library(cutefish-shell-wallpaper STATIC
    src/shell/wallpaper/bar_backdrop.cpp
    src/shell/wallpaper/bar_backdrop.h
    src/shell/wallpaper/wallpaper.cpp
    src/shell/wallpaper/wallpaper.h
    src/shell/wallpaper/wallpaper_cache.cpp
    src/shell/wallpaper/wallpaper_cache.h
)
target_link_libraries(cutefish-shell-wallpaper PUBLIC cutefish-shell Cutefish::AppearanceClient)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-wallpaper)

# The on-screen display: org.cutefish.Osd and its level panel.
add_library(cutefish-shell-osd STATIC
    src/shell/osd/level_track.cpp
    src/shell/osd/level_track.h
    src/shell/osd/osd.cpp
    src/shell/osd/osd.h
    src/shell/osd/osd_levels.cpp
)
target_link_libraries(cutefish-shell-osd PUBLIC cutefish-shell cutefish-shell-menubar
    Cutefish::AudioClient)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-osd)

# The dock: its view, window and input on the dock's backend.
add_library(cutefish-shell-dock-view STATIC
    src/shell/dock/dock_icon.cpp
    src/shell/dock/dock_icon.h
    src/shell/dock/dock_magnification.cpp
    src/shell/dock/dock_magnification.h
    src/shell/dock/dock_metrics.h
    src/shell/dock/dock_presentation.cpp
    src/shell/dock/dock_presentation.h
    src/shell/dock/dock_tooltip.cpp
    src/shell/dock/dock_tooltip.h
    src/shell/dock/dock_window.cpp
    src/shell/dock/dock_window.h
)
target_link_libraries(cutefish-shell-dock-view PUBLIC cutefish-shell cutefish-shell-dock)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-dock-view)

# The clipboard history's picker on the clipboard backend.
add_library(cutefish-shell-clipboard-view STATIC
    src/shell/clipboard/clipboard_picker.cpp
    src/shell/clipboard/clipboard_picker.h
    src/shell/clipboard/clipboard_picker_view.cpp
    src/shell/clipboard/clipboard_picker_view.h
)
target_link_libraries(cutefish-shell-clipboard-view PUBLIC cutefish-shell cutefish-shell-clipboard)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-clipboard-view)

# The sheets offering force quit for applications that stopped responding.
add_library(cutefish-shell-notresponding STATIC
    src/shell/notresponding/not_responding_sheets.cpp
    src/shell/notresponding/not_responding_sheets.h
)
target_link_libraries(cutefish-shell-notresponding PUBLIC cutefish-shell)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-notresponding)
# The screen capture interface: org.cutefish.Screenshot, its overlays,
# toolbar and Options, pictures and recordings.
add_library(cutefish-shell-screenshot STATIC
    src/shell/screenshot/capture_clipboard.cpp
    src/shell/screenshot/capture_clipboard.h
    src/shell/screenshot/capture_notifier.cpp
    src/shell/screenshot/capture_notifier.h
    src/shell/screenshot/capture_overlay.cpp
    src/shell/screenshot/capture_overlay.h
    src/shell/screenshot/capture_settings.cpp
    src/shell/screenshot/capture_settings.h
    src/shell/screenshot/capture_toolbar.cpp
    src/shell/screenshot/capture_toolbar.h
    src/shell/screenshot/screen_capture.cpp
    src/shell/screenshot/screen_capture.h
    src/shell/screenshot/screen_grabber.cpp
    src/shell/screenshot/screen_grabber.h
    src/shell/screenshot/screen_recording.cpp
    src/shell/screenshot/screen_recording.h
)
target_link_libraries(cutefish-shell-screenshot PUBLIC cutefish-shell cutefish-shell-common
    cutefish-wayland cutefish-wayland-private-shell Cutefish::AudioClient)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-screenshot)
# The session's secret agents (NetworkManager, BlueZ) and their prompts.
find_package(CutefishClients REQUIRED COMPONENTS networkmanager bluez)
add_library(cutefish-shell-secrets STATIC
    src/shell/secrets/bluetooth_pairing_dialog.cpp
    src/shell/secrets/bluetooth_pairing_dialog.h
    src/shell/secrets/network_dialogs.cpp
    src/shell/secrets/network_dialogs.h
    src/shell/secrets/network_secret_agent.cpp
    src/shell/secrets/network_secret_agent.h
    src/shell/secrets/secret_dialog.cpp
    src/shell/secrets/secret_dialog.h
    src/shell/secrets/secret_prompts.cpp
    src/shell/secrets/secret_prompts.h
    src/shell/secrets/secret_store.cpp
    src/shell/secrets/secret_store.h
    src/shell/secrets/vpn_secret_fields.cpp
    src/shell/secrets/vpn_secret_fields.h
)
target_link_libraries(cutefish-shell-secrets PUBLIC cutefish-shell
    Cutefish::NetworkManagerClient Cutefish::BluezClient)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-secrets)
# The notification banners and the notification centre on their backend.
add_library(cutefish-shell-notification-view STATIC
    src/shell/notification/notification_card.cpp
    src/shell/notification/notification_card.h
    src/shell/notification/notification_centre.cpp
    src/shell/notification/notification_centre.h
    src/shell/notification/notification_controls.cpp
    src/shell/notification/notification_controls.h
    src/shell/notification/notification_motion.cpp
    src/shell/notification/notification_motion.h
    src/shell/notification/notification_popups.cpp
    src/shell/notification/notification_popups.h
    src/shell/notification/notification_ui.cpp
    src/shell/notification/notification_ui.h
)
target_link_libraries(cutefish-shell-notification-view PUBLIC cutefish-shell cutefish-shell-notification)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-notification-view)
# The launchpad: its window, grid, folders, search and paging on the launchpad's backend.
add_library(cutefish-shell-launchpad-view STATIC
    src/shell/launchpad/launchpad_cell.cpp
    src/shell/launchpad/launchpad_cell.h
    src/shell/launchpad/launchpad_folder_view.cpp
    src/shell/launchpad/launchpad_folder_view.h
    src/shell/launchpad/launchpad_metrics.cpp
    src/shell/launchpad/launchpad_metrics.h
    src/shell/launchpad/launchpad_page_indicator.cpp
    src/shell/launchpad/launchpad_page_indicator.h
    src/shell/launchpad/launchpad_search_field.cpp
    src/shell/launchpad/launchpad_search_field.h
    src/shell/launchpad/launchpad_tween.h
    src/shell/launchpad/launchpad_view.cpp
    src/shell/launchpad/launchpad_view.h
    src/shell/launchpad/launchpad_wallpaper.cpp
    src/shell/launchpad/launchpad_wallpaper.h
    src/shell/launchpad/launchpad_window.cpp
    src/shell/launchpad/launchpad_window.h
)
target_link_libraries(cutefish-shell-launchpad-view PUBLIC cutefish-shell cutefish-shell-launchpad
    cutefish-shell-wallpaper)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-launchpad-view)
# The control centre, the status panels and the power dialog.
find_package(CutefishClients REQUIRED COMPONENTS networkmanager bluez audio)
add_library(cutefish-shell-controlcenter STATIC
    src/shell/controlcenter/control_center.cpp
    src/shell/controlcenter/control_center.h
    src/shell/controlcenter/control_center_services.h
    src/shell/controlcenter/control_center_view.cpp
    src/shell/controlcenter/control_center_view.h
    src/shell/controlcenter/level_slider.cpp
    src/shell/controlcenter/level_slider.h
    src/shell/controlcenter/panel_style.cpp
    src/shell/controlcenter/panel_style.h
    src/shell/controlcenter/power_dialog.cpp
    src/shell/controlcenter/power_dialog.h
    src/shell/controlcenter/session_services.cpp
    src/shell/controlcenter/session_services.h
    src/shell/controlcenter/status_panel.cpp
    src/shell/controlcenter/status_panel.h
    src/shell/controlcenter/tiles.cpp
    src/shell/controlcenter/tiles.h
    src/shell/menubar/menu_bar_panels.h
)
target_link_libraries(cutefish-shell-controlcenter PUBLIC cutefish-shell cutefish-shell-osd
    cutefish-shell-menubar cutefish-shell-notification Cutefish::NetworkManagerClient
    Cutefish::BluezClient Cutefish::AudioClient)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-controlcenter)
# The menu bar's items, which the lock screen shows too.
add_library(cutefish-shell-bar-items STATIC
    src/shell/menubar/bar_item.cpp
    src/shell/menubar/bar_item.h
    src/shell/menubar/menu_bar_panels.h
    src/shell/menubar/status_icon.cpp
    src/shell/menubar/status_icon.h
)
target_link_libraries(cutefish-shell-bar-items PUBLIC cutefish-shell cutefish-shell-menubar)
# The menu bar: its window, tray, status icons and clock on the bar's backend.
find_package(CutefishClients REQUIRED COMPONENTS appearance)
add_library(cutefish-shell-menubar-view STATIC
    src/shell/menubar/menu_popup.cpp
    src/shell/menubar/menu_popup.h
    src/shell/menubar/device_usage.cpp
    src/shell/menubar/device_usage.h
    src/shell/menubar/menu_bar_backdrop.cpp
    src/shell/menubar/menu_bar_backdrop.h
    src/shell/menubar/menu_bar_app_menus.h
    src/shell/menubar/menu_bar_panels.h
    src/shell/menubar/menu_bar_window.cpp
    src/shell/menubar/menu_bar_window.h
    src/shell/menubar/status_items_view.cpp
    src/shell/menubar/status_items_view.h
    src/shell/menubar/system_tray_view.cpp
    src/shell/menubar/system_tray_view.h
)
target_link_libraries(cutefish-shell-menubar-view PUBLIC cutefish-shell cutefish-shell-bar-items
    cutefish-shell-menubar cutefish-shell-wallpaper Cutefish::AudioClient)
target_link_libraries(cutefish-compositor PRIVATE cutefish-shell-menubar-view)
# The menu bar's app menus.
include(src/shell/menubar/app_menus.cmake)
