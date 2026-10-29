// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
static constexpr Choice choices_profile[] = {{"facecam-60", "Choice.facecam-60"},
					     {"balanced", "Choice.balanced"},
					     {"quality", "Choice.quality"},
					     {"custom", "Choice.custom"}};
static constexpr Choice choices_model_id[] = {{"ck2", "Choice.ck2"}, {"ck1", "Choice.ck1"}};
static constexpr Choice choices_backend[] = {{"auto", "Choice.auto"},
					     {"tensorrt_rtx", "Choice.tensorrt_rtx"},
					     {"tensorrt", "Choice.tensorrt"},
					     {"cuda", "Choice.cuda"},
					     {"directml", "Choice.directml"},
					     {"migraphx", "Choice.migraphx"},
					     {"cpu_diagnostic", "Choice.cpu_diagnostic"}};
static constexpr Choice choices_target_fps[] = {{"0", "Choice.0"},   {"15", "Choice.15"}, {"24", "Choice.24"},
						{"25", "Choice.25"}, {"30", "Choice.30"}, {"50", "Choice.50"},
						{"60", "Choice.60"}};
static constexpr Choice choices_grid[] = {{"256x512", "Choice.256x512"}, {"512x512", "Choice.512x512"},
					  {"512x768", "Choice.512x768"}, {"512x1024", "Choice.512x1024"},
					  {"256x256", "Choice.256x256"}, {"384x384", "Choice.384x384"}};
static constexpr Choice choices_stage2_interval[] = {{"0", "Choice.0"},
						     {"1", "Choice.1"},
						     {"2", "Choice.2"},
						     {"3", "Choice.3"},
						     {"4", "Choice.4"}};
static constexpr Choice choices_guide_mode[] = {{"refiner", "Choice.refiner"},
						{"rgb_guided", "Choice.rgb_guided"},
						{"bilinear", "Choice.bilinear"},
						{"chroma_legacy", "Choice.chroma_legacy"}};
static constexpr Choice choices_unmix_mode[] = {{"delta", "Choice.delta"},
						{"compositing_equation", "Choice.compositing_equation"}};
static constexpr Choice choices_screen_color[] = {{"green", "Choice.green"}, {"blue", "Choice.blue"}};
static constexpr Choice choices_hint_source[] = {{"chroma", "Choice.chroma"},
						 {"ck2_stage1", "Choice.ck2_stage1"},
						 {"modnet", "Choice.modnet"},
						 {"mediapipe", "Choice.mediapipe"},
						 {"external_mask", "Choice.external_mask"},
						 {"distilled", "Choice.distilled"}};
static constexpr Choice choices_hint_mode[] = {{"off", "Choice.off"},
					       {"previous", "Choice.previous"},
					       {"chroma", "Choice.chroma"},
					       {"blend", "Choice.blend"},
					       {"external", "Choice.external"}};
static constexpr Choice choices_obs_view[] = {{"key", "Choice.key"},     {"alpha", "Choice.alpha"},
					      {"hint", "Choice.hint"},   {"foreground", "Choice.foreground"},
					      {"crop", "Choice.crop"},   {"input", "Choice.input"},
					      {"tiles", "Choice.tiles"}, {"roi", "Choice.roi"}};
static constexpr Choice choices_view_mode[] = {{"transparent", "Choice.transparent"},
					       {"checker", "Choice.checker"},
					       {"black", "Choice.black"},
					       {"alpha", "Choice.alpha"},
					       {"side_by_side", "Choice.side_by_side"}};
static constexpr Choice choices_guided_scale[] = {{"1", "Choice.1"}, {"0.5", "Choice.0.5"}};
static constexpr Choice choices_precision[] = {{"auto", "Choice.auto"},
					       {"fp16", "Choice.fp16"},
					       {"fp32", "Choice.fp32"},
					       {"bf16", "Choice.bf16"}};
static constexpr Choice choices_transport[] = {{"auto", "Choice.auto"},
					       {"interop", "Choice.interop"},
					       {"staged_grid", "Choice.staged_grid"},
					       {"staged_tiles", "Choice.staged_tiles"},
					       {"staged_full_frame", "Choice.staged_full_frame"}};
static const Field fields[] = {

	{"profile", "top", Kind::choice, Level::normal, Disposition::active, 0, 0, 0, "", "", choices_profile,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.quality.profile == Profile::facecam    ? "facecam-60"
					  : s.quality.profile == Profile::balanced ? "balanced"
					  : s.quality.profile == Profile::quality  ? "quality"
										   : "custom")};
	 },
	 [](Settings &s, const Value &v) {
		 s.quality.profile = std::get<std::string>(v) == "facecam-60" ? Profile::facecam
				     : std::get<std::string>(v) == "balanced" ? Profile::balanced
				     : std::get<std::string>(v) == "quality"  ? Profile::quality
									      : Profile::custom;
	 }},

	{"model_id", "top", Kind::choice, Level::normal, Disposition::active, 0, 0, 0, "", "", choices_model_id,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.quality.model == ModelFamily::ck2 ? "ck2" : "ck1")};
	 },
	 [](Settings &s, const Value &v) {
		 s.quality.model = std::get<std::string>(v) == "ck2" ? ModelFamily::ck2 : ModelFamily::ck1;
	 }},
	{"model_revision",
	 "top",
	 Kind::text,
	 Level::internal,
	 Disposition::active,
	 0,
	 0,
	 0,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{s.quality.model_revision}; },
	 [](Settings &s, const Value &v) {
		 s.quality.model_revision = std::get<std::string>(v);
	 }},

	{"backend", "performance", Kind::choice, Level::advanced, Disposition::active, 0, 0, 0, "", "", choices_backend,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.backend.backend == Backend::automatic      ? "auto"
					  : s.backend.backend == Backend::tensorrt_rtx ? "tensorrt_rtx"
					  : s.backend.backend == Backend::tensorrt     ? "tensorrt"
					  : s.backend.backend == Backend::cuda         ? "cuda"
					  : s.backend.backend == Backend::directml     ? "directml"
					  : s.backend.backend == Backend::migraphx     ? "migraphx"
										       : "cpu_diagnostic")};
	 },
	 [](Settings &s, const Value &v) {
		 s.backend.backend = std::get<std::string>(v) == "auto"           ? Backend::automatic
				     : std::get<std::string>(v) == "tensorrt_rtx" ? Backend::tensorrt_rtx
				     : std::get<std::string>(v) == "tensorrt"     ? Backend::tensorrt
				     : std::get<std::string>(v) == "cuda"         ? Backend::cuda
				     : std::get<std::string>(v) == "directml"     ? Backend::directml
				     : std::get<std::string>(v) == "migraphx"     ? Backend::migraphx
										  : Backend::cpu_diagnostic;
	 }},
	{"device",
	 "performance",
	 Kind::text,
	 Level::expert,
	 Disposition::active,
	 0,
	 0,
	 0,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{s.backend.device_id}; },
	 [](Settings &s, const Value &v) {
		 s.backend.device_id = std::get<std::string>(v);
	 }},
	{"target_fps", "performance", Kind::choice, Level::advanced, Disposition::active, 0, 0, 0, "", "",
	 choices_target_fps, [](const Settings &s) -> Value { return Value{number_string(s.quality.target_fps)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.target_fps = static_cast<std::uint32_t>(std::stod(std::get<std::string>(v)));
	 }},
	{"grid", "performance", Kind::choice, Level::advanced, Disposition::active, 0, 0, 0, "", "", choices_grid,
	 [](const Settings &s) -> Value { return Value{grid_string(s.quality.grid)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.grid = parse_grid(std::get<std::string>(v));
	 }},
	{"roi_enabled",
	 "performance",
	 Kind::boolean,
	 Level::advanced,
	 Disposition::active,
	 0,
	 1,
	 1,
	 "",
	 "ck2",
	 {},
	 [](const Settings &s) -> Value { return Value{s.quality.roi_enabled}; },
	 [](Settings &s, const Value &v) {
		 s.quality.roi_enabled = std::get<bool>(v);
	 }},
	{"stage2_interval", "performance", Kind::choice, Level::advanced, Disposition::active, 0, 0, 0, "", "ck2",
	 choices_stage2_interval,
	 [](const Settings &s) -> Value { return Value{number_string(s.quality.stage2_interval)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.stage2_interval = static_cast<std::uint32_t>(std::stod(std::get<std::string>(v)));
	 }},
	{"refiner_max_tiles",
	 "performance",
	 Kind::integer,
	 Level::advanced,
	 Disposition::active,
	 64,
	 1536,
	 64,
	 "",
	 "refiner",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.quality.refiner_max_tiles)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.refiner_max_tiles =
			 static_cast<decltype(s.quality.refiner_max_tiles)>(std::get<std::int64_t>(v));
	 }},
	{"roi_padding",
	 "performance",
	 Kind::number,
	 Level::advanced,
	 Disposition::active,
	 0.05,
	 0.5,
	 0.05,
	 "",
	 "roi",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.roi_padding)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.roi_padding = static_cast<float>(std::get<double>(v));
	 }},
	{"use_rvm_temporal",
	 "performance",
	 Kind::boolean,
	 Level::advanced,
	 Disposition::active,
	 0,
	 1,
	 1,
	 "",
	 "ck2",
	 {},
	 [](const Settings &s) -> Value { return Value{s.quality.use_rvm_temporal}; },
	 [](Settings &s, const Value &v) {
		 s.quality.use_rvm_temporal = std::get<bool>(v);
	 }},
	{"refiner_enabled",
	 "edge",
	 Kind::boolean,
	 Level::normal,
	 Disposition::active,
	 0,
	 1,
	 1,
	 "",
	 "ck1_refiner",
	 {},
	 [](const Settings &s) -> Value { return Value{s.quality.refiner_enabled}; },
	 [](Settings &s, const Value &v) {
		 s.quality.refiner_enabled = std::get<bool>(v);
	 }},

	{"guide_mode", "edge", Kind::choice, Level::advanced, Disposition::active, 0, 0, 0, "", "", choices_guide_mode,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.quality.guide_mode == GuideMode::refiner      ? "refiner"
					  : s.quality.guide_mode == GuideMode::rgb_guided ? "rgb_guided"
					  : s.quality.guide_mode == GuideMode::bilinear   ? "bilinear"
											  : "chroma_legacy")};
	 },
	 [](Settings &s, const Value &v) {
		 s.quality.guide_mode = std::get<std::string>(v) == "refiner"      ? GuideMode::refiner
					: std::get<std::string>(v) == "rgb_guided" ? GuideMode::rgb_guided
					: std::get<std::string>(v) == "bilinear"   ? GuideMode::bilinear
										   : GuideMode::chroma_legacy;
	 }},
	{"refiner_model",
	 "edge",
	 Kind::text,
	 Level::advanced,
	 Disposition::active,
	 0,
	 0,
	 0,
	 "",
	 "refiner",
	 {},
	 [](const Settings &s) -> Value { return Value{s.quality.refiner_model}; },
	 [](Settings &s, const Value &v) {
		 s.quality.refiner_model = std::get<std::string>(v);
	 }},
	{"guided_radius",
	 "edge",
	 Kind::integer,
	 Level::advanced,
	 Disposition::active,
	 1,
	 12,
	 1,
	 " px",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.quality.guided_radius)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.guided_radius = static_cast<decltype(s.quality.guided_radius)>(std::get<std::int64_t>(v));
	 }},
	{"guided_eps",
	 "edge",
	 Kind::number,
	 Level::expert,
	 Disposition::active,
	 1e-05,
	 0.01,
	 1e-05,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.guided_epsilon)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.guided_epsilon = static_cast<float>(std::get<double>(v));
	 }},
	{"alpha_contrast",
	 "edge",
	 Kind::number,
	 Level::normal,
	 Disposition::active,
	 0.7,
	 1.5,
	 0.01,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.alpha_contrast)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.alpha_contrast = static_cast<float>(std::get<double>(v));
	 }},
	{"alpha_offset",
	 "edge",
	 Kind::number,
	 Level::normal,
	 Disposition::active,
	 -0.2,
	 0.2,
	 0.005,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.alpha_offset)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.alpha_offset = static_cast<float>(std::get<double>(v));
	 }},
	{"clip_black",
	 "edge",
	 Kind::number,
	 Level::advanced,
	 Disposition::active,
	 0,
	 0.5,
	 0.005,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.clip_black)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.clip_black = static_cast<float>(std::get<double>(v));
	 }},
	{"clip_white",
	 "edge",
	 Kind::number,
	 Level::advanced,
	 Disposition::active,
	 0.5,
	 1,
	 0.005,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.clip_white)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.clip_white = static_cast<float>(std::get<double>(v));
	 }},
	{"refiner_band_lo",
	 "edge",
	 Kind::number,
	 Level::expert,
	 Disposition::active,
	 0,
	 0.1,
	 0.001,
	 "",
	 "refiner",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.refiner_band_lo)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.refiner_band_lo = static_cast<float>(std::get<double>(v));
	 }},
	{"refiner_band_gain",
	 "edge",
	 Kind::number,
	 Level::expert,
	 Disposition::active,
	 0,
	 20,
	 0.5,
	 "",
	 "refiner",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.refiner_band_gain)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.refiner_band_gain = static_cast<float>(std::get<double>(v));
	 }},

	{"unmix_mode", "color", Kind::choice, Level::advanced, Disposition::active, 0, 0, 0, "", "", choices_unmix_mode,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.quality.unmix_mode == UnmixMode::delta ? "delta" : "compositing_equation")};
	 },
	 [](Settings &s, const Value &v) {
		 s.quality.unmix_mode = std::get<std::string>(v) == "delta" ? UnmixMode::delta
									    : UnmixMode::compositing_equation;
	 }},
	{"unmix_inner",
	 "color",
	 Kind::number,
	 Level::advanced,
	 Disposition::active,
	 0,
	 1,
	 0.05,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.unmix_inner)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.unmix_inner = static_cast<float>(std::get<double>(v));
	 }},
	{"despill_strength",
	 "color",
	 Kind::number,
	 Level::normal,
	 Disposition::active,
	 0,
	 1,
	 0.02,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.despill_strength)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.despill_strength = static_cast<float>(std::get<double>(v));
	 }},

	{"screen_color", "color", Kind::choice, Level::normal, Disposition::active, 0, 0, 0, "", "ck1",
	 choices_screen_color,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.quality.screen_color == ScreenColor::green ? "green" : "blue")};
	 },
	 [](Settings &s, const Value &v) {
		 s.quality.screen_color = std::get<std::string>(v) == "green" ? ScreenColor::green : ScreenColor::blue;
	 }},
	{"cut_left",
	 "crop",
	 Kind::number,
	 Level::normal,
	 Disposition::active,
	 0,
	 45,
	 0.5,
	 " %",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.context.crop.left) * 100.0}; },
	 [](Settings &s, const Value &v) {
		 s.context.crop.left = static_cast<float>(std::get<double>(v) / 100.0);
	 }},
	{"cut_right",
	 "crop",
	 Kind::number,
	 Level::normal,
	 Disposition::active,
	 0,
	 45,
	 0.5,
	 " %",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(1.0f - s.context.crop.right) * 100.0}; },
	 [](Settings &s, const Value &v) {
		 s.context.crop.right = 1.0f - static_cast<float>(std::get<double>(v) / 100.0);
	 }},
	{"cut_top",
	 "crop",
	 Kind::number,
	 Level::normal,
	 Disposition::active,
	 0,
	 45,
	 0.5,
	 " %",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.context.crop.top) * 100.0}; },
	 [](Settings &s, const Value &v) {
		 s.context.crop.top = static_cast<float>(std::get<double>(v) / 100.0);
	 }},
	{"cut_bottom",
	 "crop",
	 Kind::number,
	 Level::normal,
	 Disposition::active,
	 0,
	 45,
	 0.5,
	 " %",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(1.0f - s.context.crop.bottom) * 100.0}; },
	 [](Settings &s, const Value &v) {
		 s.context.crop.bottom = 1.0f - static_cast<float>(std::get<double>(v) / 100.0);
	 }},

	{"hint_source", "hint", Kind::choice, Level::normal, Disposition::active, 0, 0, 0, "", "ck1",
	 choices_hint_source,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.hint.source == HintSource::chroma          ? "chroma"
					  : s.hint.source == HintSource::ck2_stage1    ? "ck2_stage1"
					  : s.hint.source == HintSource::modnet        ? "modnet"
					  : s.hint.source == HintSource::mediapipe     ? "mediapipe"
					  : s.hint.source == HintSource::external_mask ? "external_mask"
										       : "distilled")};
	 },
	 [](Settings &s, const Value &v) {
		 s.hint.source = std::get<std::string>(v) == "chroma"          ? HintSource::chroma
				 : std::get<std::string>(v) == "ck2_stage1"    ? HintSource::ck2_stage1
				 : std::get<std::string>(v) == "modnet"        ? HintSource::modnet
				 : std::get<std::string>(v) == "mediapipe"     ? HintSource::mediapipe
				 : std::get<std::string>(v) == "external_mask" ? HintSource::external_mask
									       : HintSource::distilled;
	 }},
	{"hint_similarity",
	 "hint",
	 Kind::number,
	 Level::advanced,
	 Disposition::active,
	 0,
	 1,
	 0.01,
	 "",
	 "chroma",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.hint.similarity)}; },
	 [](Settings &s, const Value &v) {
		 s.hint.similarity = static_cast<float>(std::get<double>(v));
	 }},
	{"hint_smoothness",
	 "hint",
	 Kind::number,
	 Level::advanced,
	 Disposition::active,
	 0.01,
	 0.5,
	 0.01,
	 "",
	 "chroma",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.hint.smoothness)}; },
	 [](Settings &s, const Value &v) {
		 s.hint.smoothness = static_cast<float>(std::get<double>(v));
	 }},
	{"hint_refresh",
	 "hint",
	 Kind::integer,
	 Level::advanced,
	 Disposition::active,
	 1,
	 8,
	 1,
	 "",
	 "ck1",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.hint.refresh_interval)}; },
	 [](Settings &s, const Value &v) {
		 s.hint.refresh_interval = static_cast<decltype(s.hint.refresh_interval)>(std::get<std::int64_t>(v));
	 }},
	{"hint_mask",
	 "hint",
	 Kind::text,
	 Level::expert,
	 Disposition::experimental,
	 0,
	 0,
	 0,
	 "",
	 "mask",
	 {},
	 [](const Settings &s) -> Value { return Value{s.hint.mask_source_id}; },
	 [](Settings &s, const Value &v) {
		 s.hint.mask_source_id = std::get<std::string>(v);
	 }},

	{"hint_mode", "hint", Kind::choice, Level::expert, Disposition::experimental, 0, 0, 0, "", "ck2",
	 choices_hint_mode,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.hint.ck2_mode == HintMode::off        ? "off"
					  : s.hint.ck2_mode == HintMode::previous ? "previous"
					  : s.hint.ck2_mode == HintMode::chroma   ? "chroma"
					  : s.hint.ck2_mode == HintMode::blend    ? "blend"
										  : "external")};
	 },
	 [](Settings &s, const Value &v) {
		 s.hint.ck2_mode = std::get<std::string>(v) == "off"        ? HintMode::off
				   : std::get<std::string>(v) == "previous" ? HintMode::previous
				   : std::get<std::string>(v) == "chroma"   ? HintMode::chroma
				   : std::get<std::string>(v) == "blend"    ? HintMode::blend
									    : HintMode::external;
	 }},
	{"temporal_hint_quality",
	 "hint",
	 Kind::number,
	 Level::expert,
	 Disposition::experimental,
	 0,
	 0.95,
	 0.05,
	 "",
	 "ck2",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.hint.temporal_quality)}; },
	 [](Settings &s, const Value &v) {
		 s.hint.temporal_quality = static_cast<float>(std::get<double>(v));
	 }},
	{"chroma_hint_quality",
	 "hint",
	 Kind::number,
	 Level::expert,
	 Disposition::experimental,
	 0,
	 1,
	 0.05,
	 "",
	 "ck2",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.hint.chroma_quality)}; },
	 [](Settings &s, const Value &v) {
		 s.hint.chroma_quality = static_cast<float>(std::get<double>(v));
	 }},
	{"external_hint_quality",
	 "hint",
	 Kind::number,
	 Level::expert,
	 Disposition::experimental,
	 0,
	 1,
	 0.05,
	 "",
	 "ck2",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.hint.external_quality)}; },
	 [](Settings &s, const Value &v) {
		 s.hint.external_quality = static_cast<float>(std::get<double>(v));
	 }},
	{"external_hint_soft",
	 "hint",
	 Kind::integer,
	 Level::expert,
	 Disposition::experimental,
	 0,
	 64,
	 1,
	 "",
	 "mask",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.hint.external_blur)}; },
	 [](Settings &s, const Value &v) {
		 s.hint.external_blur = static_cast<decltype(s.hint.external_blur)>(std::get<std::int64_t>(v));
	 }},

	{"obs_view", "output", Kind::choice, Level::normal, Disposition::active, 0, 0, 0, "", "", choices_obs_view,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.context.output == OutputView::key          ? "key"
					  : s.context.output == OutputView::alpha      ? "alpha"
					  : s.context.output == OutputView::hint       ? "hint"
					  : s.context.output == OutputView::foreground ? "foreground"
					  : s.context.output == OutputView::crop       ? "crop"
					  : s.context.output == OutputView::input      ? "input"
					  : s.context.output == OutputView::tiles      ? "tiles"
										       : "roi")};
	 },
	 [](Settings &s, const Value &v) {
		 s.context.output = std::get<std::string>(v) == "key"          ? OutputView::key
				    : std::get<std::string>(v) == "alpha"      ? OutputView::alpha
				    : std::get<std::string>(v) == "hint"       ? OutputView::hint
				    : std::get<std::string>(v) == "foreground" ? OutputView::foreground
				    : std::get<std::string>(v) == "crop"       ? OutputView::crop
				    : std::get<std::string>(v) == "input"      ? OutputView::input
				    : std::get<std::string>(v) == "tiles"      ? OutputView::tiles
									       : OutputView::roi;
	 }},

	{"view_mode", "output", Kind::choice, Level::advanced, Disposition::active, 0, 0, 0, "", "", choices_view_mode,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.context.inspection == InspectionView::transparent ? "transparent"
					  : s.context.inspection == InspectionView::checker   ? "checker"
					  : s.context.inspection == InspectionView::black     ? "black"
					  : s.context.inspection == InspectionView::alpha     ? "alpha"
											      : "side_by_side")};
	 },
	 [](Settings &s, const Value &v) {
		 s.context.inspection = std::get<std::string>(v) == "transparent" ? InspectionView::transparent
					: std::get<std::string>(v) == "checker"   ? InspectionView::checker
					: std::get<std::string>(v) == "black"     ? InspectionView::black
					: std::get<std::string>(v) == "alpha"     ? InspectionView::alpha
										  : InspectionView::side_by_side;
	 }},
	{"bg_color_rgb",
	 "output",
	 Kind::color,
	 Level::advanced,
	 Disposition::active,
	 0,
	 1,
	 0,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value {
		 return Value{static_cast<std::int64_t>(pack_color(s.context.preview_color))};
	 },
	 [](Settings &s, const Value &v) {
		 s.context.preview_color = unpack_color(std::get<std::int64_t>(v));
	 }},
	{"bypass",
	 "output",
	 Kind::boolean,
	 Level::normal,
	 Disposition::active,
	 0,
	 1,
	 1,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{s.context.bypass}; },
	 [](Settings &s, const Value &v) {
		 s.context.bypass = std::get<bool>(v);
	 }},
	{"auto_sync",
	 "sync",
	 Kind::boolean,
	 Level::normal,
	 Disposition::active,
	 0,
	 1,
	 1,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{s.context.auto_sync}; },
	 [](Settings &s, const Value &v) {
		 s.context.auto_sync = std::get<bool>(v);
	 }},
	{"audio_source_id",
	 "sync",
	 Kind::text,
	 Level::normal,
	 Disposition::active,
	 0,
	 0,
	 0,
	 "",
	 "sync",
	 {},
	 [](const Settings &s) -> Value { return Value{s.context.audio_source_id}; },
	 [](Settings &s, const Value &v) {
		 s.context.audio_source_id = std::get<std::string>(v);
	 }},
	{"sync_trim_ms",
	 "sync",
	 Kind::integer,
	 Level::advanced,
	 Disposition::active,
	 -250,
	 250,
	 1,
	 " ms",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.context.sync_trim_ms)}; },
	 [](Settings &s, const Value &v) {
		 s.context.sync_trim_ms = static_cast<decltype(s.context.sync_trim_ms)>(std::get<std::int64_t>(v));
	 }},
	{"sync_limit_ms",
	 "sync",
	 Kind::integer,
	 Level::expert,
	 Disposition::active,
	 50,
	 1000,
	 10,
	 " ms",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.context.sync_limit_ms)}; },
	 [](Settings &s, const Value &v) {
		 s.context.sync_limit_ms = static_cast<decltype(s.context.sync_limit_ms)>(std::get<std::int64_t>(v));
	 }},
	{"stale_timeout_ms",
	 "sync",
	 Kind::integer,
	 Level::expert,
	 Disposition::active,
	 100,
	 1000,
	 10,
	 " ms",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.context.stale_timeout_ms)}; },
	 [](Settings &s, const Value &v) {
		 s.context.stale_timeout_ms =
			 static_cast<decltype(s.context.stale_timeout_ms)>(std::get<std::int64_t>(v));
	 }},
	{"guided_scale", "expert", Kind::choice, Level::expert, Disposition::active, 0, 0, 0, "", "",
	 choices_guided_scale, [](const Settings &s) -> Value { return Value{number_string(s.quality.guided_scale)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.guided_scale = static_cast<float>(std::stod(std::get<std::string>(v)));
	 }},
	{"aspect_fix",
	 "expert",
	 Kind::boolean,
	 Level::expert,
	 Disposition::active,
	 0,
	 1,
	 1,
	 "",
	 "",
	 {},
	 [](const Settings &s) -> Value { return Value{s.quality.aspect_fix}; },
	 [](Settings &s, const Value &v) {
		 s.quality.aspect_fix = std::get<bool>(v);
	 }},
	{"moge_interval",
	 "expert",
	 Kind::integer,
	 Level::expert,
	 Disposition::active,
	 1,
	 8,
	 1,
	 "",
	 "ck2",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.quality.moge_interval)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.moge_interval = static_cast<decltype(s.quality.moge_interval)>(std::get<std::int64_t>(v));
	 }},
	{"moge_motion_threshold",
	 "expert",
	 Kind::number,
	 Level::expert,
	 Disposition::active,
	 0.005,
	 0.15,
	 0.005,
	 "",
	 "ck2",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.moge_motion_threshold)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.moge_motion_threshold = static_cast<float>(std::get<double>(v));
	 }},
	{"roi_smoothing",
	 "expert",
	 Kind::number,
	 Level::expert,
	 Disposition::active,
	 0,
	 0.95,
	 0.05,
	 "",
	 "roi",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.quality.roi_smoothing)}; },
	 [](Settings &s, const Value &v) {
		 s.quality.roi_smoothing = static_cast<float>(std::get<double>(v));
	 }},
	{"stagger_s1_s2",
	 "expert",
	 Kind::boolean,
	 Level::expert,
	 Disposition::experimental,
	 0,
	 1,
	 1,
	 "",
	 "stagger",
	 {},
	 [](const Settings &s) -> Value { return Value{s.experimental.stagger_stages}; },
	 [](Settings &s, const Value &v) {
		 s.experimental.stagger_stages = std::get<bool>(v);
	 }},

	{"precision", "expert", Kind::choice, Level::expert, Disposition::active, 0, 0, 0, "", "", choices_precision,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.backend.precision == Precision::automatic ? "auto"
					  : s.backend.precision == Precision::fp16    ? "fp16"
					  : s.backend.precision == Precision::fp32    ? "fp32"
										      : "bf16")};
	 },
	 [](Settings &s, const Value &v) {
		 s.backend.precision = std::get<std::string>(v) == "auto"   ? Precision::automatic
				       : std::get<std::string>(v) == "fp16" ? Precision::fp16
				       : std::get<std::string>(v) == "fp32" ? Precision::fp32
									    : Precision::bf16;
	 }},

	{"transport", "expert", Kind::choice, Level::expert, Disposition::active, 0, 0, 0, "", "", choices_transport,
	 [](const Settings &s) -> Value {
		 return Value{std::string(s.backend.transport == TransportKind::automatic      ? "auto"
					  : s.backend.transport == TransportKind::interop      ? "interop"
					  : s.backend.transport == TransportKind::staged_grid  ? "staged_grid"
					  : s.backend.transport == TransportKind::staged_tiles ? "staged_tiles"
											       : "staged_full_frame")};
	 },
	 [](Settings &s, const Value &v) {
		 s.backend.transport = std::get<std::string>(v) == "auto"           ? TransportKind::automatic
				       : std::get<std::string>(v) == "interop"      ? TransportKind::interop
				       : std::get<std::string>(v) == "staged_grid"  ? TransportKind::staged_grid
				       : std::get<std::string>(v) == "staged_tiles" ? TransportKind::staged_tiles
										    : TransportKind::staged_full_frame;
	 }},
	{"head_pass",
	 "expert",
	 Kind::boolean,
	 Level::expert,
	 Disposition::experimental,
	 0,
	 1,
	 1,
	 "",
	 "head",
	 {},
	 [](const Settings &s) -> Value { return Value{s.experimental.head_pass}; },
	 [](Settings &s, const Value &v) {
		 s.experimental.head_pass = std::get<bool>(v);
	 }},
	{"head_size",
	 "expert",
	 Kind::integer,
	 Level::expert,
	 Disposition::experimental,
	 512,
	 768,
	 256,
	 "",
	 "head_on",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.experimental.head_size)}; },
	 [](Settings &s, const Value &v) {
		 s.experimental.head_size = static_cast<decltype(s.experimental.head_size)>(std::get<std::int64_t>(v));
	 }},
	{"head_max_tiles",
	 "expert",
	 Kind::integer,
	 Level::expert,
	 Disposition::experimental,
	 64,
	 1536,
	 64,
	 "",
	 "head_on",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<std::int64_t>(s.experimental.head_max_tiles)}; },
	 [](Settings &s, const Value &v) {
		 s.experimental.head_max_tiles =
			 static_cast<decltype(s.experimental.head_max_tiles)>(std::get<std::int64_t>(v));
	 }},
	{"head_min_gain",
	 "expert",
	 Kind::number,
	 Level::expert,
	 Disposition::experimental,
	 1,
	 4,
	 0.1,
	 "",
	 "head_on",
	 {},
	 [](const Settings &s) -> Value { return Value{static_cast<double>(s.experimental.head_min_gain)}; },
	 [](Settings &s, const Value &v) {
		 s.experimental.head_min_gain = static_cast<float>(std::get<double>(v));
	 }},
};
