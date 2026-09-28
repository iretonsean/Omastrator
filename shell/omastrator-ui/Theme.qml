pragma Singleton
import QtQuick
import qs.Commons

// The installed Omarchy theme's look, for the island, the floating bar and
// their cards. A theme with component tokens (Graphite's [graphite] section
// in shell.toml) gives its surfaces, text tones, fonts and shape; any other
// theme gets the same roles made from its popup colours and font.
QtObject {
  id: root

  // Graphite's tokens are in shell.toml as graphite.<token>.
  readonly property bool graphite: Color.shellValues["graphite.surface-1"] !== undefined

  function parseColor(raw, fallback) {
    var s = String(raw || "").trim()
    var rgba = s.match(/^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([\d.]+))?\s*\)$/)
    if (rgba) return Qt.rgba(rgba[1] / 255, rgba[2] / 255, rgba[3] / 255, rgba[4] === undefined ? 1 : Number(rgba[4]))
    var hex = s.match(/^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$/)
    if (hex) {
      var h = hex[1]
      return Qt.rgba(parseInt(h.substr(0, 2), 16) / 255, parseInt(h.substr(2, 2), 16) / 255,
                     parseInt(h.substr(4, 2), 16) / 255, hex[2] ? parseInt(hex[2], 16) / 255 : 1)
    }
    return fallback
  }

  function token(name, fallback) {
    return parseColor(Color.shellValues["graphite." + name], fallback)
  }

  // Colour a over b, by a's weight.
  function mix(a, b, weight) {
    return Qt.rgba(a.r * weight + b.r * (1 - weight), a.g * weight + b.g * (1 - weight),
                   a.b * weight + b.b * (1 - weight), 1)
  }

  readonly property color _paper: Color.popups.background
  readonly property color _ink: Color.popups.text

  // Surfaces: the panel, a group or resting control on it, a track, and
  // the lift under the pointer.
  readonly property color surface0: token("surface-0", mix(_ink, _paper, 0.0))
  readonly property color surface1: token("surface-1", _paper)
  readonly property color surface2: token("surface-2", mix(_ink, _paper, 0.06))
  readonly property color surface3: token("surface-3", mix(_ink, _paper, 0.1))
  readonly property color lift: token("lift", mix(_ink, _paper, 0.13))

  // Text, from titles down to disabled.
  readonly property color text1: token("text-1", _ink)
  readonly property color text2: token("text-2", _ink)
  readonly property color text3: token("text-3", mix(_ink, _paper, 0.62))
  readonly property color text4: token("text-4", mix(_ink, _paper, 0.4))

  // The accent marks what is chosen, never where the pointer is.
  readonly property color accent: token("accent", Color.accent)
  readonly property color accentSoft: token("accent-soft", Color.accent)
  readonly property color onAccent: token("on-accent", _paper)
  readonly property color labelTint: token("label-tint", mix(_ink, _paper, 0.6))
  readonly property color ok: token("ok", Color.accent)
  readonly property color warn: token("warn", Color.urgent)
  readonly property color error: token("error", Color.urgent)
  readonly property color highlight: token("highlight", Qt.rgba(1, 1, 1, 0.06))
  readonly property color keycapEdge: token("keycap-edge", Qt.rgba(1, 1, 1, 0.14))

  // No borders on a theme that separates by tone; others keep a faint edge.
  readonly property color edge: graphite ? "transparent" : Util.alpha(_ink, 0.14)
  readonly property int edgeWidth: graphite ? 0 : 1

  // Labels in the sans, values (numbers, keys, times, names) in the mono.
  readonly property string labelFont: graphite ? "SF Pro Text" : Style.font.family
  readonly property string valueFont: graphite ? "SFMono Nerd Font Mono" : Style.font.family

  readonly property int sizeTitle: graphite ? 15 : Style.font.subtitle
  readonly property int sizeBody: graphite ? 13 : Style.font.body
  readonly property int sizeValue: graphite ? 12 : Style.font.body
  readonly property int sizeMeta: graphite ? 11 : Style.font.caption

  readonly property int radiusPanel: graphite ? 16 : Style.space(10)
  readonly property int radiusGroup: graphite ? 8 : Style.space(8)
  readonly property int radiusRow: graphite ? 6 : Style.space(6)
  readonly property int radiusField: graphite ? 5 : Style.space(5)

  readonly property int padPanel: graphite ? 12 : Style.space(8)
  readonly property int gapRow: 2

  // Motion: hover and colour changes settle in about 120 ms.
  readonly property int snap: 120
  readonly property int move: 220
}
