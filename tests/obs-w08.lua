-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
-- The dedicated test collection reads numbered commands, never executable text.
local obs = obslua
local directory, previous = "", ""
local scene_source, source = nil, nil
local function poll()
    if not scene_source then
        scene_source = obs.obs_frontend_get_current_scene()
        if not scene_source then return end
        local scene = obs.obs_scene_from_source(scene_source)
        local items = obs.obs_scene_enum_items(scene)
        if items then
            for _, item in ipairs(items) do obs.obs_sceneitem_remove(item) end
            obs.sceneitem_list_release(items)
        end
        for _, entry in ipairs({{"W08 checker", "checker.png"}, {"W08 calibration", "calibration.png"}}) do
            local data = obs.obs_data_create()
            obs.obs_data_set_string(data, "file", directory .. "/" .. entry[2])
            obs.obs_data_set_bool(data, "linear_alpha", true)
            local image = obs.obs_source_create("image_source", entry[1], data, nil)
            obs.obs_data_release(data)
            obs.obs_scene_add(scene, image)
            if entry[2] == "calibration.png" then source = image else obs.obs_source_release(image) end
        end
        local data = obs.obs_data_create()
        obs.obs_data_set_bool(data, "w03_fake_enabled", true)
        obs.obs_data_set_double(data, "alpha_contrast", 1.0)
        local filter = obs.obs_source_create("corridorkey", "W08 reconstruction", data, nil)
        obs.obs_source_filter_add(source, filter)
        obs.obs_source_release(filter); obs.obs_data_release(data)
    end
    local file = io.open(directory .. "/command.txt", "r")
    if not file then return end
    local line = file:read("*l"); file:close()
    if line == previous then return end
    previous = line
    local action = line and line:match("^%d+ (%w+)$")
    if not action then return end
    obs.script_log(obs.LOG_INFO, "W08 action: " .. action)
    if action == "screenshot" then obs.obs_frontend_take_source_screenshot(scene_source); return end
    if action == "source" then obs.obs_frontend_take_source_screenshot(source); return end
    local filter = obs.obs_source_get_filter_by_name(source, "W08 reconstruction")
    if not filter then return end
    local data = obs.obs_source_get_settings(filter)
    local view = action:match("^view(%d)$")
    if view then obs.obs_data_set_int(data, "w08_debug_view", tonumber(view)) end
    if action == "crop" then
        obs.obs_data_set_double(data, "cut_left", 17); obs.obs_data_set_double(data, "cut_right", 13)
        obs.obs_data_set_double(data, "cut_top", 11); obs.obs_data_set_double(data, "cut_bottom", 9)
    end
    if action == "full" then
        for _, key in ipairs({"cut_left", "cut_right", "cut_top", "cut_bottom"}) do obs.obs_data_set_double(data, key, 0) end
    end
    if action == "disable" then obs.obs_source_set_enabled(filter, false) end
    if action == "enable" then obs.obs_source_set_enabled(filter, true) end
    obs.obs_source_update(filter, data); obs.obs_data_release(data); obs.obs_source_release(filter)
end
function script_description() return "Reconstruction pixel and debug views in CorridorKey Test." end
function script_load(settings)
    directory = obs.obs_data_get_string(settings, "directory")
    if obs.obs_frontend_get_current_scene_collection() == "CorridorKey Test" then obs.timer_add(poll, 500) end
end
function script_unload()
    obs.timer_remove(poll)
    if source then obs.obs_source_release(source) end
    if scene_source then obs.obs_source_release(scene_source) end
end
