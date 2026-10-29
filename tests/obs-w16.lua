-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
-- Dedicated portable test collection only. No websocket server or browser source.
local obs = obslua
-- Windows x64: dispatch the C frontend entry point without a Lua callback on
-- another thread. OBS timers run on the scripting thread, not the UI thread.
local ffi = require("ffi")
ffi.cdef[[
void obs_queue_task(int type, void (*task)(void *), void *param, bool wait);
void obs_frontend_reset_video(void);
]]
local core = ffi.load("obs")
local frontend = ffi.load("obs-frontend-api")
local directory, footage, previous = "", "", ""
local backend = "tensorrt"
local sources, items = {}, {}
local scene, away, duplicate_item, real = nil, nil, nil, false
local serial, ticks = 0, 0

local function log(message) obs.script_log(obs.LOG_INFO, "W16 " .. message) end
local function filter_for(index)
    return sources[index] and obs.obs_source_get_filter_by_name(sources[index], "W16 Key " .. index)
end
local function add_filter(index)
    local data = obs.obs_data_create()
    local profile = index == 2 and "balanced" or "facecam-60"
    obs.obs_data_set_int(data, "settings_version", 2)
    obs.obs_data_set_string(data, "preset", profile)
    obs.obs_data_set_string(data, "profile", profile)
    obs.obs_data_set_string(data, "backend", backend)
    obs.obs_data_set_string(data, "transport", "auto")
    obs.obs_data_set_string(data, "grid", index == 2 and "512x512" or "256x512")
    obs.obs_data_set_int(data, "refiner_max_tiles", index == 2 and 512 or 384)
    obs.obs_data_set_bool(data, "edge_refiner", true)
    obs.obs_data_set_bool(data, "refiner_enabled", true)
    obs.obs_data_set_bool(data, "w03_fake_enabled", not real)
    obs.obs_data_set_bool(data, "roi_enabled", index == 2)
    local filter = obs.obs_source_create("corridorkey", "W16 Key " .. index, data, nil)
    obs.obs_data_release(data)
    if filter then
        obs.obs_source_filter_add(sources[index], filter)
        obs.obs_source_release(filter)
    end
end
local function remove_filter(index)
    local filter = filter_for(index)
    if filter then
        obs.obs_source_filter_remove(sources[index], filter)
        obs.obs_source_release(filter)
    end
end
local function add_source(index)
    serial = serial + 1
    local data = obs.obs_data_create()
    local width = index == 1 and 1920 or index == 2 and 3840 or 719
    local height = index == 1 and 1080 or index == 2 and 2160 or 1281
    local kind = "color_source"
    obs.obs_data_set_int(data, "width", width)
    obs.obs_data_set_int(data, "height", height)
    obs.obs_data_set_int(data, "color", 0xff809040 + index * 16)
    if real and index < 3 then
        kind = "ffmpeg_source"
        obs.obs_data_set_bool(data, "is_local_file", true)
        obs.obs_data_set_bool(data, "looping", true)
        obs.obs_data_set_bool(data, "hw_decode", false)
        obs.obs_data_set_bool(data, "restart_on_activate", true)
        local clip = index == 1 and
            "mixkit-female-reporter-reporting-with-microphone-in-hand-on-a-chroma-28293-full-hd.mp4" or
            "1871395_Green_Screen_Dancing_3840x2160.mp4"
        obs.obs_data_set_string(data, "local_file", footage .. "/" .. clip)
    end
    local source = obs.obs_source_create(kind, "W16 Input " .. index .. " " .. serial, data, nil)
    obs.obs_data_release(data)
    sources[index] = source
    if not source then error("source creation failed") end
    obs.obs_source_set_muted(source, true)
    add_filter(index)
    items[index] = obs.obs_scene_add(scene, source)
    local scale = obs.vec2()
    scale.x = 1280 / width; scale.y = scale.x
    obs.obs_sceneitem_set_scale(items[index], scale)
    local position = obs.vec2()
    position.x = (index - 1) * 1280; position.y = 0
    obs.obs_sceneitem_set_pos(items[index], position)
end
local function clear()
    if scene then
        local list = obs.obs_scene_enum_items(scene)
        if list then
            for _, item in ipairs(list) do obs.obs_sceneitem_remove(item) end
            obs.sceneitem_list_release(list)
        end
    end
    for index, source in pairs(sources) do
        obs.obs_source_release(source)
        sources[index] = nil
    end
    items = {}
    duplicate_item = nil
end
local function setup(use_real, count)
    if not scene then
        scene = obs.obs_scene_create("W16 Cameras")
        away = obs.obs_scene_create("W16 Empty")
    end
    clear(); real = use_real
    for index = 1, count do add_source(index) end
    obs.obs_frontend_set_current_scene(obs.obs_scene_get_source(scene))
end
local function change_filter(index, key, value)
    local filter = filter_for(index)
    if not filter then return end
    local data = obs.obs_source_get_settings(filter)
    if type(value) == "boolean" then obs.obs_data_set_bool(data, key, value)
    elseif type(value) == "number" then obs.obs_data_set_int(data, key, value)
    else obs.obs_data_set_string(data, key, value) end
    obs.obs_source_update(filter, data)
    obs.obs_data_release(data)
    obs.obs_source_release(filter)
end
local function status()
    for index, source in pairs(sources) do
        local filter = filter_for(index)
        if filter then
            local data = obs.obs_source_get_settings(filter)
            local properties = obs.obs_source_properties(filter)
            obs.obs_properties_apply_settings(properties, data)
            log("status input=" .. index .. " size=" .. obs.obs_source_get_width(source) .. "x" ..
                obs.obs_source_get_height(source) .. " profile=" .. obs.obs_data_get_string(data, "profile") ..
                " status=" .. obs.obs_data_get_string(data, "status") ..
                " delay=" .. obs.obs_data_get_string(data, "filter_delay"))
            obs.obs_properties_destroy(properties)
            obs.obs_data_release(data)
            obs.obs_source_release(filter)
        end
    end
end
local function command(action)
    log("action=" .. action)
    if action == "fake2" then setup(false, 2)
    elseif action == "real1" then setup(true, 1)
    elseif action == "real2" then setup(true, 2)
    elseif action == "add2" and not sources[2] then add_source(2)
    elseif action == "add3" and not sources[3] then add_source(3)
    elseif action == "remove3" and sources[3] then
        obs.obs_sceneitem_remove(items[3]); items[3] = nil
        obs.obs_source_release(sources[3]); sources[3] = nil
    elseif action == "clear" then clear()
    elseif action == "away" then obs.obs_frontend_set_current_scene(obs.obs_scene_get_source(away))
    elseif action == "back" then obs.obs_frontend_set_current_scene(obs.obs_scene_get_source(scene))
    elseif action == "hide1" and items[1] then obs.obs_sceneitem_set_visible(items[1], false)
    elseif action == "show1" and items[1] then obs.obs_sceneitem_set_visible(items[1], true)
    elseif action == "duplicate" and sources[1] and not duplicate_item then
        duplicate_item = obs.obs_scene_add(scene, sources[1])
    elseif action == "unduplicate" and duplicate_item then
        obs.obs_sceneitem_remove(duplicate_item); duplicate_item = nil
    elseif action == "disable1" or action == "enable1" then
        local filter = filter_for(1)
        if filter then obs.obs_source_set_enabled(filter, action == "enable1"); obs.obs_source_release(filter) end
    elseif action == "remove1" then remove_filter(1)
    elseif action == "readd1" and sources[1] then add_filter(1)
    elseif action == "ck1first" and sources[1] then
        local filter = filter_for(1)
        if filter then
            local data = obs.obs_source_get_settings(filter)
            obs.obs_data_set_string(data, "model_id", "ck1")
            obs.obs_data_set_string(data, "profile", "custom")
            obs.obs_data_set_string(data, "grid", "384x384")
            obs.obs_data_set_string(data, "hint_source", "modnet")
            obs.obs_data_set_bool(data, "refiner_enabled", false)
            obs.obs_source_update(filter, data)
            obs.obs_data_release(data); obs.obs_source_release(filter)
        end
    elseif action == "slow1" then change_filter(1, "w03_fake_delay", 400)
    elseif action == "normal1" then change_filter(1, "w03_fake_delay", 0); change_filter(1, "w03_fake_fault", 0)
    elseif action == "oom1" then change_filter(1, "w03_fake_fault", 1)
    elseif action == "lost1" then change_filter(1, "w03_fake_fault", 2)
    elseif action == "nan1" then change_filter(1, "w03_fake_fault", 3)
    elseif action == "crop1" then change_filter(1, "cut_left", 17)
    elseif action == "portrait1" or action == "restore1" then
        if sources[1] and not real then
            local data = obs.obs_source_get_settings(sources[1])
            obs.obs_data_set_int(data, "width", action == "portrait1" and 719 or 1920)
            obs.obs_data_set_int(data, "height", action == "portrait1" and 1281 or 1080)
            obs.obs_source_update(sources[1], data); obs.obs_data_release(data)
        end
    elseif action == "canvas" then
        local config = obs.obs_frontend_get_profile_config()
        local width = obs.config_get_uint(config, "Video", "BaseCX") == 3840 and 1920 or 3840
        obs.config_set_uint(config, "Video", "BaseCX", width)
        obs.config_set_uint(config, "Video", "BaseCY", width * 9 / 16)
        obs.config_set_uint(config, "Video", "OutputCX", width)
        obs.config_set_uint(config, "Video", "OutputCY", width * 9 / 16)
        core.obs_queue_task(0, ffi.cast("void (*)(void *)", frontend.obs_frontend_reset_video), nil, false)
        log("canvas_requested=" .. width)
    elseif action == "status" then status()
    end
    local ack = io.open(directory .. "/ack.txt", "w")
    if ack then ack:write(previous .. " " .. action); ack:close() end
end
local function poll()
    ticks = ticks + 1
    local input = io.open(directory .. "/command.txt", "r")
    if input then
        local text = input:read("*a"); input:close()
        local id, action = text:match("^(%d+)%s+(%S+)")
        if id and id ~= previous then
            previous = id
            local ok, message = pcall(command, action)
            if not ok then log("FAILED " .. tostring(message)) end
        end
    end
    if ticks % 40 == 0 then status() end
end
function script_description() return "W16 isolated multi-instance lifecycle tests (file commands)." end
function script_load(settings)
    if obs.obs_frontend_get_current_scene_collection() ~= "CorridorKey Test" then
        log("Refusing to run outside CorridorKey Test"); return
    end
    directory = obs.obs_data_get_string(settings, "directory")
    footage = obs.obs_data_get_string(settings, "footage")
    local configured_backend = obs.obs_data_get_string(settings, "backend")
    if configured_backend ~= "" then backend = configured_backend end
    -- Create scenes on the first command, after the frontend finishes loading
    -- its saved collection and releasing its temporary scene references.
    obs.timer_add(poll, 250)
    log("ready")
end
function script_unload()
    obs.timer_remove(poll)
    clear()
    if scene then obs.obs_scene_release(scene); scene = nil end
    if away then obs.obs_scene_release(away); away = nil end
end
