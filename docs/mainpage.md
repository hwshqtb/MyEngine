# hwshqtb::metaobject

A C++26 reflection-driven clone of the Qt Core **meta-object** system.

The library replaces moc's code generation with `consteval` reflection: every
class that expands `OBJECT` or `GADGET` describes itself at compile time, and
everything a meta-object system does — walking the class chain, resolving an
overload by name and argument types, invoking a method, emitting a signal,
reading and writing a property, enumerating methods and constructors — reads
records that were built from `std::meta` reflections of the declarations.

This page is the entry point for the reference documentation; the four
hand-written companions are:

- \subpage model — the data model as it currently stands (tables, row types,
  the invocation surface, the boundary decisions);
- \subpage conventions — the project's naming and API conventions.
- \subpage events — how events are queued, merged and dispatched per phase, how
  one input step reaches a window, and how a frame is requested and painted.
- \subpage ui — the control layer: the window's three roles, the item tree, the
  composition host and the immediate host, focus and pointer state, layout and
  size constraints, the controls, the style, and the dirty-region rendering
  model.

## Layers

| Layer | What it is |
|---|---|
| `meta_info_t` | The compile-time table of ONE class: its name, methods, constructors, properties, enumerations, class infos and interfaces, plus the query and direct-invocation primitives. |
| `meta_object_t` | The chain: links a table to its parent's, resolves names own-first, and owns the diagnostics. |
| `object_t` | The instance half: signals and slots, thread affinity, event filtering, parent/child tree. |
| `meta_method_t` / `meta_constructor_t` | Handles onto one table row: connection type, `any_t` return slot, misuse checks. |
| `any_t` / `meta_type_t` | The value and type abstractions the erased channels traffic in. |
| `property_t`, `event_t`, `event_loop_t`, `thread_t` | The Qt-Core facilities built on top. |
| `gui` | The QtGui-equivalent layer: geometry, colour and fonts, painting (`painter_t`, `paint_engine_t`) with its persistent surface (`backing_store_t`), images, and the top-level window in its THREE roles — `window_t` (the frame policy and the paint device), `platform_window_t` (the platform half: native window, renderer, engine, event source) and its implementations. There are two: `detail::sdl_platform_window_t`, the one NATIVE implementation this platform has, and `detail::offscreen_platform_window`, which has no native window at all and is what `gui::offscreen_window_t` paints into — it runs with no video subsystem and no display, so the whole frame policy is exercised without a window system. Plus the input snapshots (`input_state_t`, `interaction_t`). |
| `ui` | The QtWidgets-equivalent layer: an `item_t` control tree (`widget_t`) with a COMPOSITION host that holds a `gui::window_t` instead of being one (`widget_window_t`), an immediate host that is itself an item of that tree (`im_host_t`), plus the layouts (`layout_t`, `box_layout_t`, `grid_layout_t`). |
| `log` | The categorised logging facade: `log_category_t`, the `HWSHQTB_LOG` macro family, `set_filter_rules` and the `HWSHQTB_LOGGING_RULES` environment variable. |

## Reading order

Start with \ref model, then the `metaobject` namespace grouping in the
"Namespaces" tab, then the individual classes. The headers are the source of
truth: every public declaration carries a comment stating its contract, and a
`@note` marks every trap or precondition the callers must honour.

The design criteria behind the gui/ui layers — why the retained host decouples
state from painting while the immediate host does both in one pass, the
window/plugin split, and the I1–I4 invariants — are recorded in \ref ui and
\ref events.

## Building the documentation

```
doxygen Doxyfile
```

The generated HTML lands in `doc/doxygen/html`; the configuration warns into
`doc/doxygen/doxygen-warnings.log`.
