# AI features: plan for after the MVP

Decided with the user on 2026-09-26. Nothing here is built yet.

## Principles

- **Everything stays editable.** AI output is always real paths, text and groups
  in the document, never a flattened image.
- **No bundled model.** The app hands work to Omarchy's default agent, the one
  the user picked in Omarchy's settings, the way omadesign does. The app itself
  holds no API keys.
- **Preview, then accept.** An AI edit shows as a ghost preview over the canvas.
  Enter accepts it as one named undo step (for example "AI: Recolor"), and Esc
  discards it.

## Surfaces

1. **Contextual actions.** AI entries in the menus and the Properties panel,
   next to the manual command they extend: Generate…, Vectorize (smart trace),
   Edit Selection with Instruction…
2. **MCP server.** The app exposes the open document to the agent, and to any
   external agent such as Claude Code:
   - *Read*: layers and objects as JSON (`DocumentCodec`), plus a rendered PNG
     (`VectorRenderer::render`) so the agent can see the artboard.
   - *Edit*: `EditorSession`'s operations as tools: add path/text, transform,
     style, group, arrange, align, pathfinder, clipping masks.
   - *Insert SVG*: the agent writes SVG and the app imports it with
     `SvgImporter` as editable paths. This is the simplest route to generation.
   - *Files*: open, save, and export to PNG, SVG and PDF.

   Edits that arrive over MCP use the same preview-then-accept flow.

## Features, in order

1. **Generate artwork.** A prompt with a variation count that the user chooses.
   The variations appear side by side on the pasteboard. After the user picks
   one, iteration continues on it ("rounder", "fewer colours", "thicker
   strokes"), and each round is previewed and accepted like any other AI edit.
2. **Edit by instruction.** For example "recolor to this palette", "make these
   icons consistent" or "snap to an 8 pt grid", carried out through the MCP edit
   tools and scoped to the selection when one exists.
3. **Smarter tracing**, for two cases:
   - *Logos and icons*: a few flat colours, with near-circles snapped to true
     ellipses, straight runs to lines, and symmetric halves mirrored.
   - *Sketches and line art*: centreline tracing into stroked paths rather than
     filled outlines.

   Both start from the classic tracer (`ImageTrace`, ported from omadesign),
   and the agent then cleans up the result.

## Open questions

- How to find Omarchy's default agent and launch it (omadesign's `agent.rs`
  shows one way).
- The MCP transport: stdio launched by the agent, or a local socket the running
  app listens on.

## Roast My Design

A toolbar button that sends the artboard or the selection to the agent. The
agent roasts it first (savage, about the design only), then gives sincere,
specific feedback, then offers one click to generate variations from that
feedback. Full spec in docs/HUMOR.md.
