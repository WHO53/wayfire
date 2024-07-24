#pragma once

#include "ipc-rules-common.hpp"
#include <set>
#include "plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/seat.hpp"
#include <wayfire/per-output-plugin.hpp>

namespace wf
{
class ipc_rules_events_methods_t : public wf::per_output_tracker_mixin_t<>
{
  public:
    void init_events(ipc::method_repository_t *method_repository)
    {
        method_repository->register_method("window-rules/events/watch", on_client_watch);
        method_repository->connect(&on_client_disconnected);
        init_output_tracking();
    }

    void fini_events(ipc::method_repository_t *method_repository)
    {
        method_repository->unregister_method("window-rules/events/watch");
        fini_output_tracking();
    }

    void handle_new_output(wf::output_t *output) override
    {
        for (auto& [_, event] : signal_map)
        {
            if (event.connected_count)
            {
                event.register_output(output);
            }
        }

        Json::Value data;
        data["event"]  = "output-added";
        data["output"] = output_to_json(output);
        send_event_to_subscribes(data, data["event"].asString());
    }

    void handle_output_removed(wf::output_t *output) override
    {
        Json::Value data;
        data["event"]  = "output-removed";
        data["output"] = output_to_json(output);
        send_event_to_subscribes(data, data["event"].asString());
    }

    // Template FOO for efficient management of signals: ensure that only actually listened-for signals
    // are connected.
    struct signal_registration_handler
    {
        std::function<void()> register_core = [] () {};
        std::function<void(wf::output_t*)> register_output = [] (wf::output_t*) {};
        std::function<void()> unregister = [] () {};
        int connected_count = 0;

        void increase_count()
        {
            connected_count++;
            if (connected_count > 1)
            {
                return;
            }

            register_core();
            for (auto& wo : wf::get_core().output_layout->get_outputs())
            {
                register_output(wo);
            }
        }

        void decrease_count()
        {
            connected_count--;
            if (connected_count > 0)
            {
                return;
            }

            unregister();
        }
    };

    template<class Signal>
    static signal_registration_handler get_generic_core_registration_cb(
        wf::signal::connection_t<Signal> *conn)
    {
        return {
            .register_core = [=] () { wf::get_core().connect(conn); },
            .unregister    = [=] () { conn->disconnect(); }
        };
    }

    template<class Signal>
    signal_registration_handler get_generic_output_registration_cb(wf::signal::connection_t<Signal> *conn)
    {
        return {
            .register_output = [=] (wf::output_t *wo) { wo->connect(conn); },
            .unregister = [=] () { conn->disconnect(); }
        };
    }

    std::map<std::string, signal_registration_handler> signal_map =
    {
        {"view-mapped", get_generic_core_registration_cb(&on_view_mapped)},
        {"view-unmapped", get_generic_core_registration_cb(&on_view_unmapped)},
        {"view-set-output", get_generic_core_registration_cb(&on_view_set_output)},
        {"view-geometry-changed", get_generic_core_registration_cb(&on_view_geometry_changed)},
        {"view-wset-changed", get_generic_core_registration_cb(&on_view_moved_to_wset)},
        {"view-focused", get_generic_core_registration_cb(&on_kbfocus_changed)},
        {"view-title-changed", get_generic_core_registration_cb(&on_title_changed)},
        {"view-app-id-changed", get_generic_core_registration_cb(&on_app_id_changed)},
        {"plugin-activation-state-changed", get_generic_core_registration_cb(&on_plugin_activation_changed)},
        {"output-gain-focus", get_generic_core_registration_cb(&on_output_gain_focus)},

        {"view-tiled", get_generic_output_registration_cb(&_tiled)},
        {"view-minimized", get_generic_output_registration_cb(&_minimized)},
        {"view-fullscreened", get_generic_output_registration_cb(&_fullscreened)},
        {"view-sticky", get_generic_output_registration_cb(&_stickied)},
        {"view-workspace-changed", get_generic_output_registration_cb(&_view_workspace)},
        {"output-wset-changed", get_generic_output_registration_cb(&on_wset_changed)},
        {"wset-workspace-changed", get_generic_output_registration_cb(&on_wset_workspace_changed)},
    };

    // Track a list of clients which have requested watch
    std::map<wf::ipc::client_interface_t*, std::set<std::string>> clients;

    wf::ipc::method_callback_full on_client_watch =
        [=] (Json::Value data, wf::ipc::client_interface_t *client)
    {
        static constexpr const char *EVENTS = "events";
        if (data.isMember(EVENTS) && !data[EVENTS].isArray())
        {
            return wf::ipc::json_error("Event list is not an array!");
        }

        std::set<std::string> subscribed_to;
        if (data.isMember(EVENTS))
        {
            for (auto& sub : data[EVENTS])
            {
                if (!sub.isString())
                {
                    return wf::ipc::json_error("Event list contains non-string entries!");
                }

                if (signal_map.count(sub.asString()))
                {
                    subscribed_to.insert(sub.asString());
                }
            }
        } else
        {
            for (auto& [ev_name, _] : signal_map)
            {
                subscribed_to.insert(ev_name);
            }
        }

        for (auto& ev_name : subscribed_to)
        {
            signal_map[ev_name].increase_count();
        }

        clients[client] = subscribed_to;
        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::ipc::client_disconnected_signal> on_client_disconnected =
        [=] (wf::ipc::client_disconnected_signal *ev)
    {
        for (auto& ev_name : clients[ev->client])
        {
            signal_map[ev_name].decrease_count();
        }

        clients.erase(ev->client);
    };

    void send_view_to_subscribes(wayfire_view view, std::string event_name)
    {
        Json::Value event;
        event["event"] = event_name;
        event["view"]  = view_to_json(view);
        send_event_to_subscribes(event, event_name);
    }

    void send_event_to_subscribes(const Json::Value& data, const std::string& event_name)
    {
        for (auto& [client, events] : clients)
        {
            if (events.empty() || events.count(event_name))
            {
                client->send_json(data);
            }
        }
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-mapped");
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-unmapped");
    };

    wf::signal::connection_t<wf::view_set_output_signal> on_view_set_output =
        [=] (wf::view_set_output_signal *ev)
    {
        Json::Value data;
        data["event"]  = "view-set-output";
        data["output"] = output_to_json(ev->output);
        data["view"]   = view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"].asString());
    };

    wf::signal::connection_t<wf::view_geometry_changed_signal> on_view_geometry_changed =
        [=] (wf::view_geometry_changed_signal *ev)
    {
        Json::Value data;
        data["event"] = "view-geometry-changed";
        data["old-geometry"] = wf::ipc::geometry_to_json(ev->old_geometry);
        data["view"] = view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"].asString());
    };

    wf::signal::connection_t<wf::view_moved_to_wset_signal> on_view_moved_to_wset =
        [=] (wf::view_moved_to_wset_signal *ev)
    {
        Json::Value data;
        data["event"]    = "view-wset-changed";
        data["old-wset"] = wset_to_json(ev->old_wset.get());
        data["new-wset"] = wset_to_json(ev->new_wset.get());
        data["view"]     = view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"].asString());
    };

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_kbfocus_changed =
        [=] (wf::keyboard_focus_changed_signal *ev)
    {
        send_view_to_subscribes(wf::node_to_view(ev->new_focus), "view-focused");
    };

    // Tiled rule handler.
    wf::signal::connection_t<wf::view_tiled_signal> _tiled = [=] (wf::view_tiled_signal *ev)
    {
        Json::Value data;
        data["event"]     = "view-tiled";
        data["old-edges"] = ev->old_edges;
        data["new-edges"] = ev->new_edges;
        data["view"] = view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"].asString());
    };

    // Minimized rule handler.
    wf::signal::connection_t<wf::view_minimized_signal> _minimized = [=] (wf::view_minimized_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-minimized");
    };

    // Fullscreened rule handler.
    wf::signal::connection_t<wf::view_fullscreen_signal> _fullscreened = [=] (wf::view_fullscreen_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-fullscreen");
    };

    // Stickied rule handler.
    wf::signal::connection_t<wf::view_set_sticky_signal> _stickied = [=] (wf::view_set_sticky_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-sticky");
    };

    wf::signal::connection_t<wf::view_change_workspace_signal> _view_workspace =
        [=] (wf::view_change_workspace_signal *ev)
    {
        Json::Value data;
        data["event"] = "view-workspace-changed";
        data["from"]  = wf::ipc::point_to_json(ev->from);
        data["to"]    = wf::ipc::point_to_json(ev->to);
        data["view"]  = view_to_json(ev->view);
        send_event_to_subscribes(data, data["event"].asString());
    };

    wf::signal::connection_t<wf::view_title_changed_signal> on_title_changed =
        [=] (wf::view_title_changed_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-title-changed");
    };

    wf::signal::connection_t<wf::view_app_id_changed_signal> on_app_id_changed =
        [=] (wf::view_app_id_changed_signal *ev)
    {
        send_view_to_subscribes(ev->view, "view-app-id-changed");
    };

    wf::signal::connection_t<wf::output_plugin_activated_changed_signal> on_plugin_activation_changed =
        [=] (wf::output_plugin_activated_changed_signal *ev)
    {
        Json::Value data;
        data["event"]  = "plugin-activation-state-changed";
        data["plugin"] = ev->plugin_name;
        data["state"]  = ev->activated;
        data["output"] = ev->output ? (int)ev->output->get_id() : -1;
        data["output-data"] = output_to_json(ev->output);
        send_event_to_subscribes(data, data["event"].asString());
    };

    wf::signal::connection_t<wf::output_gain_focus_signal> on_output_gain_focus =
        [=] (wf::output_gain_focus_signal *ev)
    {
        Json::Value data;
        data["event"]  = "output-gain-focus";
        data["output"] = output_to_json(ev->output);
        send_event_to_subscribes(data, data["event"].asString());
    };

    wf::signal::connection_t<wf::workspace_set_changed_signal> on_wset_changed =
        [=] (wf::workspace_set_changed_signal *ev)
    {
        Json::Value data;
        data["event"]    = "output-wset-changed";
        data["new-wset"] = ev->new_wset ? (int)ev->new_wset->get_id() : -1;
        data["output"]   = ev->output ? (int)ev->output->get_id() : -1;
        data["new-wset-data"] = wset_to_json(ev->new_wset.get());
        data["output-data"]   = output_to_json(ev->output);
        send_event_to_subscribes(data, data["event"].asString());
    };

    wf::signal::connection_t<wf::workspace_changed_signal> on_wset_workspace_changed =
        [=] (wf::workspace_changed_signal *ev)
    {
        Json::Value data;
        data["event"] = "wset-workspace-changed";
        data["previous-workspace"] = wf::ipc::point_to_json(ev->old_viewport);
        data["new-workspace"] = wf::ipc::point_to_json(ev->new_viewport);
        data["output"] = ev->output ? (int)ev->output->get_id() : -1;
        data["wset"]   = (ev->output && ev->output->wset()) ? (int)ev->output->wset()->get_id() : -1;
        data["output-data"] = output_to_json(ev->output);
        data["wset-data"]   = ev->output ? wset_to_json(ev->output->wset().get()) : Json::nullValue;
        send_event_to_subscribes(data, data["event"].asString());
    };
};
}
