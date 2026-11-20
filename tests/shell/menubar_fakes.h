// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_TESTS_SHELL_MENUBAR_FAKES_H_
#define CUTEFISH_TESTS_SHELL_MENUBAR_FAKES_H_

#include <algorithm>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "base/dbus.h"

namespace cutefish::test {

namespace dbus = base::dbus;

using Pixmap = std::tuple<int32_t, int32_t, std::vector<uint8_t>>;
using Layout = std::tuple<int32_t, dbus::VariantMap, std::vector<dbus::Variant>>;

inline dbus::Variant layout(int32_t id, dbus::VariantMap properties, std::vector<dbus::Variant> children = {})
{
    return dbus::Variant(Layout(id, std::move(properties), std::move(children)));
}

// The layout is what `layouts` holds for each parent.
class FakeMenuExporter
{
public:
    FakeMenuExporter(dbus::Connection *bus, const std::string &path)
        : object(bus, path, "com.canonical.dbusmenu")
    {
        object.addMethod("GetLayout", "iias", "u(ia{sv}av)", [this](dbus::MethodCall &call) {
            int32_t parent = 0;
            int32_t depth = 0;
            std::vector<std::string> names;
            call.read(parent, depth, names);
            calls.push_back("GetLayout " + std::to_string(parent) + " " + std::to_string(depth));
            const auto it = layouts.find(parent);
            if (it == layouts.end()) {
                call.replyError("com.canonical.dbusmenu.Error", "No such item");
                return;
            }
            call.replyWith({dbus::Variant(revision), it->second});
        });
        object.addMethod("AboutToShow", [this](int32_t id) {
            calls.push_back("AboutToShow " + std::to_string(id));
            return needUpdate;
        });
        object.addMethod("Event", "isvu", "", [this](dbus::MethodCall &call) {
            int32_t id = 0;
            std::string event;
            call.read(id, event);
            calls.push_back("Event " + std::to_string(id) + " " + event);
            call.reply();
        });
        object.addSignal("LayoutUpdated", "ui");
        object.addSignal("ItemsPropertiesUpdated", "a(ia{sv})a(ias)");
        object.addSignal("ItemActivationRequested", "iu");
        EXPECT_TRUE(object.publish());
    }

    bool called(const std::string &what) const
    {
        return std::find(calls.begin(), calls.end(), what) != calls.end();
    }
    int count(const std::string &what) const { return int(std::count(calls.begin(), calls.end(), what)); }

    dbus::Object object;
    std::map<int32_t, dbus::Variant> layouts;
    std::vector<std::string> calls;
    uint32_t revision = 1;
    bool needUpdate = false;
};

inline dbus::Variant menuWithFileAndEdit()
{
    return layout(0, {},
                  {layout(1, {{"label", dbus::Variant(std::string("_File"))},
                              {"children-display", dbus::Variant(std::string("submenu"))}}),
                   layout(2, {{"label", dbus::Variant(std::string("_Edit"))},
                              {"children-display", dbus::Variant(std::string("submenu"))}})});
}

using GtkMenuContents = std::tuple<uint32_t, uint32_t, std::vector<dbus::VariantMap>>;
using GtkMenuChange = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, std::vector<dbus::VariantMap>>;
using GtkActionDescription = std::tuple<bool, dbus::Signature, std::vector<dbus::Variant>>;

inline dbus::Variant gtkLink(uint32_t group, uint32_t menu)
{
    return dbus::Variant(std::tuple<uint32_t, uint32_t>(group, menu));
}

// Start answers with the menus `menus` holds for the groups asked for.
class FakeGtkMenus
{
public:
    FakeGtkMenus(dbus::Connection *bus, const std::string &path)
        : object(bus, path, "org.gtk.Menus")
    {
        object.addMethod("Start", "au", "a(uuaa{sv})", [this](dbus::MethodCall &call) {
            std::vector<uint32_t> groups;
            call.read(groups);
            std::vector<GtkMenuContents> reply;
            for (uint32_t group : groups) {
                calls.push_back("Start " + std::to_string(group));
                for (const GtkMenuContents &menu : menus) {
                    if (std::get<0>(menu) == group)
                        reply.push_back(menu);
                }
            }
            call.replyWith({dbus::Variant(reply)});
        });
        object.addMethod("End", "au", "", [this](dbus::MethodCall &call) {
            std::vector<uint32_t> groups;
            call.read(groups);
            for (uint32_t group : groups)
                calls.push_back("End " + std::to_string(group));
            call.reply();
        });
        object.addSignal("Changed", "a(uuuuaa{sv})");
        EXPECT_TRUE(object.publish());
    }

    bool called(const std::string &what) const
    {
        return std::find(calls.begin(), calls.end(), what) != calls.end();
    }

    dbus::Object object;
    std::vector<GtkMenuContents> menus;
    std::vector<std::string> calls;
};

// Records "Activate name", "Activate name 'target'" and "SetState name true".
class FakeGtkActions
{
public:
    FakeGtkActions(dbus::Connection *bus, const std::string &path)
        : object(bus, path, "org.gtk.Actions")
    {
        object.addMethod("DescribeAll", "", "a{s(bgav)}", [this](dbus::MethodCall &call) {
            call.replyWith({dbus::Variant(actions)});
        });
        object.addMethod("Activate", "sava{sv}", "", [this](dbus::MethodCall &call) {
            std::string name;
            std::vector<dbus::Variant> parameter;
            call.read(name, parameter);
            std::string text = "Activate " + name;
            if (!parameter.empty())
                text += " '" + parameter.front().value<std::string>() + "'";
            calls.push_back(text);
            call.reply();
        });
        object.addMethod("SetState", "sva{sv}", "", [this](dbus::MethodCall &call) {
            std::string name;
            dbus::Variant state;
            call.read(name, state);
            calls.push_back("SetState " + name + (state.value<bool>() ? " true" : " false"));
            call.reply();
        });
        object.addSignal("Changed", "asa{sb}a{sv}a{s(bgav)}");
        EXPECT_TRUE(object.publish());
    }

    bool called(const std::string &what) const
    {
        return std::find(calls.begin(), calls.end(), what) != calls.end();
    }

    dbus::Object object;
    std::map<std::string, GtkActionDescription> actions;
    std::vector<std::string> calls;
};

class FakeTrayItem
{
public:
    explicit FakeTrayItem(dbus::Connection *bus)
        : object(bus, "/StatusNotifierItem", "org.kde.StatusNotifierItem")
    {
        object.addProperty("Id", [this] { return id; });
        object.addProperty("Title", [this] { return title; });
        object.addProperty("IconName", [this] { return iconName; });
        object.addProperty("IconThemePath", [this] { return iconThemePath; });
        object.addProperty("IconPixmap", [this] { return pixmaps; });
        object.addProperty("ToolTip", [this] {
            return std::tuple<std::string, std::vector<Pixmap>, std::string, std::string>("", {}, toolTip, "sub");
        });
        object.addProperty("Menu", [this] { return dbus::ObjectPath{menuPath}; });
        object.addProperty("ItemIsMenu", [this] { return itemIsMenu; });
        object.addMethod("Activate", [this](int32_t x, int32_t y) {
            calls.push_back("Activate " + std::to_string(x) + "," + std::to_string(y));
        });
        object.addMethod("SecondaryActivate", [this](int32_t, int32_t) { calls.push_back("SecondaryActivate"); });
        object.addMethod("ContextMenu", [this](int32_t, int32_t) { calls.push_back("ContextMenu"); });
        object.addMethod("Scroll", [this](int32_t delta, const std::string &orientation) {
            calls.push_back("Scroll " + std::to_string(delta) + " " + orientation);
        });
        object.addSignal("NewTitle", "");
        object.addSignal("NewIcon", "");
        EXPECT_TRUE(object.publish());
    }

    dbus::Object object;
    std::string id = "example";
    std::string title = "Example";
    std::string iconName;
    std::string iconThemePath = "/nonexistent/icons/";
    std::string toolTip = "Tip";
    std::vector<Pixmap> pixmaps;
    std::string menuPath = "/NO_DBUSMENU";
    bool itemIsMenu = false;
    std::vector<std::string> calls;
};


} // namespace cutefish::test

#endif // CUTEFISH_TESTS_SHELL_MENUBAR_FAKES_H_
