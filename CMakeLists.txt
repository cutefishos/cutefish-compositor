cmake_minimum_required(VERSION 3.22)

project(cutefish-compositor
    VERSION 1.0
    DESCRIPTION "Cutefish Wayland compositor and window manager"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
# The suite needs GoogleTest and GPU pixel tests, so it is built on request
# (-DBUILD_TESTING=ON) rather than by every plain cmake run.
option(BUILD_TESTING "Build the test suite" OFF)
include(CTest)

find_package(PkgConfig REQUIRED)
find_package(CutefishToolkit REQUIRED)
pkg_check_modules(TitleText REQUIRED IMPORTED_TARGET harfbuzz freetype2 fontconfig icu-uc icu-i18n)
pkg_check_modules(WaylandServer REQUIRED IMPORTED_TARGET wayland-server>=1.23)
pkg_check_modules(Libseat REQUIRED IMPORTED_TARGET libseat)
pkg_check_modules(Libdrm REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(Gbm REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(Egl REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GlesV2 REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(Xkbcommon REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(Libinput REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(Libudev REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(WaylandProtocols REQUIRED wayland-protocols>=1.39)
pkg_get_variable(WAYLAND_PROTOCOLS_DATADIR wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

# Generated protocol marshalling. The XML is wayland-protocols'; the generated
# sources live in the build tree and are never checked in.
set(CUTEFISH_PROTOCOL_DIR ${CMAKE_BINARY_DIR}/wayland-protocols)
file(MAKE_DIRECTORY ${CUTEFISH_PROTOCOL_DIR})

function(cutefish_wayland_protocol name xml)
    set(code ${CUTEFISH_PROTOCOL_DIR}/${name}-protocol.c)
    set(server ${CUTEFISH_PROTOCOL_DIR}/${name}-server-protocol.h)
    set(client ${CUTEFISH_PROTOCOL_DIR}/${name}-client-protocol.h)
    add_custom_command(OUTPUT ${code}
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code}
        DEPENDS ${xml} VERBATIM)
    add_custom_command(OUTPUT ${server}
        COMMAND ${WAYLAND_SCANNER} server-header ${xml} ${server}
        DEPENDS ${xml} VERBATIM)
    add_custom_command(OUTPUT ${client}
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${client}
        DEPENDS ${xml} VERBATIM)
    set(${name}_SOURCES ${code} ${server} ${client} PARENT_SCOPE)
endfunction()

cutefish_wayland_protocol(ext-idle-notify-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/ext-idle-notify/ext-idle-notify-v1.xml)
cutefish_wayland_protocol(idle-inhibit-unstable-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml)
cutefish_wayland_protocol(pointer-constraints-unstable-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml)
cutefish_wayland_protocol(pointer-gestures-unstable-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/pointer-gestures/pointer-gestures-unstable-v1.xml)
cutefish_wayland_protocol(relative-pointer-unstable-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/relative-pointer/relative-pointer-unstable-v1.xml)
cutefish_wayland_protocol(keyboard-shortcuts-inhibit-unstable-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml)

cutefish_wayland_protocol(presentation-time
    ${WAYLAND_PROTOCOLS_DATADIR}/stable/presentation-time/presentation-time.xml)
cutefish_wayland_protocol(fractional-scale-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/fractional-scale/fractional-scale-v1.xml)
cutefish_wayland_protocol(xdg-output-unstable-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/xdg-output/xdg-output-unstable-v1.xml)
cutefish_wayland_protocol(viewporter
    ${WAYLAND_PROTOCOLS_DATADIR}/stable/viewporter/viewporter.xml)
cutefish_wayland_protocol(xdg-activation-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/xdg-activation/xdg-activation-v1.xml)
cutefish_wayland_protocol(xdg-shell
    ${WAYLAND_PROTOCOLS_DATADIR}/stable/xdg-shell/xdg-shell.xml)
cutefish_wayland_protocol(xdg-decoration
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml)
cutefish_wayland_protocol(xdg-dialog-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/xdg-dialog/xdg-dialog-v1.xml)
cutefish_wayland_protocol(xdg-foreign-unstable-v2
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/xdg-foreign/xdg-foreign-unstable-v2.xml)
cutefish_wayland_protocol(xdg-toplevel-drag-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/xdg-toplevel-drag/xdg-toplevel-drag-v1.xml)

cutefish_wayland_protocol(linux-dmabuf-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/stable/linux-dmabuf/linux-dmabuf-v1.xml)

cutefish_wayland_protocol(linux-drm-syncobj-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml)

# Cutefish's own private protocols. cutefish-toolkit/protocols keeps the
# client-side copy of every one a client speaks, and the two copies must stay
# byte-identical: they define the wire format.
set(CUTEFISH_PRIVATE_PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protocols)
cutefish_wayland_protocol(cutefish-shell-v1
    ${CUTEFISH_PRIVATE_PROTOCOL_DIR}/cutefish-shell-v1.xml)
cutefish_wayland_protocol(cutefish-computer-control-v1
    ${CUTEFISH_PRIVATE_PROTOCOL_DIR}/cutefish-computer-control-v1.xml)

cutefish_wayland_protocol(cursor-shape-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/cursor-shape/cursor-shape-v1.xml)
# cursor-shape's tablet-tool argument references this interface at link time.
# Generate its descriptors only; no tablet global or capability is advertised.
cutefish_wayland_protocol(tablet-v2
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/tablet/tablet-unstable-v2.xml)
cutefish_wayland_protocol(cutefish-window-hints-v1
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/cutefish-window-hints-v1.xml)
cutefish_wayland_protocol(cutefish-surface-blur-v1
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/cutefish-surface-blur-v1.xml)
cutefish_wayland_protocol(cutefish-status-item-v1
    ${CUTEFISH_PRIVATE_PROTOCOL_DIR}/cutefish-status-item-v1.xml)
cutefish_wayland_protocol(cutefish-main-menu-v1
    ${CUTEFISH_PRIVATE_PROTOCOL_DIR}/cutefish-main-menu-v1.xml)
cutefish_wayland_protocol(cutefish-window-placement-v1
    ${CUTEFISH_PRIVATE_PROTOCOL_DIR}/cutefish-window-placement-v1.xml)
cutefish_wayland_protocol(ext-data-control-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/ext-data-control/ext-data-control-v1.xml)
cutefish_wayland_protocol(wlr-data-control-unstable-v1
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-data-control-unstable-v1.xml)
cutefish_wayland_protocol(primary-selection-unstable-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/primary-selection/primary-selection-unstable-v1.xml)
cutefish_wayland_protocol(appmenu ${CMAKE_CURRENT_SOURCE_DIR}/protocols/appmenu.xml)
cutefish_wayland_protocol(ext-image-capture-source-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/ext-image-capture-source/ext-image-capture-source-v1.xml)
cutefish_wayland_protocol(ext-image-copy-capture-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml)
cutefish_wayland_protocol(ext-foreign-toplevel-list-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml)
cutefish_wayland_protocol(text-input-unstable-v1
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/text-input/text-input-unstable-v1.xml)
cutefish_wayland_protocol(text-input-unstable-v3
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/text-input/text-input-unstable-v3.xml)
cutefish_wayland_protocol(input-method-unstable-v2
    ${CUTEFISH_PRIVATE_PROTOCOL_DIR}/input-method-unstable-v2.xml)
cutefish_wayland_protocol(virtual-keyboard-unstable-v1
    ${CUTEFISH_PRIVATE_PROTOCOL_DIR}/virtual-keyboard-unstable-v1.xml)
add_library(cutefish-protocols STATIC
    ${text-input-unstable-v1_SOURCES}
    ${text-input-unstable-v3_SOURCES}
    ${input-method-unstable-v2_SOURCES}
    ${virtual-keyboard-unstable-v1_SOURCES}
    ${ext-idle-notify-v1_SOURCES}
    ${idle-inhibit-unstable-v1_SOURCES}
    ${pointer-constraints-unstable-v1_SOURCES}
    ${pointer-gestures-unstable-v1_SOURCES}
    ${relative-pointer-unstable-v1_SOURCES}
    ${keyboard-shortcuts-inhibit-unstable-v1_SOURCES}
    ${ext-image-capture-source-v1_SOURCES} ${ext-image-copy-capture-v1_SOURCES}
    ${ext-foreign-toplevel-list-v1_SOURCES}
    ${xdg-activation-v1_SOURCES}
    ${appmenu_SOURCES}
    ${primary-selection-unstable-v1_SOURCES}
    ${presentation-time_SOURCES}
    ${viewporter_SOURCES}
    ${fractional-scale-v1_SOURCES}
    ${xdg-output-unstable-v1_SOURCES}
    ${ext-data-control-v1_SOURCES} ${wlr-data-control-unstable-v1_SOURCES}
    ${cutefish-window-hints-v1_SOURCES} ${cutefish-surface-blur-v1_SOURCES} ${xdg-shell_SOURCES} ${xdg-decoration_SOURCES}
    ${cutefish-status-item-v1_SOURCES}
    ${cutefish-main-menu-v1_SOURCES}
    ${cutefish-window-placement-v1_SOURCES}
    ${xdg-dialog-v1_SOURCES}
    ${xdg-foreign-unstable-v2_SOURCES}
    ${xdg-toplevel-drag-v1_SOURCES}
    ${cursor-shape-v1_SOURCES} ${tablet-v2_SOURCES} ${linux-dmabuf-v1_SOURCES}
    ${linux-drm-syncobj-v1_SOURCES} ${cutefish-shell-v1_SOURCES}
    ${cutefish-computer-control-v1_SOURCES})
target_include_directories(cutefish-protocols PUBLIC ${CUTEFISH_PROTOCOL_DIR})
target_link_libraries(cutefish-protocols PUBLIC PkgConfig::WaylandServer)
set_target_properties(cutefish-protocols PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Shared logging categories, preference files, process environment and the
# main thread's toolkit loop.
add_library(cutefish-core STATIC
    src/core/logging.cpp
    src/core/logging.h
    src/core/main_loop.cpp
    src/core/main_loop.h
    src/core/preferences.cpp
    src/core/preferences.h
    src/core/process_environment.cpp
    src/core/process_environment.h
)
target_include_directories(cutefish-core PUBLIC src)
target_link_libraries(cutefish-core PUBLIC Cutefish::ToolkitBase)
set_target_properties(cutefish-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Backend-independent display contracts. DRM, Wayland and UI adapters consume
# this library without becoming part of the state and geometry model.
add_library(cutefish-display STATIC
    src/display/callback_native_display_delegate.cpp
    src/display/callback_native_display_delegate.h
    src/display/display_configuration_controller.cpp
    src/display/display_configuration_controller.h
    src/display/display_configurator.cpp
    src/display/display_configurator.h
    src/display/display_configuration_validator.cpp
    src/display/display_configuration_validator.h
    src/display/display_identity.cpp
    src/display/display_identity.h
    src/display/display_topology_coordinator.cpp
    src/display/display_topology_coordinator.h
    src/display/display_manager.cpp
    src/display/display_manager.h
    src/display/display_preferences.cpp
    src/display/display_preferences.h
    src/display/display_layout.cpp
    src/display/display_layout.h
    src/display/display_transform.cpp
    src/display/display_transform.h
    src/display/mirror_transform.cpp
    src/display/mirror_transform.h
    src/display/display_types.cpp
    src/display/display_types.h
    src/display/edid.cpp
    src/display/edid.h
    src/display/native_display_delegate.h
    src/display/night_light.cpp
    src/display/night_light.h
    src/display/recommended_scale.cpp
    src/display/recommended_scale.h
)
target_include_directories(cutefish-display PUBLIC src)
target_link_libraries(cutefish-display PUBLIC cutefish-core Cutefish::ToolkitBase)
set_target_properties(cutefish-display PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(cutefish-display-ipc STATIC
    src/ipc/display_config_service.cpp
    src/ipc/display_config_service.h
    src/ipc/display_dbus_types.cpp
    src/ipc/display_dbus_types.h
)
target_include_directories(cutefish-display-ipc PUBLIC src)
target_link_libraries(cutefish-display-ipc PUBLIC cutefish-display)
set_target_properties(cutefish-display-ipc PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Protocol/compositor core, shared by the executable and the tests.
add_library(cutefish-wayland STATIC
    src/wayland/client_tracker.cpp
    src/wayland/client_tracker.h
    src/wayland/scoped_wl.h
    src/wayland/resource_reference.h
    src/wayland/serial_tracker.cpp
    src/wayland/serial_tracker.h
    src/wayland/server.cpp
    src/wayland/server.h
    src/wayland/server_util.cpp
    src/wayland/server_util.h
    src/wayland/security_delegate.cpp
    src/wayland/security_delegate.h
)

target_include_directories(cutefish-wayland PUBLIC src)
target_link_libraries(cutefish-wayland PUBLIC cutefish-core PkgConfig::WaylandServer)
set_target_properties(cutefish-wayland PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Explicit-synchronization primitives: sync_file fences and DRM syncobj
# timelines. libdrm is the only dependency; nothing here knows about Wayland.
add_library(cutefish-sync STATIC
    src/render/sync_fence.cpp
    src/render/sync_fence.h
    src/render/drm_syncobj.cpp
    src/render/drm_syncobj.h
)
target_include_directories(cutefish-sync PUBLIC src)
target_link_libraries(cutefish-sync PUBLIC cutefish-core PkgConfig::Libdrm)
set_target_properties(cutefish-sync PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Scene graph. Deliberately free of GL and DRM so it stays testable.
add_library(cutefish-scene STATIC
    src/scene/window_visual_node.cpp
    src/scene/motion.cpp
    src/scene/motion.h
    src/scene/snap_preview_node.cpp
    src/scene/snap_preview_node.h
    src/scene/screen_transition_node.cpp
    src/scene/screen_transition_node.h
    src/scene/desk_switch_node.cpp
    src/scene/desk_switch_node.h
    src/scene/finger_track.cpp
    src/scene/finger_track.h
    src/scene/overview_node.cpp
    src/scene/overview_node.h
    src/scene/slide_out_node.cpp
    src/scene/slide_out_node.h
    src/scene/window_animation.cpp
    src/scene/window_animation.h
    src/scene/node.cpp
    src/scene/node.h
    src/scene/scene.cpp
    src/scene/scene.h
    src/scene/surface_node.cpp
    src/scene/surface_node.h
    src/scene/wallpaper_node.cpp
    src/scene/wallpaper_node.h
    src/scene/overlay_candidate.cpp
    src/scene/overlay_candidate.h
    src/render/cursor_sink.h
    src/render/scanout.h
    src/render/renderer.cpp
    src/render/renderer.h
    src/render/rounded_shape.cpp
    src/render/rounded_shape.h
    src/render/shape_mask.cpp
    src/render/shape_mask.h
    src/render/output_render_context.h
    src/render/dmabuf.h
    src/render/texture.h
)
target_include_directories(cutefish-scene PUBLIC src)
target_link_libraries(cutefish-scene PUBLIC cutefish-core cutefish-sync Cutefish::ToolkitBase Cutefish::Toolkit)
set_target_properties(cutefish-scene PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Compositor model and the Wayland protocol implementations that drive it.
add_library(cutefish-compositor-core STATIC
    src/compositor/buffer.cpp
    src/compositor/buffer.h
    src/compositor/dmabuf_buffer.cpp
    src/compositor/dmabuf_buffer.h
    src/compositor/compositor.cpp
    src/compositor/compositor.h
    src/compositor/shm_pool.cpp
    src/compositor/shm_pool.h
    src/compositor/sub_surface.cpp
    src/compositor/sub_surface.h
    src/wayland/protocols/wp_presentation.cpp
    src/compositor/output.h
    src/compositor/output_scale.h
    src/wayland/protocols/wp_fractional_scale.cpp
    src/wayland/protocols/wp_fractional_scale.h
    src/wayland/protocols/wl_subcompositor.cpp
    src/wayland/protocols/wl_subcompositor.h
    src/wayland/protocols/image_capture.cpp
    src/compositor/capture_source.h
    src/wayland/protocols/wl_output.cpp
    src/wayland/protocols/wl_output.h
    src/wayland/protocols/wp_viewporter.cpp
    src/wayland/protocols/wp_viewporter.h
    src/compositor/surface.cpp
    src/compositor/surface.h
    src/compositor/menu_model.h
    src/wayland/protocols/wl_compositor.cpp
    src/wayland/protocols/wl_compositor.h
    src/wayland/protocols/wl_shm.cpp
    src/wayland/protocols/wl_shm.h
)
target_include_directories(cutefish-compositor-core PUBLIC src)
# Cutefish::Toolkit for the blur shape's ui::Path.
target_link_libraries(cutefish-compositor-core PUBLIC cutefish-wayland cutefish-scene cutefish-protocols
    Cutefish::Toolkit)
set_target_properties(cutefish-compositor-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Compositor-side input: seat, pointer, keyboard, xkb. Free of libinput and of
# the Wayland protocol so it stays testable.
# The session bus services of input, shortcuts and cursors.
add_library(cutefish-input-dbus STATIC src/input/dbus_export.cpp src/input/dbus_export.h)
target_include_directories(cutefish-input-dbus PUBLIC src)
target_link_libraries(cutefish-input-dbus PUBLIC cutefish-core Cutefish::ToolkitBase)

add_library(cutefish-mouse-settings STATIC
    src/input/mouse_settings.cpp
    src/input/mouse_settings.h
    src/input/pointer_acceleration.cpp
    src/input/pointer_acceleration.h
    src/input/touchpad_click.cpp
    src/input/touchpad_click.h
    src/input/touchpad_settings.cpp
    src/input/touchpad_settings.h
    src/input/touchscreen_settings.cpp
    src/input/touchscreen_settings.h
)
target_include_directories(cutefish-mouse-settings PUBLIC src)
target_link_libraries(cutefish-mouse-settings PUBLIC cutefish-input-dbus)

add_library(cutefish-keyboard-settings STATIC
    src/input/keyboard_settings.cpp
    src/input/keyboard_settings.h
)
target_include_directories(cutefish-keyboard-settings PUBLIC src)
target_link_libraries(cutefish-keyboard-settings PUBLIC cutefish-input-dbus)

add_library(cutefish-input-method-settings STATIC
    src/input/input_method_settings.cpp
    src/input/input_method_settings.h
)
target_include_directories(cutefish-input-method-settings PUBLIC src)
target_link_libraries(cutefish-input-method-settings PUBLIC cutefish-input-dbus)

add_library(cutefish-input STATIC
    src/clipboard/selection.cpp
    src/clipboard/selection.h
    src/clipboard/stored_source.cpp
    src/clipboard/stored_source.h
    src/input/types.cpp
    src/input/types.h
    src/input/xkb_tracker.cpp
    src/input/xkb_tracker.h
    src/input/key_translator.cpp
    src/input/key_translator.h
    src/input/hit_test.cpp
    src/input/hit_test.h
    src/input/keyboard_delegate.h
    src/input/pointer_delegate.h
    src/input/seat_observer.h
    src/input/drag.cpp
    src/input/drag.h
    src/input/seat.cpp
    src/input/seat.h
    src/input/keyboard.cpp
    src/input/keyboard.h
    src/input/pointer.cpp
    src/input/pointer.h
    src/input/touch.cpp
    src/input/touch.h
    src/input/touch_delegate.h
    src/input/pointer_constraint_delegate.h
    src/input/pointer_constraints.cpp
    src/input/pointer_constraints.h
    src/input/gesture_filter.h
    src/input/pointer_gesture_delegate.h
    src/input/relative_pointer_delegate.h
    src/input/shortcuts_inhibition.cpp
    src/input/shortcuts_inhibition.h
    src/input/event_sink.h
    src/input/input_manager.cpp
    src/input/input_manager.h
)
target_include_directories(cutefish-input PUBLIC src)
target_link_libraries(cutefish-input PUBLIC cutefish-display cutefish-compositor-core Cutefish::ToolkitBase
    PkgConfig::Xkbcommon)
set_target_properties(cutefish-input PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Window manager. Above the compositor and the input model, below the Wayland
# shell protocols: nothing here knows about xdg-shell or wl_resource.
add_library(cutefish-wm STATIC
    src/wm/types.cpp
    src/wm/types.h
    src/wm/wm_event.cpp
    src/wm/wm_event.h
    src/wm/anchored_placement.cpp
    src/wm/anchored_placement.h
    src/wm/placement.cpp
    src/wm/placement.h
    src/wm/stacking.cpp
    src/wm/stacking.h
    src/wm/work_area.cpp
    src/wm/work_area.h
    src/wm/window_observer.h
    src/wm/window_visual_controller.cpp
    src/wm/window_visual_controller.h
    src/decoration/window_visual_style.cpp
    src/decoration/window_visual_style.h
    src/wm/window.cpp
    src/wm/window.h
    src/wm/window_shadow.h
    src/wm/shadow_types.cpp
    src/wm/shadow_types.h
    src/wm/shadow_controller_delegate.h
    src/wm/shadow_controller.cpp
    src/wm/shadow_controller.h
    src/wm/wm_shadow_controller_delegate.cpp
    src/wm/wm_shadow_controller_delegate.h
    src/wm/window_state.cpp
    src/wm/window_state.h
    src/wm/activation.cpp
    src/wm/activation.h
    src/wm/transient_manager.cpp
    src/wm/transient_manager.h
    src/wm/window_modality_controller.cpp
    src/wm/window_modality_controller.h
    src/wm/window_positioner.cpp
    src/wm/window_positioner.h
    src/wm/window_snap.cpp
    src/wm/window_snap.h
    src/wm/resize_handles.cpp
    src/wm/resize_handles.h
    src/wm/window_resizer.cpp
    src/wm/window_resizer.h
    src/wm/snap_drag_controller.cpp
    src/wm/snap_drag_controller.h
    src/wm/toplevel_event_handler.cpp
    src/wm/toplevel_event_handler.h
    src/wm/toplevel_drag.cpp
    src/wm/toplevel_drag.h
    src/wm/window_manager.cpp
    src/wm/window_manager.h
    src/wm/quit_all.cpp
    src/wm/quit_all.h
    src/wm/dock_layout_manager.cpp
    src/wm/dock_layout_manager.h
    src/wm/dock_preferences.cpp
    src/wm/dock_preferences.h
    src/wm/shell_surface.cpp
    src/wm/shell_surface.h
    src/wm/show_desktop_controller.cpp
    src/wm/show_desktop_controller.h
    src/wm/status_items.cpp
    src/wm/status_items.h
    src/wm/mru_window_tracker.cpp
    src/wm/mru_window_tracker.h
    src/wm/window_cycle_controller.cpp
    src/wm/window_cycle_controller.h
    src/wm/desks/desk.cpp
    src/wm/desks/desk.h
    src/wm/desks/desk_animation.cpp
    src/wm/desks/desk_animation.h
    src/wm/desks/desks_controller.cpp
    src/wm/desks/desks_controller.h
    src/wm/desks/desk_preferences.cpp
    src/wm/desks/desk_preferences.h
    src/wm/gestures/wm_gesture_handler.cpp
    src/wm/gestures/wm_gesture_handler.h
    src/wm/overview/overview_controller.cpp
    src/wm/overview/overview_controller.h
    src/wm/overview/overview_layout.cpp
    src/wm/overview/overview_layout.h
    # Development shortcuts for exercising the window manager.
    src/wm/demo_controller.cpp
    src/wm/demo_controller.h
)
target_include_directories(cutefish-wm PUBLIC src)
target_link_libraries(cutefish-wm PUBLIC cutefish-input cutefish-compositor-core cutefish-display
    cutefish-core Cutefish::ToolkitBase)
set_target_properties(cutefish-wm PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Server-side window decoration. Above the window manager and the scene, below
# the Wayland protocols: nothing here knows about xdg-decoration.
add_library(cutefish-decoration STATIC
    src/decoration/shadow.cpp
    src/decoration/shadow.h
    src/decoration/shadow_node.cpp
    src/decoration/shadow_node.h
    src/decoration/raster.cpp
    src/decoration/raster.h
    src/decoration/text.cpp
    src/decoration/text.h
    src/decoration/decoration_theme.cpp
    src/decoration/decoration_theme.h
    src/decoration/overview_window_frame.cpp
    src/decoration/overview_window_frame.h
    src/decoration/caption_button.cpp
    src/decoration/caption_button.h
    src/decoration/decoration_node.cpp
    src/decoration/decoration_node.h
    src/decoration/decoration.cpp
    src/decoration/decoration.h
    src/decoration/theme_preferences.cpp
    src/decoration/theme_preferences.h
)
target_include_directories(cutefish-decoration PUBLIC src)
target_link_libraries(cutefish-decoration PUBLIC cutefish-wm cutefish-scene Cutefish::Toolkit
    PRIVATE PkgConfig::TitleText)
set_target_properties(cutefish-decoration PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The wl_seat protocol adapters live with the other protocol code but need the
# input model, so they are a separate target rather than part of
# cutefish-compositor-core.
add_library(cutefish-wayland-input STATIC
    src/wayland/protocols/input_method.cpp
    src/wayland/protocols/input_method.h
    src/wayland/protocols/text_input.h
    src/wayland/protocols/text_input_v3.cpp
    src/wayland/protocols/zwp_text_input_v1.cpp
    src/wayland/input_method_launcher.cpp
    src/wayland/input_method_launcher.h
    src/wayland/protocols/idle_notify.cpp
    src/wayland/protocols/zwp_idle_inhibit_manager.cpp
    src/wayland/protocols/zwp_keyboard_shortcuts_inhibit_manager.cpp
    src/wayland/protocols/zwp_keyboard_shortcuts_inhibit_manager.h
    src/wayland/protocols/zwp_pointer_constraints.cpp
    src/wayland/protocols/zwp_pointer_gestures.cpp
    src/wayland/protocols/zwp_pointer_gestures.h
    src/wayland/protocols/zwp_pointer_constraints.h
    src/wayland/protocols/zwp_relative_pointer_manager.cpp
    src/wayland/protocols/zwp_relative_pointer_manager.h
    src/wayland/idle_inhibition.cpp
    src/wayland/idle_inhibition.h
    src/wayland/protocols/cursor_shape.cpp
    src/wayland/protocols/wl_seat.cpp
    src/wayland/protocols/wl_seat.h
    src/wayland/protocols/wayland_keyboard_delegate.cpp
    src/wayland/protocols/wayland_keyboard_delegate.h
    src/wayland/protocols/wayland_pointer_delegate.cpp
    src/wayland/protocols/wayland_pointer_delegate.h
    src/wayland/protocols/wayland_touch_delegate.cpp
    src/wayland/protocols/wayland_touch_delegate.h
    src/wayland/protocols/data_control.cpp
    src/wayland/protocols/data_control.h
    src/wayland/protocols/primary_selection.cpp
    src/wayland/protocols/primary_selection.h
    src/wayland/protocols/wl_data_device_manager.cpp
    src/wayland/protocols/wl_data_device_manager.h
)
target_include_directories(cutefish-wayland-input PUBLIC src)
target_link_libraries(cutefish-wayland-input PUBLIC cutefish-input cutefish-wayland)
set_target_properties(cutefish-wayland-input PROPERTIES POSITION_INDEPENDENT_CODE ON)

# xdg-shell: the protocol tables and the positioner. Above the window manager,
# which is where all shell policy stays.
# Session state and the secure scene container. The controller and the container
# know nothing about windows; only the teardown reaches into the window manager,
# and only to drop what a lock must not let survive.
add_library(cutefish-session STATIC
    src/session/applications_service.cpp
    src/session/applications_service.h
    src/session/power_button.cpp
    src/session/power_button.h
    src/session/screen_saver_service.cpp
    src/session/screen_saver_service.h
    src/session/secure_container.cpp
    src/session/secure_container.h
    src/session/session_controller.cpp
    src/session/session_controller.h
    src/session/session_state.h
    src/session/session_teardown.cpp
    src/session/session_teardown.h
    src/session/sleep_lock.cpp
    src/session/sleep_lock.h
    src/session/window_debug_service.cpp
    src/session/window_debug_service.h
)
target_include_directories(cutefish-session PUBLIC src)
target_link_libraries(cutefish-session PUBLIC cutefish-compositor-core cutefish-wm Cutefish::Toolkit)
set_target_properties(cutefish-session PROPERTIES POSITION_INDEPENDENT_CODE ON)

# An agent driving the seat once the user agrees, over
# cutefish-computer-control-v1: consent, injection, and the pause on real input.
add_library(cutefish-computer-control STATIC
    src/computer_control/controller.cpp
    src/computer_control/controller.h
    src/computer_control/permission_consent.cpp
    src/computer_control/permission_consent.h
    src/computer_control/remembered_apps.cpp
    src/computer_control/remembered_apps.h
)
target_include_directories(cutefish-computer-control PUBLIC src)
target_link_libraries(cutefish-computer-control PUBLIC cutefish-input cutefish-input-dbus Cutefish::ToolkitBase)
set_target_properties(cutefish-computer-control PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Keyboard shortcuts: the Ash accelerator controller and its configuration,
# above the window manager and the session, exposed to Settings over D-Bus.
add_library(cutefish-accelerators STATIC
    src/accelerators/accelerator.cpp
    src/accelerators/accelerator.h
    src/accelerators/accelerator_actions.cpp
    src/accelerators/accelerator_actions.h
    src/accelerators/accelerator_commands.cpp
    src/accelerators/accelerator_commands.h
    src/accelerators/accelerator_configuration.cpp
    src/accelerators/accelerator_configuration.h
    src/accelerators/accelerator_controller.cpp
    src/accelerators/accelerator_controller.h
    src/accelerators/accelerator_delegate.cpp
    src/accelerators/accelerator_delegate.h
    src/accelerators/accelerator_filter.cpp
    src/accelerators/accelerator_filter.h
    src/accelerators/accelerator_history.cpp
    src/accelerators/accelerator_history.h
    src/accelerators/accelerator_launcher_state_machine.cpp
    src/accelerators/accelerator_launcher_state_machine.h
    src/accelerators/accelerator_manager.cpp
    src/accelerators/accelerator_manager.h
    src/accelerators/accelerator_map.h
    src/accelerators/accelerator_service.cpp
    src/accelerators/accelerator_service.h
    src/accelerators/accelerator_table.h
    src/accelerators/hot_corner_controller.cpp
    src/accelerators/hot_corner_controller.h
    src/accelerators/hot_corner_settings.cpp
    src/accelerators/hot_corner_settings.h
)
target_include_directories(cutefish-accelerators PUBLIC src)
target_link_libraries(cutefish-accelerators PUBLIC cutefish-wm cutefish-session cutefish-input-dbus PkgConfig::Xkbcommon)
set_target_properties(cutefish-accelerators PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(cutefish-wayland-shell STATIC
    src/wayland/protocols/dmabuf_scanout_policy.cpp
    src/wayland/protocols/dmabuf_scanout_policy.h
    src/wayland/protocols/xdg_activation.cpp
    src/wayland/protocols/xdg_activation.h
    src/wayland/protocols/application_menu.cpp
    src/wayland/protocols/main_menu.cpp
    src/wayland/protocols/main_menu.h
    src/wayland/protocols/xdg_positioner.cpp
    src/wayland/protocols/xdg_positioner.h
    src/wayland/protocols/xdg_shell.cpp
    src/wayland/protocols/xdg_shell.h
    src/wayland/protocols/status_item.cpp
    src/wayland/protocols/status_item.h
    src/wayland/protocols/window_hints.cpp
    src/wayland/protocols/window_hints.h
    src/wayland/protocols/window_placement.cpp
    src/wayland/protocols/window_placement.h
    src/wayland/protocols/surface_blur.cpp
    src/wayland/protocols/surface_blur.h
    src/wayland/protocols/xdg_decoration.cpp
    src/wayland/protocols/xdg_decoration.h
    src/wayland/protocols/xdg_dialog.cpp
    src/wayland/protocols/xdg_dialog.h
    src/wayland/protocols/xdg_foreign.cpp
    src/wayland/protocols/xdg_foreign.h
    src/wayland/protocols/xdg_toplevel_drag.cpp
    src/wayland/protocols/xdg_toplevel_drag.h
)
target_include_directories(cutefish-wayland-shell PUBLIC src)
target_link_libraries(cutefish-wayland-shell
    PUBLIC cutefish-wm cutefish-wayland cutefish-protocols cutefish-wayland-input
           cutefish-wayland-dmabuf)
set_target_properties(cutefish-wayland-shell PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The private shell protocols. Above the window manager, like xdg-shell: all
# policy stays in wm/, and no wl_resource crosses into it.
add_library(cutefish-wayland-private-shell STATIC
    src/wayland/privileged_client.cpp
    src/wayland/privileged_client.h
    src/wayland/shell_control_channel.cpp
    src/wayland/shell_control_channel.h
    src/wayland/protocols/cutefish_shell.cpp
    src/wayland/protocols/cutefish_shell.h
    src/wayland/protocols/foreign_toplevel_list.cpp
    src/wayland/protocols/foreign_toplevel_list.h
)
target_include_directories(cutefish-wayland-private-shell PUBLIC src)
target_link_libraries(cutefish-wayland-private-shell
    PUBLIC cutefish-wm cutefish-wayland cutefish-protocols)
set_target_properties(cutefish-wayland-private-shell PROPERTIES POSITION_INDEPENDENT_CODE ON)

# linux-dmabuf-v1. Needs the compositor model, the generated protocol tables and
# the DRM fourcc definitions, but nothing from the renderer.
add_library(cutefish-wayland-dmabuf STATIC
    src/wayland/protocols/dmabuf_feedback_manager.cpp
    src/wayland/protocols/dmabuf_feedback_manager.h
    src/wayland/protocols/linux_dmabuf.cpp
    src/wayland/protocols/linux_dmabuf.h
    src/wayland/protocols/linux_drm_syncobj.cpp
    src/wayland/protocols/linux_drm_syncobj.h
)
target_include_directories(cutefish-wayland-dmabuf PUBLIC src)
target_link_libraries(cutefish-wayland-dmabuf
    PUBLIC cutefish-compositor-core cutefish-wayland cutefish-protocols cutefish-sync
    PRIVATE PkgConfig::Libdrm)
set_property(TARGET cutefish-wayland-dmabuf PROPERTY POSITION_INDEPENDENT_CODE ON)

# EGL/GLES renderer on the GBM platform.
set(CUTEFISH_SHADER_NAMES
    color_vertex_shader color_fragment_shader
    texture_vertex_shader texture_fragment_shader
    blur_fragment_shader blur_mask_fragment_shader glass_fragment_shader
    external_fragment_shader external_extension
    rounded_clip clipped_precision clipped_main)
set(CUTEFISH_SHADER_FILES)
foreach(name IN LISTS CUTEFISH_SHADER_NAMES)
    list(APPEND CUTEFISH_SHADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/${name}.glsl")
endforeach()
set(CUTEFISH_SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/render_shaders.h")
add_custom_command(OUTPUT "${CUTEFISH_SHADER_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        "-DOUTPUT=${CUTEFISH_SHADER_HEADER}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders"
        "-DSHADERS=${CUTEFISH_SHADER_NAMES}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/embed.cmake"
    DEPENDS ${CUTEFISH_SHADER_FILES} src/render/shaders/embed.cmake
    VERBATIM)
add_library(cutefish-render STATIC
    "${CUTEFISH_SHADER_HEADER}"
    src/render/egl_renderer.cpp
    src/render/egl_renderer.h
    src/render/egl_context.cpp
    src/render/egl_context.h
    src/render/egl_render_target.cpp
    src/render/egl_render_target.h
    src/render/egl_fence.cpp
    src/render/egl_fence.h
    src/render/gpu_timer.cpp
    src/render/gpu_timer.h
    src/render/egl_dmabuf_importer.cpp
    src/render/egl_dmabuf_importer.h
    src/render/gl_texture.cpp
    src/render/gl_texture.h
    src/render/gl_program.cpp
    src/render/gl_program.h
    src/render/gl_render_pass.cpp
    src/render/gl_render_pass.h
    src/render/backdrop_filter.cpp
    src/render/backdrop_filter.h
    src/render/drop_shadow.cpp
    src/render/drop_shadow.h
    src/render/gaussian_kernel.cpp
    src/render/gaussian_kernel.h
    src/render/color_transform.h
    src/render/texture.h
)
target_include_directories(cutefish-render PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(cutefish-render
    PUBLIC cutefish-scene PkgConfig::Egl PkgConfig::GlesV2
    PRIVATE PkgConfig::Gbm PkgConfig::Libdrm
)
set_target_properties(cutefish-render PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Session and DRM/KMS backend.
add_library(cutefish-backend STATIC
    src/backend/drm/drm_commit_coordinator.cpp
    src/backend/drm/drm_commit_coordinator.h
    src/backend/drm/drm_configuration_planner.cpp
    src/backend/drm/drm_configuration_planner.h
    src/backend/drm/drm_mode_fallback_planner.cpp
    src/backend/drm/drm_mode_fallback_planner.h
    src/backend/drm/drm_multi_device_planner.cpp
    src/backend/drm/drm_multi_device_planner.h
    src/backend/drm/drm_hotplug_monitor.cpp
    src/backend/drm/drm_hotplug_monitor.h
    src/backend/session/session.h
    src/backend/session/libseat_session.cpp
    src/backend/session/libseat_session.h
    src/backend/drm/drm_backend.cpp
    src/backend/drm/drm_backend.h
    src/backend/drm/drm_device_api.h
    src/backend/drm/drm_device.cpp
    src/backend/drm/drm_device.h
    src/backend/drm/drm_plane.cpp
    src/backend/drm/drm_plane.h
    src/backend/drm/plane_assignment.cpp
    src/backend/drm/plane_assignment.h
    src/backend/drm/drm_cursor.cpp
    src/backend/drm/drm_cursor.h
    src/backend/drm/scanout_controller.cpp
    src/backend/drm/scanout_controller.h
    src/backend/drm/drm_framebuffer.cpp
    src/backend/drm/drm_framebuffer.h
    src/backend/drm/drm_output.cpp
    src/backend/drm/drm_output.h
    src/backend/drm/drm_properties.cpp
    src/backend/drm/drm_properties.h
    src/backend/drm/drm_selection.cpp
    src/backend/drm/drm_selection.h
    src/backend/input/libinput_backend.cpp
    src/backend/input/libinput_backend.h
    src/backend/input/touchpad_contacts.cpp
    src/backend/input/touchpad_contacts.h
)
target_link_libraries(cutefish-backend
    PUBLIC cutefish-core cutefish-render cutefish-input cutefish-mouse-settings PkgConfig::Libdrm
    PRIVATE PkgConfig::Libseat PkgConfig::Gbm PkgConfig::Libinput PkgConfig::Libudev
)
set_target_properties(cutefish-backend PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(cutefish-output-scheduler STATIC src/render/output_scheduler.cpp)
target_include_directories(cutefish-output-scheduler PUBLIC src)
target_link_libraries(cutefish-backend PUBLIC cutefish-output-scheduler)

add_library(cutefish-display-runtime STATIC src/display/display_runtime.cpp)
target_link_libraries(cutefish-display-runtime PUBLIC cutefish-backend cutefish-wayland-shell
    cutefish-wm cutefish-display cutefish-wayland-input cutefish-cursor)
# The toolkit's application inside the compositor, for the panels it draws itself.
add_library(cutefish-toolkit-host STATIC
    src/toolkit/toolkit_host.cpp src/toolkit/toolkit_host.h
    src/toolkit/compositor_colors.cpp src/toolkit/compositor_colors.h)
target_include_directories(cutefish-toolkit-host PUBLIC src)
target_link_libraries(cutefish-toolkit-host PUBLIC Cutefish::Toolkit cutefish-render)

find_library(PAM_LIBRARY pam REQUIRED)
add_executable(cutefish-auth-helper src/auth/auth_helper.cpp)
target_link_libraries(cutefish-auth-helper PRIVATE ${PAM_LIBRARY})
install(TARGETS cutefish-auth-helper RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES data/cutefish-auth DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d)
add_library(cutefish-auth STATIC
    src/auth/auth_status.cpp src/auth/auth_status.h
    src/auth/auth_ui.cpp src/auth/auth_ui.h
    src/auth/auth_view.cpp src/auth/auth_view.h
    src/auth/auth_controller.cpp src/auth/auth_controller.h src/auth/auth_backend.h
    src/auth/pam_backend.cpp src/auth/pam_backend.h
    src/auth/greetd_backend.cpp src/auth/greetd_backend.h
    src/auth/login_sessions.cpp src/auth/login_sessions.h)
target_include_directories(cutefish-auth PUBLIC src)
# The status bar's items and backdrop rules come from the shell's libraries,
# defined further down.
find_package(CutefishClients REQUIRED COMPONENTS accounts)
target_link_libraries(cutefish-auth PUBLIC cutefish-session cutefish-toolkit-host
    cutefish-shell-bar-items cutefish-shell-wallpaper PRIVATE PkgConfig::TitleText Cutefish::AccountsClient)
target_compile_definitions(cutefish-auth PRIVATE
    CUTEFISH_AUTH_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/cutefish-auth-helper")

# The application switcher's list, drawn by the compositor as the lock screen is.
add_library(cutefish-switcher STATIC
    src/switcher/application_info.cpp src/switcher/application_info.h
    src/switcher/switcher_view.cpp src/switcher/switcher_view.h)
target_include_directories(cutefish-switcher PUBLIC src)
target_link_libraries(cutefish-switcher PUBLIC cutefish-wm cutefish-decoration cutefish-scene
    cutefish-toolkit-host)

# The desktop shell inside the compositor: its internal windows, their input
# and rendering, and the components built on them.
add_library(cutefish-shell STATIC
    src/shell/shell.cpp src/shell/shell.h
    src/shell/shell_data.cpp src/shell/shell_data.h
    src/shell/shell_drag.cpp src/shell/shell_drag.h
    src/shell/shell_host.cpp src/shell/shell_host.h
    src/shell/shell_input.cpp src/shell/shell_input.h
    src/shell/shell_text_input.cpp src/shell/shell_text_input.h
    src/shell/shell_window.cpp src/shell/shell_window.h
    src/shell/window_renderer.cpp src/shell/window_renderer.h)
target_include_directories(cutefish-shell PUBLIC src)
target_link_libraries(cutefish-shell PUBLIC cutefish-wm cutefish-input cutefish-display
    cutefish-decoration cutefish-toolkit-host cutefish-shell-common PRIVATE cutefish-wayland-shell)
target_compile_definitions(cutefish-shell PRIVATE
    CUTEFISH_SHELL_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/cutefish-compositor/shell")
install(DIRECTORY data/shell/ DESTINATION ${CMAKE_INSTALL_DATADIR}/cutefish-compositor/shell)

# The synthetic display: outputs and a presentation clock with no hardware.
add_library(cutefish-headless STATIC
    src/backend/headless/headless_backend.cpp
    src/backend/headless/headless_backend.h
)
target_include_directories(cutefish-headless PUBLIC src)
target_link_libraries(cutefish-headless PUBLIC cutefish-compositor-core)
set_target_properties(cutefish-headless PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Global registration. Sits above every protocol library, which is why it cannot
# live in cutefish-wayland the way Exo keeps it in its Server.
add_library(cutefish-wayland-globals STATIC
    src/wayland/protocol_globals.cpp
    src/wayland/protocol_globals.h
    src/wayland/protocols/cutefish_computer_control.cpp
    src/wayland/protocols/cutefish_computer_control.h
)
target_include_directories(cutefish-wayland-globals PUBLIC src)
target_link_libraries(cutefish-wayland-globals
    PUBLIC cutefish-wayland cutefish-compositor-core cutefish-wayland-input
           cutefish-wayland-shell cutefish-wayland-private-shell cutefish-wm
           cutefish-input cutefish-input-method-settings cutefish-computer-control
)
set_target_properties(cutefish-wayland-globals PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(cutefish-compositor
    src/main.cpp
    src/core/compositor_options.cpp
    src/core/compositor_options.h
    src/core/computer_control_environment.cpp
    src/core/computer_control_environment.h
    src/core/compositor_shell.cpp
    src/core/compositor_shell.h
)
target_link_libraries(cutefish-compositor PRIVATE cutefish-display-runtime
    cutefish-auth cutefish-switcher cutefish-shell cutefish-wayland-globals cutefish-headless)
# The portal backend streams outputs to PipeWire through the capture globals and
# is allowlisted by its installed path; cutefish-core's screen recorder is started
# by the shell on a capture connection of its own.
target_compile_definitions(cutefish-compositor PRIVATE
    CUTEFISH_PORTAL_BACKEND="${CMAKE_INSTALL_FULL_LIBEXECDIR}/xdg-desktop-portal-cutefish"
    CUTEFISH_SCREEN_RECORDER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/cutefish-screenrecorder")
target_link_libraries(cutefish-compositor
    PRIVATE cutefish-wayland cutefish-compositor-core cutefish-backend cutefish-scene
            cutefish-input cutefish-wayland-input cutefish-wm
            cutefish-wayland-shell cutefish-decoration cutefish-wayland-dmabuf
            cutefish-wayland-private-shell cutefish-display-ipc cutefish-input-method-settings
            cutefish-keyboard-settings cutefish-accelerators)

pkg_check_modules(WaylandCaptureClient REQUIRED IMPORTED_TARGET wayland-client)
add_executable(cutefish-screenshot src/tools/screenshot.cpp)
target_link_libraries(cutefish-screenshot PRIVATE PkgConfig::WaylandCaptureClient Cutefish::ToolkitBase)

add_executable(cutefish-displayctl src/tools/displayctl.cpp)
target_link_libraries(cutefish-displayctl PRIVATE cutefish-display-ipc Cutefish::ToolkitBase)

option(CUTEFISH_ENABLE_XWAYLAND "Build the native Xwayland frontend" ON)
if(CUTEFISH_ENABLE_XWAYLAND)
    find_package(Threads REQUIRED)
    pkg_check_modules(Xcb REQUIRED IMPORTED_TARGET xcb xcb-composite xcb-xfixes xcb-shape xcb-sync)
    cutefish_wayland_protocol(xwayland-shell-v1
        ${WAYLAND_PROTOCOLS_DATADIR}/staging/xwayland-shell/xwayland-shell-v1.xml)
    cutefish_wayland_protocol(xwayland-keyboard-grab-unstable-v1
        ${WAYLAND_PROTOCOLS_DATADIR}/unstable/xwayland-keyboard-grab/xwayland-keyboard-grab-unstable-v1.xml)
    add_library(cutefish-xwayland STATIC
        src/xwayland/surface_association.cpp
        src/xwayland/xwayland_shell.cpp
        src/xwayland/xwayland_keyboard_grab.cpp
        src/xwayland/xwayland_server.cpp
        src/xwayland/xwm.cpp
        src/xwayland/selection_bridge.cpp
        src/xwayland/dnd_bridge.cpp
        ${xwayland-shell-v1_SOURCES}
        ${xwayland-keyboard-grab-unstable-v1_SOURCES})
    target_include_directories(cutefish-xwayland PUBLIC src ${CUTEFISH_PROTOCOL_DIR})
    target_link_libraries(cutefish-xwayland PUBLIC cutefish-wayland-private-shell PkgConfig::Xcb Threads::Threads
        Cutefish::ToolkitBase)
    target_link_libraries(cutefish-compositor PRIVATE cutefish-xwayland)
    target_compile_definitions(cutefish-compositor PRIVATE CUTEFISH_ENABLE_XWAYLAND)
    install(FILES ${WAYLAND_PROTOCOLS_DATADIR}/staging/xwayland-shell/xwayland-shell-v1.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/cutefish-compositor)
endif()

install(TARGETS cutefish-compositor cutefish-displayctl cutefish-screenshot RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    ${CUTEFISH_PRIVATE_PROTOCOL_DIR}/input-method-unstable-v2.xml
    ${CUTEFISH_PRIVATE_PROTOCOL_DIR}/virtual-keyboard-unstable-v1.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/cutefish-compositor)
install(FILES LICENSE LICENSE.chromium LICENSE.sommelier DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/cutefish-compositor)
install(FILES ${DISPLAY_CONFIG_XML} DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/interfaces)
cutefish_add_translations(cutefish-compositor SOURCES src/shell src/auth src/wm/overview
    DESKTOP_FILES data/cutefish-screenshot.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cutefish-screenshot.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

target_link_libraries(cutefish-wayland-input PUBLIC cutefish-protocols)

add_library(cutefish-cursor STATIC src/cursor/theme.cpp src/cursor/cursor.cpp
    src/cursor/output_cursor_router.cpp src/cursor/output_cursor_router.h)
target_include_directories(cutefish-cursor PUBLIC src)
target_sources(cutefish-cursor PRIVATE src/cursor/preferences.cpp src/cursor/preferences.h)
target_link_libraries(cutefish-cursor PUBLIC cutefish-input cutefish-display Cutefish::Toolkit)
target_link_libraries(cutefish-compositor PRIVATE cutefish-cursor)

# The shell's backends: models and D-Bus services without views.
include(src/shell/backends.cmake)
# The shell's components: their views on the backends above.
include(src/shell/components.cmake)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
