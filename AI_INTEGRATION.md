# Working with Claude in Seamly2D

Seamly2D can let Claude create and change the pattern that is open in the running window. Every change shows up on
the canvas at once, lands on the undo stack (Ctrl+Z works, one step per Claude action, named "Claude: ...") and goes
through the same code as the tool dialogs, so Claude's results behave exactly like drawings made by hand.

The connection uses the Model Context Protocol (MCP). Seamly2D runs a small MCP server inside the program; Claude
connects to it. Nothing is sent anywhere by Seamly2D itself: what Claude sees is what its tools return (object names,
positions, formulas, screenshots), and that goes to whichever Claude client you connected.

## Safety model

- **Off by default.** Nothing listens until you switch it on in **Help > Claude connection...**.
- **Local only.** The server listens on `127.0.0.1` and refuses requests that carry a foreign `Host` or `Origin`
  header (protection against web pages talking to it).
- **Token.** Every request needs a random token that changes each time Seamly2D starts. Copy it from the dialog.
- **File access needs your OK.** Saving, opening, exporting and changing a measurement value ask you in a dialog the
  first time Claude touches a file or folder in a session (default answer: No; you can allow it for the rest of the session). Declining makes
  the tool fail and writes nothing.
- **Everything is undoable** except what has been written to disk by an action you confirmed.

## Switching it on

1. **Help > Claude connection...**, tick "Allow Claude to connect to this window".
2. The status bar now shows "Claude: listening on port 47800" (another port if that one is taken). When a client has
   connected it shows "Claude connected (client name)".
3. The dialog offers the token and ready-made configuration for external clients (see below).

## The built-in Claude shell (Linux)

**Help > Claude shell** opens a panel next to the Group Manager and Property Editor with a terminal that runs the
Claude Code CLI (`claude`) and connects it to this window. You type what you want, in your own words and language:

> New pattern in cm, draft block Skirt. Draw a rectangle 50 x 60 cm and make a piece "Front" with 1 cm seam
> allowance. Export it as PDF on A1 to /home/me/patterns/out.

The panel needs the `claude` command (Claude Code) to be installed and you to be logged in. It runs in its own empty
working folder, so Claude Code's one-time "trust this folder" question concerns nothing but that folder. The panel
is built only when QTermWidget (Qt 6) was found while building: on Debian/Ubuntu install `libqtermwidget-dev` and
`libutf8proc-dev` before running qmake. Without it the program builds as before, just without the panel.

## Using another Claude client

Any MCP client that can talk to a local HTTP server with a bearer token can use the endpoint
`http://127.0.0.1:<port>/mcp`. The dialog shows the configuration with the current port and token:

- **Claude Code** (the CLI; the desktop app's Code tab should work the same way for sessions that run on your own
  machine, but that has not been tested): use the
  `claude mcp add --transport http seamly2d http://127.0.0.1:47800/mcp --header "Authorization: Bearer <token>"` line
  from the dialog, or the `.mcp.json` snippet.
- **Claude Desktop:** configure the same URL and header the way your version of Claude Desktop supports it for local
  servers. Depending on the version a small local bridge may be needed; check the Claude Desktop documentation.

The token changes on every start of Seamly2D, so update the client's configuration after restarting the program.

## What Claude can do

Claude learns the tools from the server itself (names, parameters, units, conventions); you do not need to teach it.

| Area | Tools |
|---|---|
| Look | `get_pattern_info`, `list_objects` (filters: element, type, name, ids), `list_variables`, `list_pieces`, `screenshot` (drawing or window), `show_mode`, `fit_view` (everything, the active draft block, or one piece) |
| Draw | `add_draft_block` (also sets the unit), `add_point` (endLine, alongLine, normal, bisector, lineIntersect, height, lineIntersectAxis, curveIntersectAxis, cutCurve, shoulder, triangle, intersectXY, intersectCircles, intersectCircleLine, tangentToCircle, intersectArcs, tangentToArc, intersectCurves), `add_line`, `add_curve` (spline, cubicBezier, path through three or more points), `add_arc`, `add_elliptical_arc` |
| Change | `update_object` (name, formulas, circle/arc/curve intersection points, referenced points and curves, cut direction, position of the base point), `delete_object`, `set_variable`, `delete_variable`, `set_measurement` (a value in the measurement file), `create_measurements`, `attach_measurements`, `undo`, `redo` |
| Pieces | `add_piece` (outline from point and curve ids, optional seam allowance), `update_piece` (quantity, fold, label letter and annotation, seam allowance, name), `set_grainline`, `set_notch`, `add_anchor_point`, `add_internal_path` (types, count, size; also removes), `delete_piece` |
| Files | `save_pattern`, `open_pattern`, `export_layout` (pdf, svg, png, jpg, tif, eps, ps, dxf; arranged on pages or one file per piece) |

Things worth knowing:

- Lengths are in the pattern unit and may be formulas that use measurements and variables. Variables ("increments")
  are written `#name` in formulas, so changing one variable moves everything that depends on it.
- Directions are in degrees: 0 = right, 90 = up, 180 = left, 270 = down on the screen; y grows downwards.
- Invalid input (unknown id, bad formula, name already used, circular dependency, a piece that does not fit the
  page, ...) is refused before anything changes, with a message Claude can read and correct.
- `open_pattern` only works while the window is empty. Opening a file into a window that already holds a pattern
  would start another Seamly2D window that this connection cannot control.
- Measurement files: `list_standard_measurements` finds the standard names (searchable), `create_measurements(path, values, unit?)` writes a new individual `.smis` file (never overwrites; custom names are written `@name`), `attach_measurements(path)` makes the pattern use it (needs a draft block; refused if the pattern uses measurements the file lacks; not undoable). Seamly2D asks the user to confirm both file accesses.
- Measurements: `set_measurement` changes a value in the measurement file of the pattern (individual files only, in
  the pattern unit). The file belongs to the person and is shared by every pattern that uses it, so you are asked to
  confirm; the change is one undo step that writes the old value back to the file.
- Not available yet: the position and look of piece labels, per-node seam allowance, elliptical arcs, changing a
  path curve or an intersection/tangent point after creation (delete it and make a new one), multisize measurement
  tables, creating a measurement file.

## Troubleshooting

- **"Claude connected" never appears:** check that the token in the client matches the dialog (it changes on every
  start) and that nothing else uses the port; Seamly2D falls back to a free port and the status bar shows it.
- **A tool call seems stuck:** a confirmation dialog may be waiting behind another window. Seamly2D answers a second
  tool call with "busy" until the dialog is closed.
- **The Claude shell entry is missing from the Help menu:** the program was built without QTermWidget (see above).

## For developers

- Protocol library: `src/libs/vmcp` (JSON-RPC 2.0 over HTTP, tool registry; depends on Qt core and network only).
- Program side: `src/app/seamly2d/mcp` (`McpIntegration`, `PatternEditor` with all pattern tools,
  `DialogClaudeConnection`, `ClaudeShellPanel`) and a few `mcp...` methods on `MainWindow` that need its private
  state. The tools use the existing tool classes and undo commands; see `AI_INTEGRATION_PLAN.md` (next to this file) for the design,
  the status of each step and the problems found on the way.
- Tests: `src/test/McpTest` (`make check`). `TST_McpServer` tests the protocol library without a GUI (token, Host and
  Origin checks, limits, batches, the busy guard). `TST_McpApp` starts the real program and drives every tool
  through JSON-RPC (geometry, variables, pieces, save and re-open, export, screenshot); it needs a display, so CI
  runs it under `xvfb-run`, and it is skipped when the program is not built or there is no display. Qt's
  `offscreen` platform cannot be used: the main window does not finish starting there.
- Test hooks (environment variables, for automated runs only): `SEAMLY_MCP=1` starts the server regardless of the
  setting, `SEAMLY_MCP_PORT` and `SEAMLY_MCP_TOKEN` fix port and token, and `SEAMLY_MCP_ASSUME_YES=1` answers the
  file-access confirmation dialogs with "yes". Never set the last one in normal use.
