import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class StarFoxConfigSurfaceTests(unittest.TestCase):
    def test_enhanced_display_mode_aliases_are_wired(self):
        config_c = (ROOT / "src" / "config.c").read_text(encoding="utf-8")

        self.assertIn("ParseEnhancedDisplayMode", config_c)
        for alias in (
            '"0"',
            '"4:3"',
            '"standard_4_3"',
            '"1"',
            '"16:9"',
            '"widescreen_16_9"',
            '"2"',
            '"16:10"',
            '"widescreen_16_10"',
            '"3"',
            '"21:9"',
            '"ultrawide_21_9"',
            '"4"',
            '"32:9"',
            '"super_ultrawide_32_9"',
        ):
            self.assertIn(alias, config_c)
        self.assertIn("g_config.widescreen_extra = 132", config_c)
        self.assertIn("g_config.widescreen_extra = 272", config_c)
        self.assertIn("extra <= kWsExtraMax", config_c)
        config_h = (ROOT / "src" / "config.h").read_text(encoding="utf-8")
        self.assertIn("uint16 widescreen_extra", config_h)
        self.assertIn("bool enhanced_renderer", config_h)
        snes_widescreen_h = (
            ROOT / "snesrecomp" / "runner" / "src" / "widescreen.h"
        ).read_text(encoding="utf-8")
        self.assertIn("kWsExtraMax = 272", snes_widescreen_h)
        snes_ppu_h = (
            ROOT / "snesrecomp" / "runner" / "src" / "snes" / "ppu.h"
        ).read_text(encoding="utf-8")
        self.assertIn("kPpuExtraLeftRight = 272", snes_ppu_h)
        snes_superfx_c = (
            ROOT / "snesrecomp" / "runner" / "src" / "snes" / "superfx.c"
        ).read_text(encoding="utf-8")
        self.assertIn("kSuperFxWsMaxExtra = 288", snes_superfx_c)
        self.assertIn("kSuperFxWsMaxWidth = 800", snes_superfx_c)
        self.assertIn("ParsePresentationFps", config_c)
        self.assertIn("fps == 90", config_c)
        self.assertIn("fps == 480", config_c)
        main_c = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        self.assertIn("ExtraPresentationsAfterFrame", main_c)
        self.assertIn("g_config.presentation_fps > 60 ? 0 : 1", (ROOT / "src" / "opengl.c").read_text(encoding="utf-8"))
        self.assertIn("g_config.presentation_fps <= 60", main_c)

    def test_launcher_exposes_supported_widescreen(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        main_c = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        mods_c = (ROOT / "src" / "starfox_mods.c").read_text(encoding="utf-8")

        self.assertIn("RECOMP_UI_ENABLE_MODS ON", cmake)
        self.assertIn("src/starfox_mods.c", cmake)
        self.assertIn("src/starfox_native_shape.c", cmake)
        self.assertIn("StarFoxLauncherModsProvider", main_c)
        self.assertIn("game_info.mods", main_c)
        self.assertIn("game_info.widescreen_supported = 1", main_c)
        self.assertIn("settings.widescreen_hud = 0", main_c)
        self.assertIn("Display Mode", mods_c)
        self.assertNotIn("Widescreen HUD", mods_c)
        self.assertIn("Enhanced Renderer", mods_c)
        self.assertIn("Crosshair Color", mods_c)
        self.assertIn("God Mode", mods_c)
        self.assertIn("God Nuke", mods_c)
        self.assertIn("Presentation FPS", mods_c)
        self.assertIn("Show FPS", mods_c)
        self.assertIn("max_frames", main_c)
        self.assertIn("ShouldPresentFrame", main_c)
        self.assertIn("DrawPpuFrameWithoutPresent", main_c)
        self.assertIn("g_fps_sample_presentations++", main_c)
        self.assertIn("freq / 4", main_c)
        self.assertIn("PresentationHistoryRecord", main_c)
        self.assertIn("PresentationDebugPresentCurrent", main_c)

    def test_enhanced_renderer_is_explicitly_opt_in(self):
        config_ini = (ROOT / "config.ini").read_text(encoding="utf-8")
        config_c = (ROOT / "src" / "config.c").read_text(encoding="utf-8")
        main_c = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        game_info_c = (ROOT / "src" / "starfox_cpu_infra.c").read_text(encoding="utf-8")
        renderer_c = (
            ROOT / "src" / "starfox_enhanced_renderer.c"
        ).read_text(encoding="utf-8")
        infra_h = (
            ROOT / "snesrecomp" / "runner" / "src" / "common_cpu_infra.h"
        ).read_text(encoding="utf-8")

        self.assertIn("EnhancedRenderer = 0", config_ini)
        self.assertIn('"EnhancedRenderer"', config_c)
        self.assertIn('"NativeRenderer"', config_c)
        self.assertIn('"EnhancedRenderer" },', config_c)
        self.assertIn("g_config.enhanced_renderer ? 1 : 0", config_c)
        self.assertIn("RtlEnhancedRendererFrame", infra_h)
        self.assertIn("enhanced_render_frame", infra_h)
        self.assertIn(".enhanced_render_frame = &StarFoxEnhancedRenderFrame", game_info_c)
        self.assertIn("g_config.enhanced_renderer", main_c)
        self.assertIn("g_config.enhanced_renderer\n                   ? IntMin(g_config.widescreen_extra, kWsExtraMax)\n                   : 0", main_c)
        self.assertIn("RtlDrawDefaultPpuFrame", main_c)
        self.assertIn("StarFoxEnhancedRenderFrame", renderer_c)
        self.assertIn("default_renderer_done", renderer_c)
        self.assertIn("StarFoxDrawPpuFrame();", renderer_c)
        self.assertIn("copy_stock_center", renderer_c)
        self.assertIn("kGsuDrawList = 0x1960", renderer_c)
        self.assertIn("kGsuDlPtr = 0x0202", renderer_c)
        self.assertIn("snapshot_gsu_draw_list", renderer_c)
        self.assertIn("draw_snapshot_shapes", renderer_c)
        self.assertIn("protect_center_left", renderer_c)
        self.assertIn("kDlColTab = 0x16", renderer_c)
        self.assertIn("load_superfx_palette", renderer_c)
        self.assertIn("SNESRECOMP_ENHANCED_RENDERER_STATS", renderer_c)
        native_shape_c = (
            ROOT / "src" / "starfox_native_shape.c"
        ).read_text(encoding="utf-8")
        self.assertIn("StarFoxNativeDrawShapeWireframe", native_shape_c)
        self.assertIn("kBspInit", native_shape_c)
        self.assertIn("kPointsX16", native_shape_c)
        self.assertIn("fill_face", native_shape_c)
        self.assertIn("filled_pixels", native_shape_c)
        self.assertIn("face_bgra", native_shape_c)
        self.assertIn("focal", (ROOT / "third_party" / "starfox-enhanced" / "include" / "starfox" / "render" / "software_renderer.hpp").read_text(encoding="utf-8"))

    def test_starfox_stock_renderer_has_no_widescreen_hacks(self):
        rtl_c = (ROOT / "src" / "starfox_rtl.c").read_text(encoding="utf-8")
        renderer_c = (
            ROOT / "src" / "starfox_enhanced_renderer.c"
        ).read_text(encoding="utf-8")

        self.assertIn("PpuSetExtraSpace(g_ppu, 0)", rtl_c)
        self.assertIn("PpuSetWidescreenLineEnhancer(g_ppu, NULL, NULL)", rtl_c)
        self.assertNotIn("kSuperFxEnhancement_WidescreenLinearProjection", rtl_c)
        self.assertNotIn("starfox_widescreen_line_enhancer", rtl_c)
        self.assertNotIn("PpuSetExtraSpace(g_ppu, (uint16_t)g_ws_extra)", rtl_c)
        self.assertNotIn("PpuSetMode2LayerCapture(g_ppu, g_ws_extra", rtl_c)
        self.assertNotIn("superfx_latch_widescreen_frame", rtl_c)
        self.assertNotIn("clear_side_margins", renderer_c)

    def test_presentation_debugger_keys_are_wired(self):
        config_h = (ROOT / "src" / "config.h").read_text(encoding="utf-8")
        config_c = (ROOT / "src" / "config.c").read_text(encoding="utf-8")
        main_c = (ROOT / "src" / "main.c").read_text(encoding="utf-8")

        for name in (
            "PresentationDebug",
            "PresentationStepForward",
            "PresentationStepBack",
        ):
            self.assertIn(f"kKeys_{name}", config_h)
            self.assertIn(f"S({name})", config_c)
            self.assertIn(f"kKeys_{name}", main_c)
        self.assertIn("kPresentationHistoryFrames = 120", main_c)

    def test_legacy_widescreen_hud_keys_are_parse_only(self):
        config_h = (ROOT / "src" / "config.h").read_text(encoding="utf-8")
        config_c = (ROOT / "src" / "config.c").read_text(encoding="utf-8")
        rtl_c = (ROOT / "src" / "starfox_rtl.c").read_text(encoding="utf-8")
        config_ini = (ROOT / "config.ini").read_text(encoding="utf-8")
        mods_c = (ROOT / "src" / "starfox_mods.c").read_text(encoding="utf-8")

        self.assertIn("bool widescreen_hud", config_h)
        self.assertIn('"WidescreenHud"', config_c)
        self.assertNotIn("WidescreenHud =", config_ini)
        self.assertNotIn("Widescreen HUD", mods_c)
        self.assertNotIn("g_config.widescreen_hud", rtl_c)
        self.assertNotIn("g_config.widescreen_hud_oam", rtl_c)
        self.assertNotIn("PpuSetWsHudOamBand(g_ppu, g_config.widescreen_hud_oam_height", rtl_c)


if __name__ == "__main__":
    unittest.main()
