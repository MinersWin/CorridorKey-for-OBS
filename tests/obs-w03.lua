-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
-- Load only in the dedicated CorridorKey Test collection. Commands are read
-- from <directory>/command.txt as "unique-number action"; no code is evaluated.
local obs = obslua
local directory, footage, previous = "", "", ""
local source, scene, scene_source = nil, nil, nil
local serial = 0

local function clear()
    if scene then
        local items = obs.obs_scene_enum_items(scene)
        if items then
            for _, item in ipairs(items) do obs.obs_sceneitem_remove(item) end
            obs.sceneitem_list_release(items)
        end
    end
    if source then obs.obs_source_release(source); source = nil end
end
local function fake()
    local settings = obs.obs_data_create()
    obs.obs_data_set_bool(settings, "w03_fake_enabled", true)
    local filter = obs.obs_source_create("corridorkey", "CorridorKey W03", settings, nil)
    obs.obs_data_release(settings)
    if filter then obs.obs_source_filter_add(source, filter); obs.obs_source_release(filter) end
end
local function create(kind, width, height)
    clear()
    serial = serial + 1
    local background_settings = obs.obs_data_create()
    obs.obs_data_set_int(background_settings, "color", 0xff383028)
    obs.obs_data_set_int(background_settings, "width", 3840)
    obs.obs_data_set_int(background_settings, "height", 2160)
    local background = obs.obs_source_create("color_source", "W03 Background", background_settings, nil)
    obs.obs_data_release(background_settings)
    obs.obs_scene_add(scene, background)
    obs.obs_source_release(background)
    local settings = obs.obs_data_create()
    local id = kind
    obs.obs_data_set_int(settings, "width", width)
    obs.obs_data_set_int(settings, "height", height)
    if kind == "color" then
        id = "color_source"
        obs.obs_data_set_int(settings, "color", 0xff8080ff)
    elseif kind == "image" then
        id = "image_source"
        obs.obs_data_set_string(settings, "file", directory .. "/input-" .. width .. ".png")
    elseif kind == "media" then
        id = "ffmpeg_source"
        obs.obs_data_set_bool(settings, "is_local_file", true)
        obs.obs_data_set_bool(settings, "looping", true)
        obs.obs_data_set_string(settings, "local_file", footage .. "/" .. (width == 3840 and
            "1871395_Green_Screen_Dancing_3840x2160.mp4" or
            "mixkit-female-reporter-reporting-with-microphone-in-hand-on-a-chroma-28293-full-hd.mp4"))
    elseif kind == "browser" then
        id = "browser_source"
        obs.obs_data_set_bool(settings, "is_local_file", true)
        obs.obs_data_set_string(settings, "local_file", directory .. "/browser.html")
        obs.obs_data_set_int(settings, "fps", 60)
        obs.obs_data_set_bool(settings, "fps_custom", true)
    end
    source = obs.obs_source_create(id, "W03 " .. kind .. " " .. width .. " " .. serial, settings, nil)
    obs.obs_data_release(settings)
    if source then
        fake()
        local item = obs.obs_scene_add(scene, source)
        if width == 1920 then
            local scale = obs.vec2(); scale.x = 2; scale.y = 2
            obs.obs_sceneitem_set_scale(item, scale)
        end
    end
end
local function command(action)
    obs.script_log(obs.LOG_INFO, "W03 action: " .. action)
    local kind, size = action:match("^(%a+)(%d+)$")
    if kind == "color" or kind == "image" or kind == "browser" or kind == "media" then
        local width = tonumber(size)
        create(kind, width, width == 3840 and 2160 or 1080)
        return
    end
    if not source then return end
    local filter = obs.obs_source_get_filter_by_name(source, "CorridorKey W03")
    if action == "duplicate" then
        obs.obs_scene_add(scene, source)
        if obs.obs_frontend_open_projector then
            obs.obs_frontend_open_projector("Source", -1, "", obs.obs_source_get_name(source))
            obs.obs_frontend_open_projector("Multiview", -1, "", "")
        end
    elseif action == "switch" and filter then
        obs.obs_source_filter_remove(source, filter)
        clear()
        local settings = obs.obs_data_create()
        obs.obs_data_set_int(settings, "width", 1920)
        obs.obs_data_set_int(settings, "height", 1080)
        obs.obs_data_set_int(settings, "color", 0xff0080ff)
        source = obs.obs_source_create("color_source", "W03 switched target", settings, nil)
        obs.obs_data_release(settings)
        obs.obs_source_filter_add(source, filter)
        obs.obs_scene_add(scene, source)
    elseif action == "status" and filter then
        local properties = obs.obs_source_properties(filter)
        local status = obs.obs_properties_get(properties, "status")
        obs.script_log(obs.LOG_INFO, "W03 status: " .. (status and obs.obs_property_description(status) or "missing") ..
            "; native=" .. obs.obs_source_get_base_width(source) .. "x" .. obs.obs_source_get_base_height(source))
        obs.obs_properties_destroy(properties)
    elseif action == "screenshot" and obs.obs_frontend_take_source_screenshot then
        obs.obs_frontend_take_source_screenshot(obs.obs_scene_get_source(scene))
    elseif action == "disable" and filter then
        obs.obs_source_set_enabled(filter, false)
    elseif action == "enable" and filter then
        obs.obs_source_set_enabled(filter, true)
    elseif action == "remove" and filter then
        obs.obs_source_filter_remove(source, filter)
    elseif action == "readd" and not filter then
        fake()
    elseif action == "clear" then
        clear()
    elseif (action == "zero" or action == "resize4k" or action == "resize1080") then
        local settings = obs.obs_data_create()
        local width = action == "zero" and 0 or (action == "resize4k" and 3840 or 1920)
        obs.obs_data_set_int(settings, "width", width)
        obs.obs_data_set_int(settings, "height", width == 0 and 0 or (width == 3840 and 2160 or 1080))
        obs.obs_source_update(source, settings)
        obs.obs_data_release(settings)
    elseif filter then
        local settings = obs.obs_source_get_settings(filter)
        if action == "slow" then obs.obs_data_set_int(settings, "w03_fake_delay", 400)
        elseif action == "normal" then
            obs.obs_data_set_int(settings, "w03_fake_delay", 0)
            obs.obs_data_set_int(settings, "w03_fake_fault", 0)
        elseif action == "reorder" then obs.obs_data_set_bool(settings, "w03_fake_reorder", true)
        elseif action:match("^fault%d$") then obs.obs_data_set_int(settings, "w03_fake_fault", tonumber(action:sub(-1)))
        end
        obs.obs_source_update(filter, settings)
        obs.obs_data_release(settings)
    end
    if filter then obs.obs_source_release(filter) end
end
local function poll()
    if not scene then
        scene_source = obs.obs_frontend_get_current_scene()
        if not scene_source then return end
        scene = obs.obs_scene_from_source(scene_source)
        if not scene then obs.obs_source_release(scene_source); scene_source = nil; return end
    end
    local file = io.open(directory .. "/command.txt", "r")
    if not file then return end
    local line = file:read("*l"); file:close()
    if line and line ~= previous then
        previous = line
        local action = line:match("^%d+ (%w+)$")
        if action then command(action) end
    end
end
function script_description() return "W03 capture validation; dedicated test collection only." end
function script_properties()
    local properties = obs.obs_properties_create()
    obs.obs_properties_add_path(properties, "directory", "Fixture and command directory", obs.OBS_PATH_DIRECTORY, nil, nil)
    obs.obs_properties_add_path(properties, "footage", "Test footage directory", obs.OBS_PATH_DIRECTORY, nil, nil)
    return properties
end
function script_update(settings)
    directory = obs.obs_data_get_string(settings, "directory")
    footage = obs.obs_data_get_string(settings, "footage")
end
function script_load(settings)
    directory = obs.obs_data_get_string(settings, "directory")
    footage = obs.obs_data_get_string(settings, "footage")
    if obs.obs_frontend_get_current_scene_collection() ~= "CorridorKey Test" then return end
    obs.timer_add(poll, 500)
end
function script_unload()
    obs.timer_remove(poll)
    if source then obs.obs_source_release(source); source = nil end
    if scene_source then obs.obs_source_release(scene_source); scene_source = nil; scene = nil end
end
