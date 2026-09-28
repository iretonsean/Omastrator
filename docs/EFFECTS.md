# Effects: shadows and blurs (design, 2026-09-28)

**Status: a design for the author to review. Nothing is built.** Effects are
next in the Figma build order (docs/FIGMA-AUDIT.md: "Effects | missing"),
after frames, auto layout and constraints. This page says what to build and
where it goes, so a Sonnet agent can build it in phases.

## What the designer gets

Figma's four effects, with Figma's defaults and behaviour:

| Effect | Settings | Draws |
|---|---|---|
| Drop shadow | colour (default black at 25 %), X 0, Y 4, blur 4, spread 0, blend, "Show behind transparent areas" | under the object, from its alpha |
| Inner shadow | colour, X, Y, blur, spread, blend | inside the object, over its fills |
| Layer blur | blur | the whole object, blurred |
| Background blur | blur | what's behind the object, blurred inside its shape |

- **A stack.** An object can have any number of effects. Each has its own eye,
  and the stack can be reordered, like the fill and stroke stacks.
- **Any object.** Effects work on paths, text, images, groups, frames and
  component instances, the same as opacity. On a group or frame, the effect
  follows the combined alpha of its contents, as in Figma.
- **Blur values mean what they mean in CSS and Figma:** the Gaussian's σ is
  blur ÷ 2. That makes Lift, tokens and CSS export line up exactly.

## Model (`src/Document`)

```cpp
enum class EffectKind { dropShadow, innerShadow, layerBlur, backgroundBlur };

struct Effect {
    EffectKind kind = EffectKind::dropShadow;
    bool isHidden = false;
    QColor color = QColor(0, 0, 0, 64);   // shadows only
    QPointF offset {0, 4};                // shadows only
    double blur = 4;
    double spread = 0;                    // shadows only
    LayerBlendMode blendMode = LayerBlendMode::normal;
    bool showBehindTransparent = false;   // drop shadow only
    bool operator==(const Effect &) const = default;  // history compares documents
};
```

- **Where effects live:** `std::vector<Effect> effects` on `VectorObject`, next
  to `opacity` and `blendMode` (VectorDocument.h:345). It isn't gated on
  `hasPaint()`, so every kind can hold effects.
- **Helpers:**
  - `hasSimpleAppearance()` returns false when a visible effect exists.
  - `copyAppearance()` copies effects, so Paste Properties carries them.
  - The Eyedropper's Alt-click copies them too.
- **Scale Strokes & Effects:** when the toggle is on, `VectorDocument::transform`
  (VectorDocument.cpp:486) scales offset, blur and spread. Today the toggle
  only scales strokes.
- **Components:**
  - Effects on an instance's children are overridable. Add
    `std::optional<std::vector<Effect>> effects` to `InstanceOverride` and
    `InstanceMade` (Components.h:27-43), with the detect, apply and encode
    steps beside the fill ones. Without this, `Components::sync` would wipe an
    instance's effect edits on the next edit.
  - Effects on the instance group itself already survive.

### Bounds

- **A visual bounds mode.** `VectorDocument::bounds` gains a mode that
  includes each effect's extent:
  - a drop shadow adds the offset plus spread plus 1.5 × blur;
  - a layer blur adds 1.5 × blur.
- **Callers that need it** (they already use `includeStroke = true`):
  - Export for Screens crops (`croppedTo`, `ScreenExport`);
  - Fit to Artwork Bounds;
  - which objects belong to an artboard;
  - the mask raster area;
  - Agent renders of a selection;
  - the overlay layers.

  Without it, exported assets would cut their shadows off.
- **What stays geometric:** selection handles, smart guides and hit tests,
  as in Figma.

## File format (`DocumentCodec`)

- **Encoding:** an additive `"effects"` array, written only when it isn't
  empty, with no version bump (the same pattern as `moreFills` and
  `textWrap`). Older builds drop the key without complaint.
  ```json
  "effects": [{"type": "dropShadow", "color": "#00000040", "x": 0, "y": 4,
               "blur": 4, "spread": 0, "blend": "normal", "behind": false},
              {"type": "layerBlur", "blur": 8, "hidden": true}]
  ```
- **What gets it for free:** `.omai` files, the clipboard and the agent's
  `document_get`, `update_object` and `replace_objects`, since they all share
  this codec.

## Rendering (`src/Rendering`)

- **Where it plugs in.** `VectorRenderer::drawObject`
  (VectorRenderer.cpp:285), after the isolation check, runs this when an
  object has visible effects:
  1. **Alpha:** render the object alone into a `QImage` with
     `renderIsolated` (the opacity-mask path, :152), over its visual bounds at
     `maskRasterScale`.
  2. **Background blur**, when the target is a raster image: copy the
     backdrop under the object's bounds, blur it, clip it to the object's
     alpha and draw it.
  3. **Drop shadows**, in stack order:
     - tint the alpha;
     - spread it (dilate for a positive spread, erode for a negative one);
     - blur and offset it, then draw it with its blend mode;
     - unless "Show behind transparent areas" is on, knock out the object's
       own alpha, as Figma does.
  4. **The object itself**, drawn as vectors as usual, or the blurred
     isolated image when there's a layer blur.
  5. **Inner shadows:** take the inverted alpha, offset, erode, blur and
     tint it, clip it to the object's alpha and draw it on top.
- **The object stays vector.** Only shadows and blurs are raster. In PDF
  export, a shadowed button keeps its text and paths as vectors, and only the
  shadow is an image at the mask scale (at least 3×). Only a layer blur makes
  the object itself a raster image.
- **The blur:** Qt has no public blur, so write `src/Rendering/Blur.{h,cpp}`
  next to `HslBlend`. Use three separable box passes to approximate the
  Gaussian, premultiplied ARGB32, and run rows in parallel through
  `Qt6::Concurrent`, which oma_core already links.
- **Cache:** `EffectCache`, an LRU of rendered effect images keyed by the
  object's value and the render scale.
  - Its size is capped in MB, about 64 MB by default.
  - A pure move reuses the cached image at the new place: the key leaves out
    translation, and the cache stores the offset. Without this, every mouse
    move in a drag re-blurs every shadow on the page. The canvas redraws the
    whole document on every repaint (EditorCanvas.cpp:224).
- **The canvas back buffer (phase 4).** Background blur needs to read what's
  already drawn. So do blend modes, which, from reading the code, don't show
  on the canvas today: `composite()` only applies them when the device is a
  `QImage`, and the canvas paints straight onto the widget. Paint the canvas
  into a device-pixel-ratio `QImage` and blit it.
  - Measure it on the author's machine before and after.
  - If the cost is visible, keep direct painting and use the back buffer only
    when the document has a background blur or a non-normal blend mode.
  - Check the blend-mode gap in the running app first: if it's real, this
    phase fixes it too.

## Properties panel (`src/UI`)

- **The Effects section** sits after Stroke (PropertiesPanel.cpp:54). It
  follows docs/PANELS.md: 24 px controls, fields in pairs, a one-line summary
  when folded ("Drop shadow, Layer blur").
  - **With no effects:** only the heading and a "+" show.
  - **"+" adds a drop shadow**, as in Figma.
  - **Each row** has a grip, an eye, a kind menu (Drop shadow, Inner shadow,
    Layer blur, Background blur), a settings button with a short summary
    ("0, 4 · 4") and "−".
  - **The settings popover** holds X and Y, Blur and Spread (both pairs),
    colour well with hex and opacity, Blend, and "Show behind transparent
    areas".
- **Code:** `EffectStack` copies `PaintStack`'s structure (the rows, the
  grip reorder, stable object names, and rows rebuilt only when the count
  changes, so a scrub keeps its field). It edits through
  `EditorSession::setEffectsOfSelection`.
- **Undo names:** Add Drop Shadow, Hide Effect, Remove Effect, Reorder
  Effects and Effect Blur. Panel scrubs use the `NumberField::gesture` bracket,
  so a drag is one step.
- **Commands:** Ctrl+K has Add Drop Shadow, Add Inner Shadow, Add Layer Blur,
  Add Background Blur and Remove Effects. The task bar's ⋯ menu gets the same
  commands under Effects.

## Session API

`setEffectsOfSelection(std::vector<Effect>, QString editName)` acts on
`m_selection`, like `setOpacityOfSelection` (EditorSession+Objects.cpp:1064),
so groups and frames get effects. With nothing selected it sets nothing.
New objects start with no effects.

## Design tokens

- **Shadow tokens become usable.** A shadow token (`ShadowValue`,
  DesignTokens.h:35) maps directly to a drop shadow, or to an inner shadow
  when `inset` is set.
- **Applying one** replaces the object's first shadow and binds it through a
  new `TokenRef` key, `"shadow"`. Changing the token updates every use in one
  step, as colour tokens do today.
  - This replaces the "objects here have no shadow yet" message
    (EditorSession+System.cpp:165).
- `ShadowValue::fromCss` keeping only the first shadow can stay for now.

## Export and import

- **SVG export:**
  - one `<filter>` per object, in the inline `<defs>` that `writeFrame` and
    `writeContainer` already write, with a region grown by the visual extent;
  - drop shadow: `feGaussianBlur`, `feOffset`, `feFlood`, `feComposite` and
    `feMerge` (`feMorphology` for spread);
  - inner shadow: the inverted-alpha composite;
  - layer blur: `feGaussianBlur` on `SourceGraphic`;
  - background blur has no SVG equivalent: leave it out, with one export
    warning.
- **SVG import** reads `feDropShadow` and the common
  blur/offset/flood/composite drop-shadow shape into effects. Other filters
  keep today's "Filters were left out." warning (SvgImporter.cpp:268).
- **PNG, JPEG and PDF** go through the renderer, so they get effects
  automatically.
- **Lift** turns CSS `box-shadow` into real drop and inner shadows, and
  `filter: blur()` and `backdrop-filter: blur()` into blurs. Today it makes
  sharp "Shadow" rectangles or drops them (Lift.cpp:455-476).
- **Imports:** once the import branches are merged, the Figma, Sketch and
  Penpot mappers change their "shadows were left out" warnings into real
  effects. They're small follow-ups in each mapper.

## Agent

- `set_style` gains an `effects` array in the codec's shape (AgentProtocol.cpp:33).
- The codec key already reaches `document_get` and `update_object`.
- The agent's docs get one example: "add a soft shadow to the card".

## Build phases (one branch, `feat/effects`, off `main` after the imports merge)

1. **Model:** the model, codec, session setter, visual bounds, Scale Strokes &
   Effects and component overrides, with tests. Nothing visible yet.
2. **Rendering:** the renderer, the blur and the cache. Drop shadow, inner
   shadow and layer blur on the canvas, and in PNG and PDF exports. Test pixel
   extents against known σ, check that a PDF shadow is an image while the
   object stays vector, and check that a moved object reuses its cached
   shadow.
3. **Panel:** the Effects section, Ctrl+K and the task bar commands, and
   Paste Properties and the Eyedropper.
4. **Canvas:** the back buffer (measured), background blur, and the check and
   fix for blend modes on the canvas.
5. **Surroundings:** SVG export and import, Lift's box-shadow, shadow tokens
   and the agent's `set_style`.
6. **Import follow-ups:** after the import branches are merged.

The tests go in `PaintAppearanceTests` (model and codec),
`DocumentExporterTests` and `ScreenExportTests` (render extents and crops), a
new `EffectStackTests` (panel), `SvgAppearanceTests` and `SvgRoundTripTests`,
`DesignSystemTests` (tokens and overrides) and `AgentToolsTests`.

## Decisions made here (change any you disagree with)

- **Figma's semantics, not CSS's.** A shadow isn't drawn behind a transparent
  fill unless "Show behind transparent areas" is on. Blur is CSS-compatible
  (σ = blur ÷ 2).
- **The object stays vector** in PDF and SVG. Only the effect is raster in
  PDF, and in SVG it's a filter.
- **Selection handles ignore effects;** export and fit bounds include them.
- **Background blur waits for phase 4**, because it needs the canvas back
  buffer.
- **Effect styles as their own library objects** (Figma's "effect styles")
  aren't part of this. Shadow tokens cover the shared case for now.
