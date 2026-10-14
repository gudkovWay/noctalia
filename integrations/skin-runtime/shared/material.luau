--!nonstrict
-- Stagecraft material: the single shared art direction for the q/skin-runtime
-- surfaces (dashboard, passive music scene, System summary, bar entry).
--
-- The family/density/topology settings are published by the native
-- SkinService into this plugin's settings; nothing here invents a geometry.
-- Colors are semantic host roles as strings ("surface", "surface_variant",
-- "on_surface", "on_surface_variant", "primary", "outline"): the ui
-- reconciler resolves them against the live base palette every frame, so
-- wallpaper palette changes propagate — no recipe colors, no frozen
-- fallback, no one-time getColor snapshots.
--
-- Focus, selection and playing are separate render inputs on purpose: the
-- material never infers one from another (a persistent passive scene shows
-- "playing" without ever drawing a focus edge).
--
-- Cards request the native structural frame (`material="stagecraft"`) only
-- while the validated native receipt marks Stagecraft active, so the relief
-- perimeter is never drawn against a profile the shell has not really applied.

local M = {}

M.DEFAULTS = {
  family = "stagecraft",
  density = "comfortable",
  topology = "top-music-side",
}

local function configString(key)
  local value = noctalia.getConfig(key)
  if type(value) == "string" then
    return value
  end
  return nil
end

local function oneOf(key, allowed)
  local value = configString(key)
  if value then
    for _, candidate in ipairs(allowed) do
      if candidate == value then
        return value
      end
    end
  end
  return nil
end

-- Cross-plugin receipt marker. The native SkinService publishes the applied
-- recipe's variant_id (32 lowercase hex) into plugin_settings["q/skin-runtime"]
-- in the same atomic projection as the family, and erases the key from the
-- script-visible snapshot whenever no validated profile is active. The manifest
-- default is "" and is not activity; the manifest family default is also
-- "stagecraft", so the family value alone never gates anything. This is the
-- exact gate the canonical desktop_skin module uses: structural material is
-- requested only while the real receipt is present.
local VARIANT_HEX = "^%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x$"

local function pluginEnabled(id)
  local ok, enabled = pcall(noctalia.getSetting, "plugins.enabled")
  if not ok or type(enabled) ~= "table" then
    return false
  end
  for _, value in ipairs(enabled) do
    if value == id then
      return true
    end
  end
  return false
end

--- True only while the native receipt marks Stagecraft active: this plugin is
--- enabled, the projected family is stagecraft (an absent override is the
--- manifest default), and a validated applied_variant (32 lowercase hex) is
--- published. A cleared or invalid projection masks the key, so this falls back
--- to false and no structural material is requested.
function M.stagecraft()
  if noctalia == nil or noctalia.getSetting == nil then
    return false
  end
  if not pluginEnabled("q/skin-runtime") then
    return false
  end
  local ok, pluginSettings = pcall(noctalia.getSetting, "plugin_settings")
  if not ok or type(pluginSettings) ~= "table" then
    return false
  end
  local settings = pluginSettings["q/skin-runtime"]
  if type(settings) ~= "table" then
    return false
  end
  local family = settings.family
  if family ~= nil and family ~= "stagecraft" then
    return false
  end
  return type(settings.applied_variant) == "string" and settings.applied_variant:match(VARIANT_HEX) ~= nil
end

--- Tokens for the current family/density/topology. Read on every call: panels
--- and widgets are rebuilt when a setting changes, so a cached token table
--- would outlive the recipe it came from. `active` is the native receipt gate
--- sampled once per draw; every card built from these tokens shares it.
---
--- Kumihimo is recognized here only so the family/topology settings resolve to
--- their real values and no Stagecraft flag is set: its bar tags render from
--- the shared Kumihimo recipe tokens in `desktop_skin.luau`, never from this
--- semantic-role table, and the unadapted dashboard-style cards stay plain
--- ordinary surfaces instead of borrowing Stagecraft structural material.
function M.tokens()
  local family =
    oneOf("family", { "stagecraft", "cut-stone", "wayfinding-field", "adaptive-workbench", "kumihimo" })
    or M.DEFAULTS.family
  local density = oneOf("density", { "comfortable", "compact" }) or M.DEFAULTS.density
  local topology =
    oneOf("topology", { "top-music-side", "top-music-bottom", "desktop-widget-side", "compact-rail", "hanging-tags" })
    or M.DEFAULTS.topology
  local compact = density == "compact"

  return {
    family = family,
    density = density,
    topology = topology,
    -- Stagecraft covers the first three topologies only; compact-rail is the
    -- shape of the other direction families and is not claimed as available.
    stagecraft = family == "stagecraft" and topology ~= "compact-rail",
    -- The wallpaper-driven hanging-tags family: recognized, but never given
    -- Stagecraft structural material.
    kumihimo = family == "kumihimo",
    -- Native receipt gate, sampled once and shared by every card in this draw.
    active = M.stagecraft(),
    top = topology == "top-music-side" or topology == "top-music-bottom",
    desktop = topology == "desktop-widget-side",
    rail = topology == "compact-rail",
    compact = compact,
    gap = compact and 6 or 10,
    -- Framed nodes keep content clear of the structural band (which reaches up
    -- to 14 logical px into the perimeter), so the card padding floor is 16.
    pad = 16,
    radius = 12,
    -- Chamfer cut size of the family silhouette (frozen r05/r06 geometry).
    chamfer = 14,
    ring = 2,
    titleSize = compact and 15 or 18,
    bodySize = compact and 12 or 13,
    metaSize = 11,
    -- Dashboard music hero (r05): dominant artwork column and display type.
    artSize = 260,
    -- Bounded width of the r05 identity column. A growing (unbounded) flex
    -- column would shove the fixed artwork to the far right of the hero; the
    -- columns are width-bounded and spaced instead.
    heroTextWidth = compact and 300 or 360,
    heroTitleSize = compact and 24 or 36,
    heroArtistSize = compact and 16 or 22,
    captionSize = 11,
    -- Transport: central play is the dominant round control, the rest sit
    -- slightly smaller; every one stays round via equal width/height.
    playSize = 64,
    transportSize = 48,
    transportSmallSize = 44,
    colors = {
      -- Semantic host roles resolved per frame by the ui reconciler.
      surface = "surface",
      surfaceVariant = "surface_variant",
      text = "on_surface",
      muted = "on_surface_variant",
      accent = "primary",
      outline = "outline",
    },
  }
end

--- True when the current recipe is the one this plugin implements. Unknown
--- families stay visible as Unknown instead of silently rendering Stagecraft.
function M.familySupported(t, supportedFamilies)
  if t.family == "stagecraft" then
    return true
  end
  if type(supportedFamilies) ~= "table" then
    return false
  end
  for _, family in ipairs(supportedFamilies) do
    if family == t.family then
      return true
    end
  end
  return false
end

--- The Stagecraft card. While the native receipt is active it renders the
--- structural material: one native relief perimeter (`material="stagecraft"`),
--- the family chamfer silhouette (`shape="chamfer"`, `radius` = cut size) and
--- the 16px framed padding floor. `border` is the band's own frame color/alpha
--- role — role outline normally, role primary only when the node actually owns
--- keyboard focus (`focused` is an explicit argument): focus swaps the one
--- frame color, it never adds a second thin outline, and the native band
--- ignores a separate borderWidth hairline.
---
--- Without a validated receipt the same column is an ordinary rounded card on
--- the semantic roles (no `material` key, which resets the native material to
--- None). The mode-distinct key suffix makes the reconciler build a fresh node
--- on a mode change instead of retaining structural fill/shape/border on a
--- reused one. `align` lays out the card's children either way.
function M.card(t, opts, children)
  opts = opts or {}
  local active = opts.active
  if active == nil then
    active = t.active == true
  end
  local key = opts.key
  if type(key) == "string" and key ~= "" then
    key = key .. (active and "-sc" or "-plain")
  end

  if active then
    return ui.column({
      key = key,
      gap = opts.gap or t.gap,
      padding = opts.padding or t.pad,
      fill = opts.fill or t.colors.surface,
      material = "stagecraft",
      shape = opts.shape or "chamfer",
      radius = opts.radius or t.chamfer,
      align = opts.align,
      border = opts.border or (opts.focused and t.colors.accent or t.colors.outline),
      flexGrow = opts.flexGrow,
      width = opts.width,
      height = opts.height,
      minWidth = opts.minWidth,
      tooltip = opts.tooltip,
    }, children)
  end

  return ui.column({
    key = key,
    gap = opts.gap or t.gap,
    padding = opts.padding or t.pad,
    fill = opts.fill or t.colors.surface,
    shape = opts.shape,
    radius = opts.radius or t.radius,
    align = opts.align,
    border = opts.border or (opts.focused and t.colors.accent or t.colors.outline),
    borderWidth = opts.borderWidth or (opts.focused and t.ring or 1),
    flexGrow = opts.flexGrow,
    width = opts.width,
    height = opts.height,
    minWidth = opts.minWidth,
    tooltip = opts.tooltip,
  }, children)
end

--- A keyed zero-size node for a conditional child. The host reads a children
--- array with `#`, which truncates at a nil hole, and a `ui.box` cannot host
--- children, so a placeholder node keeps the slot honest.
function M.empty(key)
  return ui.box({ key = key, width = 0, height = 0 })
end

--- Section header: glyph + title + right-hand meta. A shortcut is never drawn
--- here; callers pass a meta string only from the frozen action map.
function M.header(t, opts)
  opts = opts or {}
  local children = {
    ui.glyph({
      key = "glyph",
      name = opts.glyph or "square",
      size = t.bodySize + 2,
      color = opts.focused and t.colors.accent or t.colors.muted,
    }),
    ui.label({
      key = "title",
      text = opts.title or "",
      fontSize = t.titleSize,
      fontWeight = "bold",
      color = t.colors.text,
      flexGrow = 1,
      maxLines = 1,
    }),
  }
  if opts.active then
    -- Domain activity (playing / running) is not focus and not selection.
    table.insert(children, ui.box({
      key = "active-dot",
      width = 7,
      height = 7,
      radius = 4,
      fill = t.colors.accent,
    }))
  end
  if opts.meta then
    table.insert(children, ui.label({
      key = "meta",
      text = opts.meta,
      fontSize = t.metaSize,
      color = opts.metaColor or t.colors.muted,
      maxLines = 1,
    }))
  end
  return ui.row({ key = opts.key or "head", gap = t.gap, align = "center" }, children)
end

--- A selectable control. `selected` is the cursor and nothing else. Every
--- button carries an explicit palette built from the semantic roles above,
--- so no unrelated host variant can bleed in:
--- hover/press/selected all stay inside the surface/variant/primary roles, and the
--- keyboard cursor draws the accent independently of pressed/active state.
---
--- Sizing opts: `width`/`height` make round controls (equal values derive
--- radius = size/2 unless `radius` overrides); `glyphSize`/`fontSize` set the
--- glyph/text scale; `emphasis` fills the accent (the central play control).
--- Explicit `fill`/`color`/`border` win over the defaults. Disabled controls
--- stay visible (Unknown beats a hidden row) but cannot be activated.
function M.chip(t, opts)
  opts = opts or {}
  local selected = opts.selected == true
  local emphasis = opts.emphasis == true
  local enabled = opts.enabled ~= false

  local width = opts.width
  local height = opts.height
  local radius = opts.radius
  if radius == nil and width ~= nil and height ~= nil and width == height then
    radius = width / 2
  end

  return ui.button({
    key = opts.key,
    text = opts.text,
    glyph = opts.glyph,
    glyphSize = opts.glyphSize or t.bodySize + 2,
    fontSize = opts.fontSize or t.bodySize,
    width = width,
    height = height,
    radius = radius,
    enabled = enabled,
    selected = selected,
    tooltip = opts.tooltip,
    onClick = opts.onClick,
    -- Explicit local ButtonPalette, every state accounted for.
    fill = opts.fill or (emphasis and t.colors.accent or (enabled and t.colors.surfaceVariant or t.colors.surface)),
    color = opts.color or (emphasis and t.colors.surface or (enabled and t.colors.text or t.colors.muted)),
    border = selected and t.colors.accent or opts.border or t.colors.outline,
    borderWidth = 1,
    hoverFill = emphasis and t.colors.accent or t.colors.surfaceVariant,
    hoverColor = opts.color or (emphasis and t.colors.surface or t.colors.text),
    pressedFill = emphasis and t.colors.accent or t.colors.outline,
    pressedColor = opts.color or (emphasis and t.colors.surface or t.colors.text),
    selectedFill = emphasis and t.colors.accent or t.colors.surfaceVariant,
    selectedColor = opts.color or (emphasis and t.colors.surface or t.colors.text),
  })
end

--- One truthful metric row. `fraction` nil renders an Unknown row: a missing
--- sample is never drawn as zero.
function M.meter(t, opts)
  opts = opts or {}
  local known = type(opts.fraction) == "number"
  local controls = {
    ui.label({
      key = "label",
      text = opts.label or "",
      fontSize = t.bodySize,
      color = t.colors.muted,
      width = opts.labelWidth or 74,
      maxLines = 1,
    }),
    ui.progress({
      key = "bar",
      flexGrow = 1,
      height = t.compact and 6 or 8,
      radius = 4,
      progress = known and opts.fraction or 0,
      fill = known and (opts.color or t.colors.accent) or t.colors.outline,
      track = t.colors.surfaceVariant,
    }),
    ui.label({
      key = "value",
      text = opts.text or "Unknown",
      fontSize = t.bodySize,
      color = known and t.colors.text or t.colors.muted,
      width = opts.valueWidth or 96,
      textAlign = "end",
      maxLines = 1,
    }),
  }
  return ui.row({
    key = opts.key,
    gap = t.gap,
    padding = 4,
    align = "center",
    radius = 8,
    border = opts.selected and t.colors.accent or nil,
    borderWidth = opts.selected and 1 or nil,
    fill = opts.selected and t.colors.surfaceVariant or nil,
    onClick = opts.onClick,
    tooltip = opts.tooltip,
  }, controls)
end

--- Recipe-colored rule, horizontal by default; `vertical` gives a column
--- separator. This is the only divider the family surfaces use, so hairlines
--- keep the outline color instead of a host role.
function M.divider(t, key, vertical)
  return ui.separator({
    key = key,
    orientation = vertical and "vertical" or "horizontal",
    color = t.colors.outline,
    thickness = 1,
  })
end

--- An explicit Unknown line. Used whenever a source is missing or disabled —
--- never replaced by a zero or a demo value.
function M.unknown(t, key, text)
  return ui.row({ key = key, gap = t.gap, align = "center" }, {
    ui.glyph({ key = "glyph", name = "help-circle", size = t.bodySize + 2, color = t.colors.muted }),
    ui.label({ key = "text", text = text or "Unknown", fontSize = t.bodySize, color = t.colors.muted }),
  })
end

--- Footer hint block. Only a focused browse owner may render one, and it must
--- name the owner and the state.
function M.footer(t, key, text)
  return ui.row({ key = key, gap = t.gap, align = "center" }, {
    ui.glyph({ key = "glyph", name = "keyboard", size = t.metaSize + 2, color = t.colors.muted }),
    ui.label({ key = "text", text = text, fontSize = t.metaSize, color = t.colors.muted, maxLines = 2 }),
  })
end

return M
