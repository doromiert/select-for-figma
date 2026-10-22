# Select for Figma

A GTK 4/libadwaita Figma client inspired by [ReSketch](https://github.com/Ralsei-x64/ReSketch).
The window, document tabs, dialogs, downloads and preferences are native GTK. Figma runs in embedded Chromium (CEF), including its WebGL editor.

## Run

On Linux x86_64 with Nix flakes enabled:

```sh
nix run github:doromiert/select-for-figma
```

Or run from a local checkout:

```sh
nix run .
nix run . -- https://www.figma.com/design/FILE_KEY/File-name
```

The flake pins both nixpkgs and the CEF SDK. The initial download is large. If your graphics driver cannot share Chromium frames with GTK, use `nix run . -- --software-rendering`.

To install the application and its desktop launcher:

```sh
nix profile add github:doromiert/select-for-figma
```

The command is `select-for-figma`. The package includes `io.github.doromiert.SelectForFigma.desktop`, the application icon, AppStream metadata, and a `figma://` link handler. After installation, launch **Select for Figma** from your desktop's application menu. NixOS configurations can also add the flake's default package to `environment.systemPackages` or Home Manager's `home.packages`.

After rebuilding, fully close a previous instance before launching the new one. The launcher checks the running instance’s build and reports its process ID if it would otherwise forward to an older background process. Navigation and reload during startup share one Chromium browser per native tab, keeping input, rendered frames and Zane attached to the same page.

Home contains the file browser and sign-in flow, including Google popups. Documents have separate native tabs. Ctrl+T opens a new-file tab. Reload uses F5 (Ctrl+F5 without cache), leaving Ctrl+R available for Figma’s rename command. Tab Overview uses F8, Downloads uses F9, Copy Link uses Ctrl+Shift+F8, and the native shortcuts window uses F1; their former Ctrl+Shift+O, Ctrl+J, Ctrl+Shift+C and Ctrl+? bindings remain available to Figma. Home and Reload occupy one header row with tabs, Tab Overview and the main menu. Right-click a document tab to create a named group or move it into one. The square collapse button restores equally sized tabs and group labels. Click a group label to fill the tab strip with that group and return to its last-used tab; other tabs remain accessible in the overview. Grouped and ungrouped tabs use the same size and padding. An expanded group tints the whole header, hides its group title, and places its collapse button before the tabs. Choose a group color when creating or renaming it, or use Change Color from its context menu. Overflowing tabs keep a readable width. Use the scroll buttons, mouse wheel or touchpad; hovering a dragged tab over a scroll button reveals more tabs. The expanded group’s collapse button stays fixed. At narrow widths, use Tab Overview to switch documents. A wide, compact Home card sits above the overview and zooms to the existing Home page. Its grouped cards open an inline drawer, while ordinary tabs retain the native overview proportions and zoom transition. Previews retain full-resolution GTK snapshots, and dragging a card animates its neighbours into place. Group names, colors, membership and collapsed state are restored with the session. Empty groups are removed from the menu and saved session. The shortcuts window uses GNOME’s compact, searchable layout.

Local Fonts and Notifications in Preferences control Chromium’s Figma permissions. Local Fonts permits loopback access for the font helper. Figma’s custom cursor images retain their hotspots and scale. The application ID is `io.github.doromiert.SelectForFigma` and saved data lives under `$XDG_DATA_HOME/select-for-figma` (normally `~/.local/share/select-for-figma`). Earlier GTK profiles and KeePassXC pairings migrate to this identity. Close earlier running versions before the first launch after upgrading.

Tab titles are centered over their full width; close buttons use circular hover surfaces. Adding or closing a tab animates the other tab widths. Header dragging moves neighbouring tabs live, hides the original dragged tab, and settles into the destination. In the overview, New Tab zooms from its button into the new page. Downloads place Show in Files before Open File, which uses the Icon Library external-link icon.

Middle-click a tab in the header or overview to close it.

Ctrl+Tab opens a thumbnail grid, including Home; release Ctrl to activate the highlighted tab. Ctrl+Shift+Tab cycles backward and Esc cancels. Preferences → Workspace → Ctrl+Tab Order selects tab position (default) or most recently used order.

Ctrl+C/Ctrl+Insert and Ctrl+X copy or cut web selections into the desktop clipboard. Website copy buttons, including Figma’s token and link buttons, use the same GTK clipboard bridge. Text, HTML, Figma object data and PNG images retain their formats; Ctrl+V continues to paste from the desktop clipboard. Clipboard payloads travel through Chromium process messages and are not logged.

Group cards match ordinary preview dimensions, with a 2×2 preview and the title underneath. Click one to reveal a full-width drawer directly beneath its row, growing downward with its heading visible first; the pointer stays aligned to the group and the other cards dim. Escape or the drawer’s collapse button closes it. Choose **New Tab Group…** from the main menu or the overview’s Tab Options menu to create a group with a new file tab. Right-click an existing tab for **New Group…** or **Move to Group**; right-click the group label/card to rename, collapse, or ungroup it. Drag group labels or overview cards to reorder whole groups; their members move together. Drag individual tabs within a group to reorder its members, onto another group to move it there, or out of the drawer into empty overview space to remove it from the group. Drop a tab onto the middle of another tab to create a group; use the edges to reorder. This works in both the header and overview. Drag a tab or whole group outside the window to open it in a new window, preserving the existing pages and the group’s last-used tab. Reordering uses the same live movement and animated settling as ordinary tabs. Members stay together in both the header and keyboard switching order. Groups are enabled in the normal app without flags.

## Editor theme

Figma's homepage and editor chrome use an Adwaita theme by default, inspired by [Rnote](https://rnote.flxzt.net/): rounded floating toolbars, quieter side panels, unified input fields and GNOME accent colors. It follows Select's Light, Dark or Follow System appearance and updates open documents immediately, including nested context menus and page selections. Canvas artwork, color swatches and document typography retain their original styling. Disable **Preferences → Appearance → Adwaita Editor Theme** to restore Figma's own styling. Sign-in pages retain their original styling. The theme uses Figma's UI color tokens and panel selectors; editor markup changes may require theme updates.

## KeePassXC

Enable Browser Integration in KeePassXC, unlock your database, then connect from the app’s Preferences → Passwords. Approve the association in KeePassXC. Pairing is stored in the desktop secret service.

Choose **Fill from KeePassXC** in the main menu on a Figma or Google sign-in page. Search the returned entries by name or username and select one to fill the current page. The picker searches entries KeePassXC has authorized for that site. Filling does not submit the form or place credentials on the clipboard.

## Zane

Zane is an organization-first Codex assistant. In an open design, press `/` and type **hey zane**, optionally followed by a request. **hey zane** summons it to your current page and cursor anywhere in the same file, including when its separate account is browsing another page. Continue with ordinary cursor-chat messages near Zane: a 1.2-second typing pause sends a message, or Enter sends it immediately. Messages during work steer the active task. Tell Zane **you’re free to go** to invoke its leave tool; it stops work and disappears until **hey zane** wakes it again. Addressed chat drafts retain up to 4,000 characters instead of Figma’s 50-character input limit. Zane chooses message breaks with its `speak` tool, aiming for short chunks around 60 characters without truncation. Chunks appear one at a time for at least 1.8 seconds, or 1.2 seconds plus 0.05 seconds per Unicode character when longer. The last bubble clears after its reading time. A new message or dismissal cancels unfinished speech. There are no Ask Zane or Dismiss controls.

Local mode is the default and uses Figma’s actual multiplayer cursor/chat component in purple. The cursor remains at its canvas coordinates when you pan away. Click its purple **Z** avatar to follow it; clicking the canvas, scrolling, or Escape stops following. Say **follow me** to keep Zane beside your cursor while moving and panning. The Chat model can list and switch pages, search layer names, focus matches, pan, zoom and fit a page without handing the request to the Work model. The GTK conversation panel remains a compatibility fallback if an editor update breaks its private cursor component.

Optional **Use a Separate Figma Account** and **Sign In…** controls are in Preferences → Zane. Sign in to Zane’s own Figma account in an isolated profile. After connection it runs in the background, using that account’s real multiplayer session; other collaborators can see and follow it. Navigation, layer inspection, screenshots and editing use Zane’s background editor in account mode, so its camera and page can move independently of yours. Replies keep its current view. Account-mode “follow me” uses Figma’s native follow-session operation. Login is the only time its window needs to appear. **Give Zane Edit Access Automatically** invites the signed-in Zane account to the current file on eligible Starter/personal files, checks existing access first, and respects the server’s permissions. Paid-plan permission/seat changes use Figma’s normal sharing approval flow. Local mode never registers a broadcast session or writes cursor nodes into the design.

**User mode** means this separate Figma account presence, visible to other users in the file. Say **switch to user mode** or **switch to local mode** to change it directly through Zane’s `presence_mode` tool. Chat and Work can both switch presence modes, which are independent of model selection. The tool reports actual readiness and edit permission; a selected preference alone does not count as connected. Cold page-load failures retry, and commands wait briefly for the account to connect before reporting its status. Sign in through Preferences if the account requires authentication.

Install a current Codex CLI on your `PATH` and sign in with `codex login`. Zane uses `codex app-server` and your configured model. It can inspect the visible editor, see the canvas, point at locations, and read loaded comments. To read the complete comment threads, including replies and resolved status, add a Figma personal access token with `file_comments:read` in **Preferences → Zane**. The token stays in your system keyring, is never passed to the model, and can be removed with **Forget Token**.

Choose **Chat Model / Chat Reasoning** and **Work Model / Work Reasoning** in Preferences → Zane. Models come from your installed Codex catalog; reasoning options follow each model’s supported levels. Use Codex Settings preserves your existing configuration. Selections are saved and apply to the next message. The Chat model handles conversation and straightforward questions; organization, edits and complex tasks are delegated to the Work model, which owns all editing tools. Lower reasoning levels favor quicker replies.

Page/layer organization and design edits use the signed-in editor directly. Zane explicitly disables every inherited MCP server for its child Codex process and its Chat/Work threads; an empty `mcp_servers` table alone does not remove inherited servers. Your normal Codex configuration is unchanged. These tools do not consume Figma MCP quota. Figma's own file permissions and page limits still apply.

Zane's `edit_design` tool manages workspaces automatically. Frames created by Zane, frames verified as new since the file session began, and artwork outside frames can be edited directly. New work does not require a duplicate. Existing original frames are protected: Zane prepares a copy once, maps original layer IDs to the copy, and reuses that workspace while the source is unchanged. Changed sources get a fresh copy. Continue with the returned `rootId`; use `newFrame` for a fresh frame or `canvas=true` for unframed artwork. Unknown/unloaded frame histories stay protected. Frame protection follows IDs across page moves.

In **user mode**, duplication uses standard mouse selection and **Ctrl+D** in Zane's own background editor, with selection and new-node verification. If a frame caption cannot be selected, it tries the contents and **Shift+Enter** to select the parent. New frames use **F** and a mouse drag on empty canvas. There is no native-clone or MCP fallback for these user-mode operations. Local mode can clone through the editor's native scene graph. User-mode pen paths use **P**, ordinary canvas clicks and **Escape**, and verify the new vector. Curves are traced as editable polylines within one canvas unit; they do not depend on the private vector codec. Structured text, fill, sizing, positioning, auto-layout, rectangle edits and path styling use native editor transactions inside the verified workspace. Local-mode pen paths preserve cubic Bezier handles. Both support closed outlines and stroke styling. For example: **hey zane draw a smiley on empty canvas**. Private editor bindings can require updates when Figma changes them.

Zane can write its own JavaScript behaviors through the Work model's `script_behavior` tool, in either presence mode. Scripts can use loops and conditions with awaited `zane.key`, `click`, `move`, `drag`, `wheel`, `wait` and `state` calls. Zane chooses the tools, coordinates, handle drags and timing, rather than relying on a fixed drawing routine. Each input returns viewport, selection and new-layer feedback; Zane checks the result with `view_canvas`. A fresh drawing such as “draw a smiley” defaults to empty-canvas work; preparing this scope does not require private editing transactions. Design scripts automatically prepare or reuse an editable workspace and use coordinates local to that workspace; `canvas=true` uses absolute canvas coordinates. Existing frames stay protected. Code runs in a separate worker without DOM, network or storage access, limited to 256 calls and 25 seconds; cancellation terminates it, including infinite loops. All keys and mouse buttons are released after each input call. Failures identify the stopped step. Scripts never move the desktop pointer or use MCP.

Zane can also script canvas navigation with `canvas_actions`: a sequence of middle-button pan drags, Ctrl-wheel zoom, named keyboard zoom shortcuts and short pauses. Both Chat and Work can use this fallback when direct navigation is insufficient. These send real input to Chromium in the connected editor; account mode targets Zane’s own background editor. Sequences are limited to 32 steps and a ten-second replay budget, stop on cancellation/page changes, and leave every key/button released. Visible text fields and overlays block replay. Zane verifies the result with a screenshot. This is declarative behavior scripting, with no arbitrary JavaScript, desktop control or MCP quota consumption; normal Figma permissions and plan limits still apply. Zane cannot post comments.

Conversations are ephemeral. Canvas screenshots, relevant file context and comments are sent to Codex when requested by its tools. Live Figma changes and editor behavior still need verification against a real authenticated editor; the automated checks use a synthetic editor, an offline Codex server and a simulated Figma document.

Wake-up and context messages use Chromium’s native process channel, independent of page console hooks. Native typing also supports waking Zane when the editor consumes DOM keyboard/input events.

## Develop and verify

```sh
nix develop
cmake -S . -B build-flake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-flake -j 4
ctest --test-dir build-flake --output-on-failure
bash tools/run.sh
```

`bash tools/check-ui.sh` runs native tab/group, overview transition/preview quality/real mouse dragging, shortcuts and login-picker checks, WebGL/paste/custom-cursor checks, selected text/cut/copy-button and rich clipboard checks, local OAuth popup fixtures, notification delivery, and Zane wake-up, streaming, comment access and guarded Figma operations on a private Xvfb display and D-Bus session. It creates temporary Chromium profiles and does not access your Google account, KeePassXC vault or desktop clipboard, call a live Codex model, modify a real Figma file, or send banners to your desktop. The artifacts directory is printed on completion. Pass `--flake` to verify `nix run .`, or an installed executable path to check that build.

The Google handoff fixtures verify opener communication, shared cookies, popup closure and blank-page returns. Actual Google/Figma authentication and rich Figma-object paste still require testing with a real session.

Licensed under GPL-3.0-only. Chromium/CEF and its bundled dependencies retain their own licenses, installed alongside the application.

The grouped overview adapts layout and transition calculations from [libadwaita](https://gitlab.gnome.org/GNOME/libadwaita), Copyright © 2021–2022 Purism SPC, authored by Alice Mikhaylenko and originally licensed under LGPL-2.1-or-later.

The external-link icon is by Jakub Steiner, distributed under CC0 through GNOME Icon Library.

The native cursor integration follows the runtime component-discovery approach documented by [Figura](https://github.com/Figuate/Figura-Figma-Custom-Client-/blob/main/docs/cursor-agent.md). Its native cursor presentation also remains local to the client.
