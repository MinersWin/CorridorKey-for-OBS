-- SPDX-License-Identifier: GPL-3.0-or-later
-- Dedicated portable collection. Commands use a local file; no network listener.
local obs = obslua
local directory, previous = "", ""
local scene, source = nil, nil
local function tick()
    local file = io.open(directory .. "/command.json", "r")
    if not file then return end
    local text = file:read("*a"); file:close()
    if text == previous then return end
    local data = obs.obs_data_create_from_json(text)
    if not data then return end
    previous = text
    if not scene then scene = obs.obs_scene_create("W11 Measurement") end
    if not source then
        local image = obs.obs_data_create()
        obs.obs_data_set_string(image, "file", obs.obs_data_get_string(data, "image"))
        source = obs.obs_source_create("image_source", "W11 Input", image, nil)
        obs.obs_data_release(image)
        obs.obs_scene_add(scene, source)
        obs.obs_frontend_set_current_scene(obs.obs_scene_get_source(scene))
    end
    local filter = obs.obs_source_get_filter_by_name(source, "W11 Key")
    local settings = obs.obs_data_get_obj(data, "settings")
    if filter then
        obs.obs_source_update(filter, settings)
    else
        filter = obs.obs_source_create("corridorkey", "W11 Key", settings, nil)
        obs.obs_source_filter_add(source, filter)
    end
    obs.obs_source_release(filter)
    obs.obs_data_release(settings)
    obs.script_log(obs.LOG_INFO, "W11 case=" .. obs.obs_data_get_string(data, "case"))
    obs.obs_data_release(data)
end
function script_description() return "W11 DirectML measurement, dedicated portable OBS only." end
function script_update(settings) directory = obs.obs_data_get_string(settings, "directory") end
function script_load(settings)
    if obs.obs_frontend_get_current_scene_collection() ~= "CorridorKey Test" then return end
    script_update(settings); obs.timer_add(tick, 250)
end
function script_unload()
    obs.timer_remove(tick)
    if source then obs.obs_source_release(source) end
    if scene then obs.obs_scene_release(scene) end
end
