#pragma once

// What Hyprland, the accessibility helper and a page answer, for InspectTests.
// Kept out of the test's own file: moc can't read raw strings with ")" in them.
namespace InspectFixtures {
inline constexpr const char *clients = R"json([
 {"address": "0xa", "mapped": true, "hidden": false, "at": [0, 0], "size": [960, 1080], "workspace": {"id": 1, "name": "1"},
  "floating": false, "monitor": 0, "class": "foot", "title": "~", "pid": 100, "fullscreen": 0, "focusHistoryID": 1},
 {"address": "0xb", "mapped": true, "hidden": false, "at": [100, 100], "size": [400, 300], "workspace": {"id": 1, "name": "1"},
  "floating": true, "monitor": 0, "class": "pavucontrol", "title": "Volume", "pid": 200, "fullscreen": 0, "focusHistoryID": 2},
 {"address": "0xc", "mapped": true, "hidden": false, "at": [0, 0], "size": [1920, 1080], "workspace": {"id": 3, "name": "3"},
  "floating": false, "monitor": 0, "class": "chromium", "title": "Hidden", "pid": 300, "fullscreen": 0, "focusHistoryID": 0},
 {"address": "0xd", "mapped": true, "hidden": false, "at": [600, 200], "size": [500, 500], "workspace": {"id": -98, "name": "special:omastrator-desk"},
  "floating": false, "monitor": 0, "class": "io.github.iretonsean.Omastrator", "title": "Desk", "pid": 400, "fullscreen": 0, "focusHistoryID": 3}
])json";

inline constexpr const char *monitors = R"json([
 {"id": 0, "name": "DP-1", "x": 0, "y": 0, "width": 3840, "height": 2160, "scale": 2, "transform": 0, "focused": true, "reserved": [0, 26, 0, 0],
  "activeWorkspace": {"id": 1, "name": "1"}, "specialWorkspace": {"id": 0, "name": ""}},
 {"id": 1, "name": "HDMI-A-1", "x": 1920, "y": 0, "width": 1080, "height": 1920, "scale": 1, "transform": 1, "focused": false,
  "activeWorkspace": {"id": 2, "name": "2"}, "specialWorkspace": {"id": 0, "name": ""}}
])json";

inline constexpr const char *cursor = R"json({"x": 12, "y": 34})json";

inline constexpr const char *equalsButton = R"json({"role": "push button", "name": "Equals", "rect": [300, 400, 80, 40],
  "text": "=", "fontFamily": "Cantarell", "fontSize": "11", "fontWeight": "700", "color": "#ffffff", "background": "#3584e4"})json";
inline constexpr const char *offWindow = R"json({"role": "panel", "rect": [900, 900, 10, 10]})json";
inline constexpr const char *emptyPanel = R"json({"role": "panel", "rect": [0, 0, 0, 0]})json";

inline constexpr const char *pageAnswer = R"json({"inner": [1196, 700], "scroll": [0, 250], "url": "https://example.com/pricing#plans",
  "element": {"tag": "button", "selector": "button.buy", "rect": [10, 20, 120, 40], "color": "#ffffff", "background": "rgba(51, 85, 255, 0.5)",
  "fontFamily": "Inter", "fontSize": 16, "fontWeight": "600", "styles": {"padding": "8px 16px"}}})json";

inline constexpr const char *page = R"html(<!doctype html><html><body style="margin:0;background:#ffffff">
<button id="buy" class="primary big" style="position:absolute;left:40px;top:60px;width:120px;height:40px;background:#3355ff;color:#ffffff;
 font-family:'DejaVu Sans';font-size:18px;font-weight:700;border:0;border-radius:8px;padding:0">Buy now</button>
<div id="box" style="position:absolute;left:300px;top:60px;width:50px;height:50px;background:rgb(255,0,0)"></div>
</body></html>)html";
}
