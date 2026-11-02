# Plan: Live-MCP-Server im Seamly2D-Programmfenster

## Context
Claude soll Schnittmuster **im laufenden Seamly2D-Fenster** erstellen und ändern: Änderungen erscheinen sofort auf der Zeichenfläche, landen im Undo-Stack (Strg+Z funktioniert) und wirken auf das gerade geöffnete Muster. Eine CLI-/Datei-Lösung (verworfener erster Plan) reicht dafür nicht.

Erkenntnisse aus dem Code:
- Alle GUI-Änderungen laufen über `QUndoCommand`s in `src/libs/vtools/undocommands/` (z. B. `AddToCalc(const QDomElement &xml, VAbstractPattern *doc)`), die ein `QDomElement` in `<calculation>` einfügen und per `NeedFullParsing` das gesamte Muster neu parsen und neu zeichnen. Undo-Stack: `qApp->getUndoStack()`.
- Der Tool-Dialog ist nur eine dünne Getter-Schicht; das Mapping Eingabe → XML liegt in den `SaveOptions()`-Methoden der Tools (`src/libs/vtools/tools/drawTools/**`), den `Attr*`/`ToolType`-Konstanten (`src/libs/ifc/ifcdef.h`) und im XSD `src/libs/ifc/schema/pattern/v0.7.5.xsd`.
- Formeln: `Calculator::EvalFormula` (`src/libs/vpatterndb/calculator.cpp`), Prüfung in `VAbstractTool::CheckFormula`; im XML steht die interne (unübersetzte) Formelform.
- Qt 6 (`QT += core gui widgets xml svg printsupport network multimedia`); `QtHttpServer` ist nicht verfügbar → Transport selbst über `QTcpServer`/`QLocalServer` (Qt `network` ist schon eingebunden).

## Architektur
MCP-Server **im Seamly2D-Prozess** (neue Klasse im Hauptprogramm, läuft im GUI-Thread, damit alle Zugriffe auf Dokument/Szene/Undo-Stack thread-sicher sind):

```
Claude (Desktop/Code)  --MCP (Streamable HTTP, 127.0.0.1:<Port>)-->  Seamly2D
                                                                      McpServer (QTcpServer, JSON-RPC 2.0)
                                                                        -> McpToolRegistry
                                                                        -> PatternEditor (XML-Builder + QUndoCommands)
                                                                        -> VPattern/VContainer (Live-Daten)
```
- Transport: lokaler HTTP-Endpunkt `/mcp` (POST JSON-RPC; Antwort JSON, optional SSE), nur an `127.0.0.1`, zufälliges Bearer-Token pro Start (in den Einstellungen sichtbar/kopierbar). Optional kleine stdio-Brücke (`seamly2d-mcp-bridge`) für Clients ohne HTTP.
- Opt-in: Menü „Hilfe/Extras → Claude-Verbindung“ (an/aus, Port, Token) und Eintrag in `VSettings`; standardmäßig aus.
- Statusanzeige in der Statusleiste („Claude verbunden“), Änderungen als Undo-Einträge mit Text „Claude: …“.

## Neue Komponenten (Vorschlag: `src/libs/vmcp/` + Anbindung in `src/app/seamly2d/`)
1. `mcpserver.{h,cpp}`: TCP/HTTP-Parsing, JSON-RPC (`initialize`, `tools/list`, `tools/call`, `resources/list|read`), Auth, Zeitlimit.
2. `mcptoolregistry.{h,cpp}`: Tool-Definitionen mit JSON-Schema; jede Aufrufbehandlung im GUI-Thread.
3. `patterneditor.{h,cpp}`: Kernschicht „Parameter → `QDomElement` → Undo-Command“.
   - Tabelle Tool-Typ → Attribute (aus `SaveOptions`/XSD abgeleitet), ID-Vergabe über `VContainer`/`VAbstractPattern`, Namensvergabe (`A1`, `A2`…), Referenzprüfung (nur existierende frühere IDs), Formelprüfung vorab über `Calculator` mit `VContainer::DataVariables()`.
   - Einfügen über `AddToCalc` (vorhandene Cursor-Logik), Änderungen über `SaveToolOptions`, Löschen über `DelTool` (bestehende Abhängigkeitsprüfung), Mehrfachaktionen als `QUndoCommand`-Makro, damit ein Strg+Z alles zurücknimmt.
   - Fehler (ungültige Formel, fehlende Referenz) → MCP-Fehlerantwort, Muster bleibt unverändert.
4. Anbindung an `MainWindow` (`src/app/seamly2d/mainwindow.cpp`): Zugriff auf aktuelles `VPattern`, Draft-Block-Wechsel, Dateien öffnen/speichern, Zoom/Auswahl.

## MCP-Tools (Version 1)
- Lesen/Kontext: `get_pattern_info` (Einheit, Draft-Blöcke, Maßdatei), `list_objects` (Punkte, Linien, Kurven, Teile mit IDs, Namen, Koordinaten, Formeln), `get_object`, `list_variables` (Maße + Inkremente), `evaluate_formula`, `get_selection` (aktuell im Fenster gewählte Objekte), `screenshot` (Szene als PNG, damit Claude das Ergebnis sieht).
- Zeichnen: `add_point` (type: single, endLine, alongLine, normal, bisector, shoulder, lineIntersect, height, triangle, …), `add_line`, `add_curve` (spline/cubicBezier/path), `add_arc`.
- Ändern: `update_object(id, params)`, `move_point`, `rename_object`, `delete_object`, `set_variable` (Inkremente anlegen/ändern).
- Struktur: `add_draft_block`, `set_active_draft_block`, `add_piece(nodes, seam_allowance)`, `add_group`.
- Datei/Ausgabe: `new_pattern`, `open_pattern`, `save_pattern`, `export_layout(format, Seite, Gradation)` über bestehende Layout-/Export-Klassen (`VLayoutGenerator`, `MainWindow::DoExport`), `undo`/`redo`.
- Ressourcen: Katalog der Tool-Typen mit Parametern, Maßnamen der geladenen Maßdatei, Hinweise zur Formelsyntax. `prompts`: z. B. „Rock-Grundschnitt“.

## Wissen für Claude: Server-Instructions und Rezepte
Claude erfährt alles Seamly2D-Spezifische vom Server selbst, eine externe Wissensbasis ist nicht nötig:
- **Tool-Beschreibungen/JSON-Schemas** (`tools/list`): pro Tool Zweck, Parameter, Punkttypen (z. B. Unterschied `alongLine`/`endLine`).
- **Server-Instructions** (`initialize`): Arbeitsmodell (Referenzen über IDs früherer Objekte, Formeln in interner Form, Basispunkt nötig, Einheit des Musters, Undo pro Claude-Aktion, Vorgehen „erst `list_objects`/`screenshot`, dann ändern, dann prüfen“).
- **Ressourcen** (`resources/read`): Tool-Typ-Katalog, Formelsyntax/Funktionen, Maßnamen der geladenen Maßdatei, Rezepte.
- **Prompts** (`prompts/get`): Rezepte als startbare Abläufe.
- **Rezepte** als Markdown-Dateien im Repo (`src/libs/vmcp/recipes/*.md`, per Qt-Ressource eingebunden), je Rezept: benötigte Maße, Schrittfolge mit konkreten Tool-Aufrufen und Beispiel-Formeln, Kontrollschritte (Maße nachmessen, `screenshot`), typische Varianten (Taille enger, Länge ändern). Erste Kandidaten: **Rock-Grundschnitt** (Vorder-/Rückenteil, Abnäher, Teile anlegen) und **Hosen-Grundschnitt**; Vorlage zum Ableiten: `src/app/share/samples/patterns/trousers.sm2d`, `male_shirt.sm2d`. Rezepte werden an echten Mustern validiert (Rezept ausführen, Ergebnis mit Referenzmuster vergleichen).
- Ergänzend, außerhalb des Servers: Beispiel-`CLAUDE.md`/Skill für persönliche Konventionen (Konstruktionsmethode, Benennung, Nahtzugabe) in der Doku.

Zweite Ausbaustufe (nicht in V1): Operationen (Rotation/Spiegeln), exotische Punkttypen, Bilder/Beschriftungen.

## Umsetzungsschritte
1. Spike: einen Punkt (`endLine`) per Testcode über `AddToCalc` in das offene Muster einfügen; bestätigen, dass Neuzeichnen, Undo und Formelprüfung ohne Dialog funktionieren.
2. `McpServer` + Registry + Einstellungen/Menü/Token, `tools/list` und `initialize` mit einem MCP-Client (MCP Inspector) prüfen.
3. `PatternEditor` mit Lese-Tools und `screenshot`; dann Zeichen-Tools in Reihenfolge der Häufigkeit (single, endLine, alongLine, line, normal, bisector, lineIntersect, Kurven).
4. Ändern/Löschen/Variablen mit Makro-Undo und Abhängigkeitsprüfung.
5. Teile (Pieces), Gruppen, Draft-Blöcke, Export/Speichern.
5a. Server-Instructions, Ressourcen und Prompts; Rezepte „Rock-Grundschnitt“ und „Hosen-Grundschnitt“ schreiben, per MCP durchspielen und mit Referenzmustern vergleichen; Rezepte nachschärfen, wo Claude abweicht.
6. Tests, Doku (`docs`/README: Verbindung in Claude Desktop/Code einrichten, `.mcp.json`-Beispiel), Übersetzungs-Strings.
7. qmake-Integration: neue Lib in `src/libs/libs.pro`, Einbindung in `src/app/seamly2d/seamly2d.pro`, auch unter `noTests`.

## Risiken
- Nach jeder Aktion erfolgt ein Vollparse → bei vielen Aufrufen langsam; Abhilfe: Batch-Tool `apply_operations([...])` mit einem einzigen Reparse.
- Sicherheit: nur Localhost + Token, Dateizugriffe nur über Open/Save-Tools mit Pfadprüfung/Bestätigungsdialog; Verbindungsaufbau erfordert Zustimmung des Nutzers.
- Beim Bearbeiten durch den Nutzer und Claude gleichzeitig: Serialisierung im GUI-Thread, Aufrufe werden abgewiesen, solange ein Tool-Dialog/Modus aktiv ist.
- Formelübersetzung (`VFormula` lokalisiert Dezimaltrenner/Variablennamen): MCP nimmt und liefert immer die interne Form.

## Verifikation
- Unit-Tests (QtTest in `src/test/Seamly2DTest`): `PatternEditor` erzeugt für jeden Tool-Typ gültiges XML (XSD-Validierung über `VDomDocument::ValidateXML`), lehnt ungültige Formeln/Referenzen ab, Undo stellt den Ausgangszustand her.
- Integrationstest mit `QT_QPA_PLATFORM=offscreen`: App starten, JSON-RPC per HTTP gegen `/mcp`, Rock-Grundschnitt aufbauen, `list_objects` und Export prüfen.
- Manuell: Seamly2D mit Claude Desktop/Code verbinden, „Zeichne einen Rock-Grundschnitt mit meinen Maßen“, dann „Taille 2 cm enger“; Zeichenfläche aktualisiert sich live, Strg+Z nimmt Schritte zurück, gespeicherte Datei öffnet sich in Seamly2D fehlerfrei.
- CI: bestehender `xvfb-run -a make check`-Job deckt die neuen Tests ab.

## Offene Entscheidungen (Default)
- Transport: HTTP auf Localhost + optionale stdio-Brücke (Default); Alternative nur stdio-Brücke mit Unix-Socket/Named Pipe.
- Ort der Implementierung: neue Lib `src/libs/vmcp` (Default) statt direkt im App-Verzeichnis.
- Umfang V1: Punkte, Linien, Kurven, Variablen, Teile, Export – ohne Operationen.
