# W1/W2: the ui layer — widget_t geometry/tree/interaction/render, the painter prerequisites it
# rests on (translate feature bit, clip stack, save/restore), the reflect-only properties, and the
# box/grid layouts with size hints and policies.
#
# Everything runs offscreen through image_t plus a recording paint engine, exactly like the gui tests,
# so this needs no window, no display and no video subsystem. examples/ui_demo covers the real
# window host (widget_window_t).
add_executable(ui_test ui_test.cpp)
target_link_libraries(ui_test PRIVATE ui)
add_test(NAME ui_test COMMAND ui_test)

# W2: the layout algorithms on their own (box, grid, size hints/policies/stretch, nesting, and the
# re-layout triggers: set_geometry, child_added, child_removed). Same offscreen/no-window rule.
add_executable(ui_layout_test layout_test.cpp)
target_link_libraries(ui_layout_test PRIVATE ui)
add_test(NAME ui_layout_test COMMAND ui_layout_test)

# I1: ui::context_t, the immediate host — the object_name cache, the click state machine through the
# User property, the claiming rule (previous-frame rectangles, topmost wins), collect(), and the
# switchability proof (one control implementation, two hosts). Offscreen, like the other ui tests.
add_executable(ui_context_test context_test.cpp)
target_link_libraries(ui_context_test PRIVATE ui)
add_test(NAME ui_context_test COMMAND ui_context_test)

# W3: ui::input_router_t, the retained host's event routing — hit testing and bubbling, the implicit
# grab, keyboard focus and the Tab chain, key delivery, and the consumption rule that arbitrates with
# the immediate host. The router is a plain class, so this is headless like the other ui tests; the
# window adapter (widget_window_t's native event filter) is exercised by examples/ui_demo.
add_executable(ui_router_test input_router_test.cpp)
target_link_libraries(ui_router_test PRIVATE ui)
add_test(NAME ui_router_test COMMAND ui_router_test)

# M0/M1: the style subset, the keyboard convention, and the first three controls (label_t,
# check_box_t, progress_bar_t) — including the external-value driving model and the immediate host's
# three answer shapes (defined User value / valid void / invalid failure). Offscreen, like the rest.
add_executable(ui_controls_test controls_test.cpp)
target_link_libraries(ui_controls_test PRIVATE ui)
add_test(NAME ui_controls_test COMMAND ui_controls_test)

# M3a: the wheel in interaction_t, scroll_bar_t and scroll_view_t — the wheel snapshot under both
# hosts, the bar's drag/page/keyboard machine, the area's viewport/clamping/policies and the clipped,
# translated content paint. Offscreen: every pixel assertion goes through image_t.
add_executable(ui_scroll_test scroll_test.cpp)
target_link_libraries(ui_scroll_test PRIVATE ui)
add_test(NAME ui_scroll_test COMMAND ui_scroll_test)

# M3b: the unified setter/User-event rule (signals are state notifications, the User property is the
# frame's user-action result), the shared orientation_enum, and the two containers — group_box_t
# (content-area layout + title) and tab_widget_t (tab bar hit test, page ownership/visibility).
add_executable(ui_containers_test containers_test.cpp)
target_link_libraries(ui_containers_test PRIVATE ui)
add_test(NAME ui_containers_test COMMAND ui_containers_test)

# M4: the text-input channel (input_state_t -> interaction_t -> both hosts) and line_edit_t — typing,
# selection, caret movement, the deletion boundaries, password mode, read-only and the User property.
# Offscreen like the other ui tests.
add_executable(ui_line_edit_test line_edit_test.cpp)
target_link_libraries(ui_line_edit_test PRIVATE ui)
add_test(NAME ui_line_edit_test COMMAND ui_line_edit_test)

# W4 (repaint groundwork): the damaged-region bookkeeping — update(rect) mapped to window coordinates,
# the record on every item ancestor, the union across calls, clear_damage(), the paint path consuming
# a painted item's record, and the window's pending_damage() mirror of the root's damage. The last
# section needs a real window, so it runs under SDL's dummy video driver (still offscreen).
add_executable(ui_damage_test damage_test.cpp)
target_link_libraries(ui_damage_test PRIVATE ui)
add_test(NAME ui_damage_test COMMAND ui_damage_test)

# Size constraints: the hard minimum/maximum bounds of item_t (whole-extent and per-axis setters,
# the minimum <= maximum invariant, negative components clamped to 0), fixed size as "minimum ==
# maximum" instead of separate state, and the effective_size_hint() / effective_minimum_size() /
# effective_maximum_size() triple a layout asks. The last section pins the regression that matters
# here: with no maximum installed the triple equals the previous formula. Offscreen, no window.
add_executable(ui_size_constraint_test size_constraint_test.cpp)
target_link_libraries(ui_size_constraint_test PRIVATE ui)
add_test(NAME ui_size_constraint_test COMMAND ui_size_constraint_test)

# Layout constraints (step 2): the box/grid algorithms use effective_minimum_size() as the
# compression floor and effective_maximum_size() as the growth cap. Sections (2b)/(3b) repeat the
# floor rule on grid columns; the regression section pins the old pixel-exact placement while no
# hard constraint is installed. Offscreen, like the other ui tests.
add_executable(ui_layout_constraint_test layout_constraint_test.cpp)
target_link_libraries(ui_layout_constraint_test PRIVATE ui)
add_test(NAME ui_layout_constraint_test COMMAND ui_layout_constraint_test)

# Space shortage: the Qt qGeomCalc overfull rule ("take from the biggest first", the axis still fits
# the parent) and the per-policy compression floor of effective_minimum_size() (FIXED does not shrink
# below its hint, PREFERRED/EXPANDING shrink to their minimum hint). Sections (4)/(5) pin the
# interaction of the two rules and that a maximum stays an upper bound on the overfull path; section
# (6) is the pixel-exact regression for unconstrained, spacious layouts. Offscreen, like the rest.
add_executable(ui_overfull_test overfull_test.cpp)
target_link_libraries(ui_overfull_test PRIVATE ui)
add_test(NAME ui_overfull_test COMMAND ui_overfull_test)

# Container aggregation: layout_t::minimum_size() (the base default = size_hint(), the box/grid
# overrides composing effective_minimum_size() exactly like size_hint() composes the effective hint),
# widget_t::minimum_size_hint() reading the installed layout's floor, and the three containers
# (group_box_t / tab_widget_t / scroll_view_t) aggregating their children through the effective
# triple — plus the regression that no hard constraint anywhere leaves the composed numbers where
# they were. Offscreen, no window.
add_executable(ui_container_aggregate_test container_aggregate_test.cpp)
target_link_libraries(ui_container_aggregate_test PRIVATE ui)
add_test(NAME ui_container_aggregate_test COMMAND ui_container_aggregate_test)

# Pointer state on the item: hover / press / capture as item state (the one input session writes it,
# every view reads it), the router that drives it (implicit grab, hover migration, disabled/hidden
# refusal), the immediate host's mirror into its controls, the paint path reading that state, and the
# window adapter clearing it on mouse-leave / focus-loss. The last section needs a real window, so it
# runs under SDL's dummy video driver (still offscreen); the rest is headless.
add_executable(ui_item_input_state_test item_input_state_test.cpp)
target_link_libraries(ui_item_input_state_test PRIVATE ui)
add_test(NAME ui_item_input_state_test COMMAND ui_item_input_state_test)

# An item tree, many outputs. The tree paints itself and a frame owns the pass count, so an offscreen
# frame of a window's root paints the same pixels as the window frame; two outputs of one root paint
# identical pixels; input is still one session (one router step changes the item state once); a frame
# paints per damaged rectangle, not its bounds, and one pass over the whole region on a backend that
# can clip to one; an empty damage paints nothing and painting consumes the tree record;
# paint_event_t::damage() is the item-local area, with opaque occlusion; the window installs the
# repaint and scroll sinks itself; and its facade is unchanged. Sections 1 and 6-9 need a real window,
# so they run under SDL's dummy video driver (still offscreen).
add_executable(ui_view_test view_test.cpp)
target_link_libraries(ui_view_test PRIVATE ui)
add_test(NAME ui_view_test COMMAND ui_view_test)

# M5: the list item view — item_model_t, list_view_t's virtualised row painting, the fixed row
# height and scroll range, keyboard/wheel/mouse input, SINGLE/MULTI selection, the model
# notifications mapped to per-row damage, and the synchronised internal scroll bar. Offscreen: every
# paint assertion goes through image_t.
add_executable(ui_list_view_test list_view_test.cpp)
target_link_libraries(ui_list_view_test PRIVATE ui)
add_test(NAME ui_list_view_test COMMAND ui_list_view_test)

# The model's COLUMNS: the single-column defaults a list model relies on (column_count() == 1, the
# row form -> cell form forward for column 0, the empty header), a three-column model with its
# per-cell and per-column header answers, and the proof that ONE list_view_t shows column 0 of a
# list-only model, of a cell-only model and of a three-column model without a line of change.
# Offscreen, like the other ui tests.
add_executable(ui_item_model_test item_model_test.cpp)
target_link_libraries(ui_item_model_test PRIVATE ui)
add_test(NAME ui_item_model_test COMMAND ui_item_model_test)

# The REAL host path of the item views: SDL events pushed into SDL's queue -> window_t's native
# filter -> input_state_t -> widget_window_t's router filter -> input_router_t::dispatch -> the view,
# and the frame as window_t::draw_frame paints it (one pass per damaged rectangle). The pixels come
# from a mirror of the window's own passes. Needs a real window, so it runs under SDL's dummy video
# driver (still offscreen); the first frame, the mirrored passes and the scroll sink are the host's,
# not the view's, so this covers the composition the offscreen tests cannot. The last section repeats
# the scroll comparison on a table_view_t, whose blank band right of the content shows a frame line
# that a fast path shifting the whole viewport would drag into the rows.
add_executable(ui_real_path_test real_path_test.cpp)
target_link_libraries(ui_real_path_test PRIVATE ui)
add_test(NAME ui_real_path_test COMMAND ui_real_path_test)

# The PRESENTED frame — the pixels the user sees, as opposed to the backing surface every other test
# asserts on. SDL's D3D11 renderer (the one a real window gets on Windows) creates a two-buffer
# flip-model swap chain, so the buffer a frame renders into holds the pixels of the frame TWO presents
# ago; the dummy driver's software renderer keeps them, which is why an offscreen test cannot see
# either defect on its own. The file models the target as measured and pins (1) the scroll fast path
# presented against a full repaint of the same state and (2) a hover sweep leaving exactly one
# hovered row. Needs a real window, so it runs under SDL's dummy video driver (still offscreen).
add_executable(ui_present_test present_test.cpp)
target_link_libraries(ui_present_test PRIVATE ui)
add_test(NAME ui_present_test COMMAND ui_present_test)

# ALIGNMENT: item_t::alignment() and where a box/grid child sits inside a cell LARGER than it — the
# box cross-axis cell, both axes of a grid cell (spans included), the fill default that keeps the
# pre-alignment geometry to the pixel, the per-axis independence (a horizontal bit leaves the vertical
# fill alone), Qt's precedence for a contradictory pair (RIGHT before LEFT before HCENTER) and the
# overfull rule, which ignores the bits. The box MAIN axis is covered as a SIZE model: a bit caps that
# axis at the child's hint (Qt's qSmartMaxSize) and takes it out of the expansion distribution, so an
# EXPANDING child keeps its hint extent while the cell stays the slot (no main-axis shift). Also the
# label_t text-alignment versus layout-alignment split, pinned through the recorded glyph run.
# Offscreen, like the other ui tests.
add_executable(ui_alignment_test alignment_test.cpp)
target_link_libraries(ui_alignment_test PRIVATE ui)
add_test(NAME ui_alignment_test COMMAND ui_alignment_test)

# M5 (table): table_view_t — the fixed column widths and their content width, the header strip that
# follows the horizontal offset but not the vertical one, the virtualised cell painting (only the cells
# inside the viewport are asked of the model, in rows AND columns), the cell hit test, Left/Right
# column navigation with Return activating the cell, the inherited horizontal bar, and the size hints.
# The list regression (no header, no horizontal bar) is part of the same file. Offscreen: every paint
# assertion goes through image_t.
add_executable(ui_table_view_test table_view_test.cpp)
target_link_libraries(ui_table_view_test PRIVATE ui)
add_test(NAME ui_table_view_test COMMAND ui_table_view_test)

# M5 (tree): tree_view_t — the flattened visible list over a hierarchical model, the indentation and
# expander painting, the expander's own hit area (a click there toggles without selecting), Right/Left
# expansion and parent motion, the selection by visible row with the current NODE following a rebuild,
# the model notifications mapped onto visible rows, and the regression that a flat model serves a tree
# (and that the list/table answers did not move). Offscreen: every paint assertion goes through image_t.
add_executable(ui_tree_view_test tree_view_test.cpp)
target_link_libraries(ui_tree_view_test PRIVATE ui)
add_test(NAME ui_tree_view_test COMMAND ui_tree_view_test)

# The immediate host's parts after the split: im_object_pool_t (id -> object + last-visit pass, with
# the clock supplied by the caller), group_stack_t (the procedural cursor, layout_t's dual),
# im_pass_t (one content-production pass: batch + claim in one entry, pointer snapshot, visit order,
# and the acquisition/write-back log), and im_host_t's focus/capture-as-query rule (the controls'
# own item state is the truth, collect() clears it before deleting). Offscreen, like the other ui
# tests.
add_executable(ui_im_parts_test im_parts_test.cpp)
target_link_libraries(ui_im_parts_test PRIVATE ui)
add_test(NAME ui_im_parts_test COMMAND ui_im_parts_test)

# State outlives the object: im_state_store_t snapshots the control's own writable non-User
# properties, im_object_pool_t only reuses objects, and im_host_t writes the state back every pass.
# The tests pin (1) text/checked restored after the object is collected, (2) reuse on/off producing
# identical pixels/state/signals, (3) the snapshot boundary (base properties, geometry and the User
# property stay out), and (4) a type change discarding the old snapshot. Offscreen.
add_executable(ui_im_state_test im_state_test.cpp)
target_link_libraries(ui_im_state_test PRIVATE ui)
add_test(NAME ui_im_state_test COMMAND ui_im_state_test)
