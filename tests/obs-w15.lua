-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
-- Dedicated CorridorKey Test collection only. Commands are data, never evaluated.
local obs = obslua
local directory, previous = "", ""
local function poll()
    local file = io.open(directory .. "/command.txt", "r")
    if not file then return end
    local line = file:read("*l"); file:close()
    if not line or line == previous then return end
    previous = line
    local action, value = line:match("^%d+ (%w+) ?(.*)$")
    local video = obs.obs_get_source_by_name("W15 Test Card")
    if not video then return end
    local audio = obs.obs_get_source_by_name("W15 Dummy Audio") or obs.obs_get_source_by_name("W15 Renamed Audio")
    local filter = obs.obs_source_get_filter_by_name(video, "CorridorKey")
    if filter and action == "delay" then
        local settings = obs.obs_source_get_settings(filter)
        obs.obs_data_set_int(settings, "w03_fake_delay", tonumber(value))
        obs.obs_source_update(filter, settings)
        obs.obs_data_release(settings)
    elseif filter and action == "fault" then
        local settings = obs.obs_source_get_settings(filter)
        obs.obs_data_set_int(settings, "w03_fake_fault", tonumber(value))
        obs.obs_source_update(filter, settings)
        obs.obs_data_release(settings)
    elseif filter and action == "auto" then
        local settings = obs.obs_source_get_settings(filter)
        obs.obs_data_set_bool(settings, "auto_sync", value == "on")
        obs.obs_source_update(filter, settings)
        obs.obs_data_release(settings)
    elseif filter and action == "enable" then
        obs.obs_source_set_enabled(filter, value == "on")
    elseif filter and action == "remove" then
        obs.obs_source_filter_remove(video, filter)
    elseif audio and action == "manual" then
        obs.obs_source_set_sync_offset(audio, tonumber(value) * 1000000)
    elseif audio and action == "rename" then
        obs.obs_source_set_name(audio, "W15 Renamed Audio")
    elseif audio and action == "deleteaudio" then
        obs.obs_source_remove(audio)
    elseif filter and audio and action == "shared" then
        local settings = obs.obs_source_get_settings(filter)
        local second = obs.obs_source_create("corridorkey", "W15 Second Controller", settings, nil)
        obs.obs_data_release(settings)
        obs.obs_source_filter_add(video, second)
        obs.obs_source_release(second)
    elseif filter and action == "properties" then
        obs.obs_frontend_open_source_filters(video)
    end
    local report = "action=" .. tostring(action)
    if filter then
        local settings = obs.obs_source_get_settings(filter)
        local properties = obs.obs_source_properties(filter)
        obs.obs_properties_apply_settings(properties, settings)
        report = report .. " delay=" .. obs.obs_data_get_int(settings, "w03_fake_delay") ..
            " status=" .. obs.obs_data_get_string(settings, "status") ..
            " applied=" .. obs.obs_data_get_string(settings, "applied_delay")
        obs.obs_properties_destroy(properties)
        obs.obs_data_release(settings)
        obs.obs_source_release(filter)
    end
    if audio then
        report = report .. " mic_ns=" .. obs.obs_source_get_sync_offset(audio)
        obs.obs_source_release(audio)
    end
    obs.script_log(obs.LOG_INFO, "W15 " .. report)
    obs.obs_source_release(video)
end
function script_description() return "W15 sync validation; dedicated test collection and dummy audio only." end
function script_load(settings)
    directory = obs.obs_data_get_string(settings, "directory")
    if obs.obs_frontend_get_current_scene_collection() == "CorridorKey Test" then obs.timer_add(poll, 500) end
end
function script_unload() obs.timer_remove(poll) end
