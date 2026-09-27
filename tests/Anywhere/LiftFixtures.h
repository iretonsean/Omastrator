#pragma once

// Pages and trees for LiftTests. Kept out of the test's own file: moc can't read raw strings with ")" in them.
namespace LiftFixtures {
// A card: a gradient, a border, uneven corners and overflow clip, holding mixed-style text, an inline SVG, an image
// and a box that spills out of it; beside it a rotated box.
inline constexpr const char *page = R"html(<!doctype html><html><body style="margin:0;background:#f0f0f0">
<div id="card" aria-label="Pricing card" style="position:absolute;left:40px;top:30px;width:300px;height:220px;
 border-radius:12px 12px 4px 4px;background:linear-gradient(to right, #ff0000, #0000ff);border:2px solid #00ff00;overflow:hidden;
 font-family:'DejaVu Sans'">
  <p id="title" style="margin:0;position:absolute;left:20px;top:20px;width:260px;font-size:20px;line-height:30px;letter-spacing:1px;
   color:#ffffff">Plain <b style="color:#ffff00">bold</b> text</p>
  <svg id="icon" style="position:absolute;left:20px;top:70px" width="40" height="40" viewBox="0 0 20 20"><rect x="0" y="0" width="20"
   height="20" fill="#123456"/></svg>
  <img id="pic" src="pic.png" style="position:absolute;left:80px;top:70px;width:40px;height:40px">
  <div id="spill" style="position:absolute;left:250px;top:150px;width:100px;height:100px;background:#000000"></div>
</div>
<div id="turned" style="position:absolute;left:400px;top:50px;width:100px;height:50px;background:#00ff00;transform:rotate(90deg)"></div>
</body></html>)html";

// A dialog as AT-SPI describes it, in window coordinates.
inline constexpr const char *tree = R"json({"app": "zenity", "root": {"role": "frame", "name": "Question", "rect": [0, 0, 400, 300], "index": 0,
  "children": [
    {"role": "label", "name": "", "rect": [20, 20, 200, 24], "index": 0, "text": "Delete the file?", "textRect": [20, 22, 150, 20],
     "fontFamily": "DejaVu Sans", "fontSize": "11", "fontWeight": "400", "color": "#202020"},
    {"role": "push button", "name": "OK", "rect": [300, 250, 80, 32], "index": 1,
     "children": [{"role": "label", "name": "OK", "rect": [320, 256, 40, 20], "index": 0, "text": "OK"}]}
  ]}})json";
}
