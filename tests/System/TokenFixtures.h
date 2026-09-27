#pragma once
// Project files for the token round trips, kept out of the test class so moc reads it.

inline const char *const w3cFixture = R"json({
  "color": {
    "$type": "color",
    "brand": { "$value": "#0a84ff", "$description": "Buttons and links" },
    "surface": {
      "$value": "#ffffff",
      "$extensions": { "io.github.iretonsean.omastrator": { "modes": { "dark": "#111111" } } }
    },
    "link": { "$value": "{color.brand}" }
  },
  "space": {
    "4": { "$type": "dimension", "$value": { "value": 16, "unit": "px" } },
    "2": { "$type": "dimension", "$value": "0.5rem" }
  },
  "radius": { "md": { "$type": "dimension", "$value": "8px" } },
  "text": {
    "body": {
      "$type": "typography",
      "$value": { "fontFamily": ["Inter", "sans-serif"], "fontSize": "16px", "fontWeight": 400, "lineHeight": 1.5, "letterSpacing": "0px" }
    }
  },
  "shadow": {
    "sm": { "$type": "shadow", "$value": { "color": "#00000040", "offsetX": "0px", "offsetY": "1px", "blur": "2px", "spread": "0px" } }
  },
  "motion": { "fast": { "$type": "duration", "$value": { "value": 100, "unit": "ms" } } }
}
)json";

inline const char *const tailwindFixture = R"css(@import "tailwindcss";

/* The brand. */
@theme {
  --font-sans: "Inter", ui-sans-serif, system-ui;
  --color-brand-500: oklch(0.623 0.214 259.815);
  --color-ink: #111827;
  --spacing: 0.25rem;
  --radius-lg: 0.5rem;
  --text-xl: 1.25rem;
  --text-xl--line-height: calc(1.75 / 1.25);
  --text-sm: 14px;
  --text-sm--line-height: 20px;
  --shadow-md: 0 4px 6px -1px rgb(0 0 0 / 0.1);
}

.card { color: var(--color-ink); }
)css";

inline const char *const cssFixture = R"css(:root {
  --brand-blue: #0a84ff;
  --space-md: 12px;
  --radius-card: 10px;
  --shadow-card: 0 2px 8px rgba(0, 0, 0, 0.2);
  --unknown-thing: bold;
}

@media (prefers-color-scheme: dark) {
  :root {
    --brand-blue: #409cff;
  }
}

body { margin: 0; }
)css";

inline const char *const tailwindConfigFixture = R"js(/** @type {import('tailwindcss').Config} */
const colors = require('tailwindcss/colors')
module.exports = {
  content: ['./src/**/*.{js,ts}'],
  theme: {
    colors: {
      transparent: 'transparent',
      gray: colors.gray,
      brand: { DEFAULT: '#0a84ff', dark: '#0060df' },
    },
    fontFamily: { sans: ['Inter', 'sans-serif'] },
    extend: {
      spacing: { '18': '4.5rem' },
      borderRadius: { xl: '1rem' },
      fontSize: { hero: ['3rem', { lineHeight: '1', fontWeight: '800' }], tiny: ['10px', '14px'] },
      boxShadow: { soft: '0 1px 3px rgba(0,0,0,0.12)' },
    },
  },
  plugins: [],
}
)js";

inline const char *const colorsTomlFixture = R"toml(# Test theme.
mode = "dark"
accent = "#0a84ff"
background = "#1a1a1c"
foreground = "#e5e5e7"
hyprland_active_border = "rgba(0a84ffb3)"

[extra]
ignored = "#ffffff"
)toml";

inline const char *const spacingTomlFixture = R"toml([spacing]
scale = 1.0
popup-padding = 16
)toml";
