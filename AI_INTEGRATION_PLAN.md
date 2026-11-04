# Plan: Live MCP Server in the Seamly2D Program Window

## Context
Claude should create and change patterns **in the running Seamly2D window**: changes appear on the canvas immediately, land on the undo stack (Ctrl+Z works) and act on the pattern that is currently open. A CLI/file-based solution (the discarded first plan) is not enough for this.

Findings from the code:
- All GUI changes go through `QUndoCommand`s in `src/libs/vtools/undocommands/` (e.g. `AddToCalc(const QDomElement &xml, VAbstractPattern *doc)`), which insert a `QDomElement` into `<calculation>` and use `NeedFullParsing` to re-parse and redraw the whole pattern. Undo stack: `qApp->getUndoStack()`.
- The tool dialog is only a thin getter layer; the mapping from input to XML lives in the tools' `SaveOptions()` methods (`src/libs/vtools/tools/drawTools/**`), in the `Attr*`/`ToolType` constants (`src/libs/ifc/ifcdef.h`) and in the XSD `src/libs/ifc/schema/pattern/v0.7.5.xsd`.
- Formulas: `Calculator::EvalFormula` (`src/libs/vpatterndb/calculator.cpp`), checked in `VAbstractTool::CheckFormula`; the XML holds the internal (untranslated) form of a formula.
- Qt 6 (`QT += core gui widgets xml svg printsupport network multimedia`); `QtHttpServer` is not available, so the transport is built on `QTcpServer`/`QLocalServer` (Qt `network` is already linked).

### Spike results (step 1, done)
An `endLine` point was inserted into an open pattern without a dialog via a `QUndoCommand` (headless test run with Xvfb, 14 checks passed): point in `VContainer` and in the DOM, correct position, tool item on the scene, exactly one undo entry "Claude: ...", document marked as modified, undo/redo correct. This gives the following rules for the `PatternEditor`:
- **`AddToCalc` requires a finished GUI tool.** On the first `redo()` (`redoFlag == false`) it only triggers a lite parse; without a previously created tool this fails with "Can't find tool in table". Without a GUI tool, the first `redo()` must do the full parse (`redoFlag = true`, e.g. via a small subclass or a constructor flag in `VUndoCommand`). Alternative: `VTool*::Create(..., Source::FromGui)` like the dialog does.
- **Checking the formula up front is mandatory.** In GUI mode, `VAbstractTool::CheckFormula` opens a modal "Fix formula" dialog for an invalid formula and would block an MCP call. Every formula is therefore evaluated with `Calculator::EvalFormula` against `VContainer::DataVariables()` before the push (invalid variables and syntax errors are rejected, the pattern stays unchanged).
- **Include guard collision:** `vwidgets/calculator/calculator.h` and `vpatterndb/calculator.h` both use `CALCULATOR_H`; if the widget header is already included, `Calculator` is only an incomplete type. `vmcp` only includes `vpatterndb/calculator.h`.
- **Old sample patterns:** `trousers.sm2d` (format 0.6.8) cannot be opened in this environment ("no declaration found for element 'images'", presumably `VPatternConverter::fixGroupsOrder` inserts `<images/>` in a position the schema does not allow). Use a minimal 0.7.6 pattern for tests; derive recipes from the sample patterns only after this is clarified.
- **Pattern paths** must always be passed to the app as absolute paths; a relative path that cannot be resolved shows a modal error dialog and blocks the app (even headless).
- **Build notes (Linux):** needs the Qt 6 dev packages including `qt6-svg-dev` and `qt6-multimedia-dev`, plus `libxerces-c-dev`. `svg_generator.cpp` needed index loops instead of range-for over `QDomNodeList` (Qt 6.8.2).

### Status of step 2 (done)
- `src/libs/vmcp/` (`McpServer`, `McpToolRegistry`, only Qt `core` + `network`) is wired into `libs.pro` and `seamly2d.pro`. Transport: Streamable HTTP, `127.0.0.1` only, a single endpoint `/mcp`, JSON responses (no SSE; GET/DELETE → 405). Supports `initialize` (version negotiation 2025-06-18 / 2025-03-26 / 2024-11-05, server instructions), `ping`, `tools/list`, `tools/call`, notifications (→ 202) and batches.
- Security: bearer token (constant-time comparison, 32 random bytes, regenerated on every start), Host and Origin checks against DNS rebinding, limits for headers (16 KB) and body (4 MB), timeout for stalled connections (15 s), re-entrancy guard (a second request while a tool is running is rejected with "busy").
- App: `src/app/seamly2d/mcp/` (`McpIntegration`, `DialogClaudeConnection`), menu entry "Help → Claude connection…" (on/off, port, copy/renew token, ready-made `.mcp.json` and `claude mcp add` line), status bar "Claude: listening on port …" / "Claude connected (name)". **Off** by default; `VSettings::GetMcpEnabled/GetMcpPort` (default port 47800, a free port if it is taken). Test hooks: `SEAMLY_MCP=1`, `SEAMLY_MCP_PORT`, `SEAMLY_MCP_TOKEN`.
- Vertical-slice tool `get_pattern_info` (unit, file, modified, draft blocks, active block, measurement file, object count).
- Checked against the real app (Xvfb) with a JSON-RPC test client: 34 checks (auth, host/origin, size limit, error codes, handshake, tools, batch, stalled connection); default "off" confirmed; dialog and status bar checked by screenshot. The MCP Inspector was not used.
- Open/decision: the token changes on every start (as planned), so it has to be copied into the Claude configuration after each start. If that is annoying: optionally store the token in the settings (security trade-off).
- The test client is not in the repo yet; in step 6 it becomes a QtTest/integration test (`QT_QPA_PLATFORM=offscreen`).

### Status of step 3, part 1 (done)
- `src/app/seamly2d/mcp/pattern_editor.{h,cpp}`: tools `list_objects` (objects of the active draft block in construction order: element, stored definition, and for points id/name/x/y in the pattern unit) and `add_point` (types `endLine`, `alongLine`). Points are created through the same `VToolEndLine::Create` / `VToolAlongLine::Create` (`Source::FromGui`) calls as the dialogs; each call is one undo macro "Claude: add point X".
- All input is validated up front (id exists and is a point, name is unique, formulas evaluated with `Calculator`, including `CurrentLength` for `alongLine`). On error the pattern stays untouched and the message goes back to Claude.
- Verified against the real app (window maximized on the left monitor): points appear at once, the Undo button reads "Undo Claude: add point A1", Ctrl+Z / Ctrl+Y remove / restore exactly one point; invalid formula, unknown variable, unknown id, duplicate name, identical ids and unknown type are rejected.
- Tool `add_draft_block(name?, unit?)` (`MainWindow::mcpAddDraftBlock`, no dialog): creates a draft block with its base point and makes it active; for the first block it also sets the pattern unit (mm/cm/inch, default mm) and does the same view/status-bar setup as `New()`. Rejects a duplicate name, an unknown unit and a unit change after the first block. One undo step "Claude: add draft block X". Without a draft block, `list_objects`/`add_point` return a hint that points to this tool.
- Verified in the real app: invalid unit, duplicate name and a unit change are rejected; a second block becomes active and the first one greys out; and from an empty window, a German request typed into the shell ("new pattern in cm, draft block Rock, rectangle 40 x 60 cm with points A-D") produced the block and the four points.
- `add_point` types now: `endLine`, `alongLine`, `normal` (positive length = left of the direction base -> second on the screen, i.e. up for a line pointing right; optional extra angle), `bisector` (first/vertex/third point), `lineIntersect` (two lines; parallel lines are rejected up front). New tools `add_line` (refuses a second line between two points that are already connected, including the lines that endLine/alongLine/normal/bisector draw to their base) and `fit_view` (zoom to the whole drawing; the server instructions tell Claude to call it after drawing).
- Verified in the real app: every type and error path via direct calls (bisector at 45 deg, diagonals crossing at the rectangle's center, duplicate lines refused in both directions), and a German request typed into the shell ("rectangle 40 x 60 cm with all four edges, both diagonals and the intersection M as center, then fit the view") on an empty window produced the complete drawing and fitted the view; Claude drew only the one edge that was missing.
- Step 4, part 1: `update_object(id, ...)` and `delete_object(id)`. `update_object` changes the parameters of an existing point or line (name, length, angle, referenced point ids, x/y of a draft block's base point) through the same `SaveToolOptions` undo command as the dialogs; everything that depends on the object follows. Validation up front: unknown arguments, formulas, references must exist and come earlier in the calculation (no circular dependencies, not the object itself), unique names, and a rename is refused while a formula still refers to the old name (e.g. `Line_A_B`). `delete_object` pushes the same `DelTool` command as the context menu, without the confirmation dialog, and refuses objects that other objects use (naming the dependents) and the base point. One undo step each ("Claude: change X", "Claude: delete X").
- Verified in the real app: changing B's length moved E, P and a re-angled D; all error paths leave the pattern untouched; one Ctrl+Z after a delete restored the point (toolbar: "Undo Claude: change A / Redo Claude: delete P").
- Step 4, part 2: variables. In this Seamly2D version the "increments" are the custom variables stored in the pattern file (`<variables><variable .../>`, referenced in formulas as `#name`; measurements come from the loaded measurement file). Tools: `list_variables` (custom variables with formula/value/description, plus measurements and their values), `set_variable(name, formula?, description?)` (create or change; the name is given without `#`) and `delete_variable(name)`. The variables dialog edits the pattern directly without undo, so the tools use their own `QUndoCommand` (`VariableCommand` in `pattern_editor.cpp`) that makes the same `VPattern` edits plus `LiteParseVariables()` / `LiteParseTree()`; it restores the old position on undo. Validation up front: name syntax and uniqueness, formula evaluated without the variable's own value, circular dependencies between variables rejected, `delete_variable` refuses while a tool or another variable still uses it (naming the places).
- Verified in the real app: creating/changing variables moves the points that use them (also through a second variable `#half = #skirt_w/2`); all error paths leave the pattern unchanged; Ctrl+Z steps back through delete, add, description change and a formula change, including the dependent values; and a German request in the shell ("variables rock_breite = 40, rock_laenge = 60, draw the rectangle with them, then change rock_laenge to 55") produced the expected drawing. Not yet verified: `list_variables` with a loaded measurement file (the test pattern has none).
- Open for step 4: editing measurement values, which live in the measurement file (`MeasurementDoc`), not in the pattern.
- Open: more point types (height, shoulder, triangle, cut/axis points, ...), curves and arcs; a clean test pattern (a hand-written 0.7.6 file failed to load without an error message).

### Status of step 6a (Claude shell panel, phase 1: done)
- `src/app/seamly2d/mcp/claude_shell_panel.{h,cpp}`: dock panel (right side, as a tab next to Group Manager and Property Editor; Help > Claude shell brings it to the front, or hides it if it is already in front) with an embedded QTermWidget terminal running the `claude` CLI. Built only on Linux when `qtermwidget6` is found by pkg-config (`mcp.pri`; Debian/Ubuntu: `libqtermwidget-dev` and `libutf8proc-dev`), otherwise it is skipped and the program builds as before.
- On first show the panel starts the MCP server if needed, writes an owner-only temporary MCP config (the token never appears in process arguments), and starts `claude --mcp-config <file> --append-system-prompt <hint>` in the dedicated folder `<AppData>/claude-shell` (so Claude Code's one-time "trust this folder" question concerns an empty folder, not the user's files).
- Verified in the real window: the panel opens, the CLI runs in the terminal, the MCP endpoint comes up without any test environment variables, the CLI exits together with the program. End-to-end verified: the request "add a point B 100 mm to the right of A and a point C exactly halfway between A and B, then list the objects" typed into the shell made Claude Code call `add_point`/`list_objects`; B and C appeared on the canvas (C via `CurrentLength/2`), the Undo button read "Undo Claude: add point C", and an independent `list_objects` call over MCP matched.
- Bug found and fixed in testing: Seamly2D's single-letter shortcuts (tools) stole keystrokes from the terminal. The panel now accepts `QEvent::ShortcutOverride` for the terminal and its children (`ClaudeShellPanel::eventFilter`), so every key reaches the shell.
- Terminal colors follow the program's palette: `ClaudeShellPanel::applyColorScheme()` generates a `.colorscheme` from the canvas background/text color with readable ANSI sets for light and dark themes, and regenerates it on a palette change (the dark variant is untested).
- Polish: remember panel size, Codex/Gemini support (they need different MCP setup arguments), restart on token change.

## Architecture
The MCP server lives **inside the Seamly2D process** (new class in the main program, running in the GUI thread so that all access to document/scene/undo stack is thread-safe):

```
Claude (Desktop/Code)  --MCP (Streamable HTTP, 127.0.0.1:<port>)-->  Seamly2D
                                                                      McpServer (QTcpServer, JSON-RPC 2.0)
                                                                        -> McpToolRegistry
                                                                        -> PatternEditor (XML builder + QUndoCommands)
                                                                        -> VPattern/VContainer (live data)
```
- Transport: local HTTP endpoint `/mcp` (POST JSON-RPC; JSON response, optionally SSE), bound to `127.0.0.1` only, random bearer token per start (visible/copyable in the settings). Optionally a small stdio bridge (`seamly2d-mcp-bridge`) for clients without HTTP.
- Opt-in: menu "Help/Extras → Claude connection" (on/off, port, token) and an entry in `VSettings`; off by default.
- Status display in the status bar ("Claude connected"), changes as undo entries with the text "Claude: ...".

## New components (proposal: `src/libs/vmcp/` + integration in `src/app/seamly2d/`)
`vmcp` is a pure protocol library (only Qt `core` + `network`, no dependency on the pattern libraries). The app registers the tool handlers; this keeps `vmcp` testable in isolation.
1. `mcpserver.{h,cpp}`: TCP/HTTP parsing, JSON-RPC (`initialize`, `ping`, `tools/list`, `tools/call`, later `resources/*`, `prompts/*`), bearer token, Origin check (protection against DNS rebinding), size and time limits.
2. `mcptoolregistry.{h,cpp}`: tool definitions with JSON schema; every call is handled in the GUI thread.
3. `patterneditor.{h,cpp}`: core layer "parameters → `QDomElement` → undo command".
   - Table of tool type → attributes (derived from `SaveOptions`/XSD), ID allocation via `VContainer`/`VAbstractPattern`, name allocation (`A1`, `A2`, …), reference check (only existing earlier IDs), formula check up front via `Calculator` with `VContainer::DataVariables()`.
   - Insertion via `AddToCalc` (existing cursor logic; first `redo()` with full parse, see spike results), changes via `SaveToolOptions`, deletion via `DelTool` (existing dependency check), multi-step actions as a `QUndoCommand` macro so that a single Ctrl+Z reverts everything.
   - Errors (invalid formula, missing reference) → MCP error response, the pattern stays unchanged.
4. Integration with `MainWindow` (`src/app/seamly2d/mainwindow.cpp`): access to the current `VPattern`, switching the draft block, opening/saving files, zoom/selection.

## MCP tools (version 1)
- Read/context: `get_pattern_info` (unit, draft blocks, measurement file), `list_objects` (points, lines, curves, pieces with IDs, names, coordinates, formulas), `get_object`, `list_variables` (measurements + increments), `evaluate_formula`, `get_selection` (objects currently selected in the window), `screenshot` (scene as PNG so Claude can see the result).
- Drawing: `add_point` (type: single, endLine, alongLine, normal, bisector, shoulder, lineIntersect, height, triangle, …), `add_line`, `add_curve` (spline/cubicBezier/path), `add_arc`.
- Changing: `update_object(id, params)`, `move_point`, `rename_object`, `delete_object`, `set_variable` (create/change increments).
- Structure: `add_draft_block`, `set_active_draft_block`, `add_piece(nodes, seam_allowance)`, `add_group`.
- File/output: `new_pattern`, `open_pattern`, `save_pattern`, `export_layout(format, page, grading)` via the existing layout/export classes (`VLayoutGenerator`, `MainWindow::DoExport`), `undo`/`redo`.
- Resources: catalog of tool types with parameters, measurement names of the loaded measurement file, notes on the formula syntax. `prompts`: e.g. "Basic skirt block".

## Knowledge for Claude: server instructions and recipes
Claude learns everything Seamly2D-specific from the server itself; an external knowledge base is not needed:
- **Tool descriptions/JSON schemas** (`tools/list`): per tool its purpose, parameters, point types (e.g. the difference between `alongLine`/`endLine`).
- **Server instructions** (`initialize`): working model (references via IDs of earlier objects, formulas in internal form, a base point is required, unit of the pattern, undo per Claude action, procedure "first `list_objects`/`screenshot`, then change, then verify").
- **Resources** (`resources/read`): tool type catalog, formula syntax/functions, measurement names of the loaded measurement file, recipes.
- **Prompts** (`prompts/get`): recipes as startable workflows.
- **Recipes** as Markdown files in the repo (`src/libs/vmcp/recipes/*.md`, embedded as a Qt resource), each with: required measurements, sequence of steps with concrete tool calls and example formulas, check steps (re-measure, `screenshot`), typical variants (narrower waist, change length). First candidates: **basic skirt block** (front/back piece, darts, creating pieces) and **basic trousers block**; templates to derive from: `src/app/share/samples/patterns/trousers.sm2d`, `male_shirt.sm2d`. Recipes are validated against real patterns (run the recipe, compare the result with the reference pattern).
- Additionally, outside the server: an example `CLAUDE.md`/skill for personal conventions (construction method, naming, seam allowance) in the documentation.

Second stage (not in V1): operations (rotation/mirroring), exotic point types, images/labels.

## Long-term goal: in-app "Claude shell" panel
A dockable panel inside Seamly2D with a Claude chat/command shell that edits the open pattern in place through code (the tool layer above), not by simulating shortcuts or menu clicks. The tool layer (`pattern_editor.*`) stays independent of the transport, so every option below can use it unchanged; only the part that talks to the model differs.

### Options for the panel
| # | Option | How it works | Pros | Cons |
|---|---|---|---|---|
| 1 | **Embedded terminal** running a CLI agent (`claude`, Codex CLI, Gemini CLI) | pty widget in a dock panel; the CLI is pointed at our MCP endpoint | Least code; uses the user's existing subscription login; all agent features for free; any MCP-capable CLI works | Terminal look, not a native UI; needs a terminal widget or own pty code; the CLI must be installed; no custom display of tool calls/undo |
| 2 | **Native chat panel driving a CLI headless** | Start `claude -p --output-format stream-json` with `--mcp-config` (Codex has a similar `exec --json` mode) and render the events in Qt | Native UI, subscription login, session resume | CLI still required; we parse each CLI's event format |
| 3 | **Direct API client in C++** | `QNetworkAccessManager` + own agent loop (Anthropic Messages API with tool use); tool calls run in-process (no HTTP, no token) | No external dependencies; full control over streaming, caching, permissions, UI; easiest for end users | Needs an API key (billed per token, no subscription); we write the agent loop and context handling |
| 4 | **Provider-neutral client** (OpenAI-compatible chat with tools) | Same as 3 against OpenAI, Gemini's OpenAI-compatible endpoint, OpenRouter, or local Ollama / llama.cpp | One code path for many vendors; local models give offline use and privacy | Tool-calling quality varies a lot; small local models probably handle pattern geometry and formulas poorly |
| 5 | **Agent Client Protocol (ACP) panel** | Panel is an ACP client talking to any ACP agent (Claude Code, Gemini CLI, ...) | Cross-vendor standard with rich events | Newer; extra protocol to implement and adapters to depend on (verify current state before committing) |
| 6 | **Sidecar with the Agent SDK** (Node/Python process) | Ship a small runtime that runs the agent loop | Mature agent features | Second runtime inside a C++/Qt app; heavy |

External clients (Claude Desktop, Claude Code, Cursor, ... over MCP) already work today and are the baseline without any panel.

Deciding factors: audience (developers vs. end users without setup), billing (subscription via CLI vs. API key), privacy (pattern data leaves the machine with every cloud option; only option 4 with a local model keeps it local), and who owns the permission model (we do in 3/4, the CLI does in 1/2).

### Decision
- **Start with option 1** (embedded terminal running the `claude` CLI against our MCP endpoint): least effort, validates the whole loop (agent -> MCP -> live pattern) inside the program, and uses the user's login.
- Planned later: an `AgentBackend` interface with a shared panel UI (messages, tool calls with results, "undo this request"), then option 3 (direct Anthropic API, in-process) for end users without a CLI, then option 4 for other vendors and local models. Option 1 stays as an expert mode.

## Implementation steps
1. ~~Spike: insert a point (`endLine`) into the open pattern with test code via `AddToCalc`; confirm that redraw, undo and formula checking work without a dialog.~~ **Done** (see spike results).
2. ~~`McpServer` + registry + settings/menu/token, check `tools/list` and `initialize` with an MCP client; a `get_pattern_info` tool as a vertical slice.~~ **Done** (see status of step 2).
3. `PatternEditor` with read tools and `screenshot`; then drawing tools in order of frequency (single, endLine, alongLine, line, normal, bisector, lineIntersect, curves). **In progress** (see status of step 3, part 1).
4. Change/delete/variables with macro undo and dependency check.
5. Pieces, groups, draft blocks, export/save.
5a. Server instructions, resources and prompts; write the recipes "basic skirt block" and "basic trousers block", run them through MCP and compare with reference patterns; sharpen the recipes where Claude deviates.
6. Tests, documentation (`docs`/README: setting up the connection in Claude Desktop/Code, `.mcp.json` example), translation strings.
6a. In-app panel, phase 1: dockable embedded terminal running a CLI agent against the MCP endpoint (see "Options for the panel").
7. qmake integration: new lib in `src/libs/libs.pro`, inclusion in `src/app/seamly2d/seamly2d.pro`, also under `noTests`.

## Risks
- Every action triggers a full parse → slow with many calls; remedy: a batch tool `apply_operations([...])` with a single re-parse.
- Security: localhost only + token + Origin check, file access only through open/save tools with path check/confirmation dialog; establishing a connection requires the user's consent.
- When the user and Claude edit at the same time: serialization in the GUI thread, calls are rejected while a tool dialog/mode is active.
- Formula translation (`VFormula` localizes decimal separator/variable names): MCP always takes and returns the internal form.

## Verification
- Unit tests (QtTest in `src/test/Seamly2DTest`): `PatternEditor` produces valid XML for every tool type (XSD validation via `VDomDocument::ValidateXML`), rejects invalid formulas/references, undo restores the initial state.
- Integration test with `QT_QPA_PLATFORM=offscreen`: start the app, JSON-RPC over HTTP against `/mcp`, build a basic skirt block, check `list_objects` and export.
- Manual: connect Seamly2D to Claude Desktop/Code, "Draw a basic skirt block with my measurements", then "Waist 2 cm narrower"; the canvas updates live, Ctrl+Z reverts steps, the saved file opens in Seamly2D without errors.
- CI: the existing `xvfb-run -a make check` job covers the new tests.

## Open decisions (defaults)
- Transport: HTTP on localhost + optional stdio bridge (default); alternative: stdio bridge only, with a Unix socket/named pipe.
- Where to implement: new lib `src/libs/vmcp` (default) rather than directly in the app directory.
- Scope of V1: points, lines, curves, variables, pieces, export – without operations.
