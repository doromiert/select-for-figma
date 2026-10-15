#!/usr/bin/env bash
set -euo pipefail
if [[ "${FIGMA_GTK_PRIVATE_BUS:-}" != 1 ]]; then
  bus_config=$(mktemp /tmp/figma-gtk-test-bus.XXXXXX.conf)
  cat >"$bus_config" <<'XML'
<busconfig>
  <type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*"/><allow receive_sender="*"/><allow own="*"/>
  </policy>
</busconfig>
XML
  trap 'rm -f "$bus_config"' EXIT
  dbus-run-session --config-file="$bus_config" -- env FIGMA_GTK_PRIVATE_BUS=1 bash "$0" "$@"
  exit
fi
# Run inside nix develop. The display and Chromium profile are private.
app_binary="${1:-$PWD/build-flake/figma-gtk}"
app_command=("$app_binary")
if [[ "$app_binary" == --flake ]]; then
  # Build once before starting pointer drivers, and keep the source immutable
  # throughout the suite even if the working tree is edited during a check.
  flake_source=$(nix flake metadata --json --no-write-lock-file . | python3 -c 'import json,sys; print(json.load(sys.stdin)["path"])')
  nix build "path:$flake_source#default" --no-link
  app_command=(nix run "path:$flake_source#default" --)
fi
check_dir=$(mktemp -d /tmp/figma-gtk-ui-check.XXXXXX)
xvfb_binary="${XVFB_BINARY:-Xvfb}"
"$xvfb_binary" -displayfd 3 -screen 0 1200x900x24 -nolisten tcp -ac 3>"$check_dir/display" >"$check_dir/xvfb.log" 2>&1 &
display_pid=$!
python3 tools/auth-fixture.py "$check_dir/port" >"$check_dir/fixture.log" 2>&1 &
fixture_pid=$!
"${FIGMA_GTK_NOTIFICATION_FIXTURE:-$PWD/build-flake/notification-fixture}" "$check_dir/notifications-ready" >"$check_dir/notifications.log" 2>&1 &
notification_pid=$!
trap 'kill "$notification_pid" "$fixture_pid" "$display_pid" 2>/dev/null || true' EXIT
for ((i=0; i<100; i++)); do
  if [[ -s "$check_dir/display" && -s "$check_dir/port" && -s "$check_dir/notifications-ready" ]]; then break; fi
  sleep 0.05
done
[[ -s "$check_dir/display" && -s "$check_dir/port" && -s "$check_dir/notifications-ready" ]]
[[ "$(cat "$check_dir/display")" =~ ^[0-9]+$ ]]
export DISPLAY=":$(cat "$check_dir/display")" GDK_BACKEND=x11 GSK_RENDERER=gl
export GIO_USE_VFS=local GTK_A11Y=none GTK_USE_PORTAL=0
unset WAYLAND_DISPLAY
run_check() {
  local name="$1"; shift
  if [[ -n "${FIGMA_GTK_CHECKS:-}" && " $FIGMA_GTK_CHECKS " != *" $name "* ]]; then return; fi
  local driver_pids=()
  if [[ "$name" == zane-keyboard ]]; then python3 tools/drag-fixture.py "$check_dir/$name.png.drag.json" >"$check_dir/$name-driver.log" 2>&1 & driver_pids+=("$!"); fi
  if [[ "$name" == overflow-drag || "$name" == interaction-* || "$name" == drag || "$name" == header-drag || "$name" == middle-click || "$name" == group-create || "$name" == group-header-drag || "$name" == group-overview-drag || "$name" == group-member-drag || "$name" == group-drawer-drag || "$name" == group-tab-drag || "$name" == group-join-drag || "$name" == group-extract-drag || "$name" == group-transfer-drag ]]; then python3 tools/drag-fixture.py "$check_dir/$name.png.drag.json" >"$check_dir/$name-driver.log" 2>&1 & driver_pids+=("$!"); fi
  if [[ "$name" == middle-click ]]; then python3 tools/drag-fixture.py "$check_dir/$name.png.overview.drag.json" >"$check_dir/$name-overview-driver.log" 2>&1 & driver_pids+=("$!"); fi
  if [[ "$name" == group-create ]]; then python3 tools/drag-fixture.py "$check_dir/$name.png.menu.drag.json" >"$check_dir/$name-menu-driver.log" 2>&1 & driver_pids+=("$!"); fi
  export FIGMA_GTK_TEST_ZANE_LOG="$check_dir/$name.zane.log"
  if ! timeout 25s "${app_command[@]}" "$@" --software-rendering --screenshot="$check_dir/$name.png" >"$check_dir/$name.log" 2>&1; then
    if [[ "${#driver_pids[@]}" -gt 0 ]]; then kill "${driver_pids[@]}" 2>/dev/null || true; fi
    cat "$check_dir/$name.log"; return 1
  fi
  for pid in "${driver_pids[@]}"; do wait "$pid" || { cat "$check_dir/$name"*driver.log; return 1; }; done
  if rg -q 'CRITICAL|assertion .*failed|Authentication handoff: FAIL|diagnostic: FAIL' "$check_dir/$name.log"; then
    cat "$check_dir/$name.log"; return 1
  fi
  rg 'Header height:|Tab strip:|Narrow header:|diagnostic:|Authentication handoff:' "$check_dir/$name.log"
}
run_instance_check() {
  local name="$1" mode="$2"
  if [[ -n "${FIGMA_GTK_CHECKS:-}" && " $FIGMA_GTK_CHECKS " != *" $name "* ]]; then return; fi
  "${FIGMA_GTK_INSTANCE_FIXTURE:-$PWD/build-flake/instance-fixture}" "$mode" "$check_dir/$name.ready" >"$check_dir/$name-primary.log" 2>&1 &
  local primary_pid=$!
  for ((i=0; i<100; i++)); do
    if [[ -s "$check_dir/$name.ready" ]]; then break; fi
    sleep 0.05
  done
  local status=0 valid=0
  timeout 25s "${app_command[@]}" --render-test --software-rendering >"$check_dir/$name.log" 2>&1 || status=$?
  if [[ "$mode" == same ]]; then
    if [[ "$status" == 0 ]] && rg -q '^Forwarded activation: PASS$' "$check_dir/$name-primary.log"; then valid=1; fi
  else
    if [[ "$status" == 1 ]] && rg -q 'An older Select for Figma process is still running' "$check_dir/$name.log" && rg -q "kill -TERM $primary_pid" "$check_dir/$name.log" && ! rg -q 'Forwarded activation' "$check_dir/$name-primary.log"; then valid=1; fi
  fi
  kill "$primary_pid" 2>/dev/null || true
  wait "$primary_pid" 2>/dev/null || true
  if [[ "$valid" != 1 ]]; then cat "$check_dir/$name.log" "$check_dir/$name-primary.log"; return 1; fi
  printf '%s diagnostic: PASS\n' "$name"
}
run_instance_check instance-stale legacy
run_instance_check instance-forward same
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_SPEECH=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-speech --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_MODELS=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-models --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_CURSOR=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-cursor --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_CURSOR=1 FIGMA_GTK_TEST_ZANE_ACTIONS=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-actions --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_USER_MODE=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-user-mode --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_USER_MODE=1 FIGMA_GTK_TEST_ZANE_USER_INPUT=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-user-input --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_SCRIPT=1 FIGMA_GTK_TEST_ZANE_CANVAS_SCRIPT=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-canvas-script --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_SCRIPT=1 FIGMA_GTK_TEST_ZANE_CANVAS_SCRIPT=1 FIGMA_GTK_TEST_ZANE_USER_MODE=1 FIGMA_GTK_TEST_ZANE_USER_INPUT=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-user-canvas-script --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_SCRIPT=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-script --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_SCRIPT=1 FIGMA_GTK_TEST_ZANE_USER_MODE=1 FIGMA_GTK_TEST_ZANE_USER_INPUT=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-user-script --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_CURSOR=1 FIGMA_GTK_TEST_ZANE_FOLLOWUP=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-followup --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" FIGMA_GTK_TEST_ZANE_API="http://127.0.0.1:$(cat "$check_dir/port")/zane/comments" run_check zane-comments --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_NATIVE=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-native --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_NATIVE=1 FIGMA_GTK_TEST_ZANE_SITE_HOOKS=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-site-hooks --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_NATIVE=1 FIGMA_GTK_TEST_ZANE_SITE_HOOKS=1 FIGMA_GTK_TEST_ZANE_KEYBOARD=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-keyboard --render-test
FIGMA_GTK_TEST_CREATION_RACE=1 FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check creation-race --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_CANCEL=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-cancel --render-test
FIGMA_GTK_TEST_ZANE=1 FIGMA_GTK_TEST_ZANE_RECOVER=1 FIGMA_GTK_TEST_CODEX="$PWD/tools/zane-fixture.py" run_check zane-recover --render-test
FIGMA_GTK_TEST_LOGIN_SEARCH=1 run_check native --self-test
FIGMA_GTK_TEST_NARROW=1 run_check narrow --self-test
FIGMA_GTK_TEST_SHORTCUTS=1 run_check shortcuts --self-test
FIGMA_GTK_TEST_ABOUT=1 run_check about --self-test
FIGMA_GTK_TEST_GROUPS=1 run_check groups --self-test
FIGMA_GTK_TEST_GROUP_CREATION=1 run_check group-create --self-test
FIGMA_GTK_TEST_GHOST_GROUPS=1 run_check ghost-groups --self-test
FIGMA_GTK_TEST_OVERFLOW=1 run_check overflow --self-test
FIGMA_GTK_TEST_OVERFLOW=1 FIGMA_GTK_TEST_OVERFLOW_DRAG=1 run_check overflow-drag --self-test
FIGMA_GTK_TEST_OVERFLOW=1 FIGMA_GTK_TEST_OVERFLOW_GROUP=1 run_check overflow-group --self-test
FIGMA_GTK_TEST_DRAG_INTERACTION=1 run_check interaction-tab-header --self-test
FIGMA_GTK_TEST_DRAG_INTERACTION=1 FIGMA_GTK_TEST_INTERACTION_OVERVIEW=1 run_check interaction-tab-overview --self-test
FIGMA_GTK_TEST_DRAG_INTERACTION=1 FIGMA_GTK_TEST_INTERACTION_WHOLE=1 run_check interaction-group-header --self-test
FIGMA_GTK_TEST_DRAG_INTERACTION=1 FIGMA_GTK_TEST_INTERACTION_WHOLE=1 FIGMA_GTK_TEST_INTERACTION_OVERVIEW=1 run_check interaction-group-overview --self-test
FIGMA_GTK_TEST_DRAG_INTERACTION=1 FIGMA_GTK_TEST_INTERACTION_UNGROUP=1 FIGMA_GTK_TEST_INTERACTION_OVERVIEW=1 run_check interaction-ungroup --self-test
FIGMA_GTK_TEST_DRAG_INTERACTION=1 FIGMA_GTK_TEST_INTERACTION_MERGE=1 run_check interaction-merge-header --self-test
FIGMA_GTK_TEST_DRAG_INTERACTION=1 FIGMA_GTK_TEST_INTERACTION_MERGE=1 FIGMA_GTK_TEST_INTERACTION_OVERVIEW=1 run_check interaction-merge-overview --self-test
FIGMA_GTK_TEST_GROUP_DRAG=1 run_check group-header-drag --self-test
FIGMA_GTK_TEST_GROUP_DRAG=1 FIGMA_GTK_TEST_GROUP_DRAG_OVERVIEW=1 run_check group-overview-drag --self-test
FIGMA_GTK_TEST_GROUP_DRAG=1 FIGMA_GTK_TEST_GROUP_DRAG_MEMBERS=1 run_check group-member-drag --self-test
FIGMA_GTK_TEST_GROUP_DRAG=1 FIGMA_GTK_TEST_GROUP_DRAG_OVERVIEW=1 FIGMA_GTK_TEST_GROUP_DRAG_MEMBERS=1 run_check group-drawer-drag --self-test
FIGMA_GTK_TEST_GROUP_DRAG=1 FIGMA_GTK_TEST_GROUP_DRAG_PLAIN=1 run_check group-tab-drag --self-test
FIGMA_GTK_TEST_GROUP_DRAG=1 FIGMA_GTK_TEST_GROUP_DRAG_OVERVIEW=1 FIGMA_GTK_TEST_GROUP_DRAG_JOIN=1 run_check group-join-drag --self-test
FIGMA_GTK_TEST_GROUP_DRAG=1 FIGMA_GTK_TEST_GROUP_DRAG_OVERVIEW=1 FIGMA_GTK_TEST_GROUP_DRAG_MEMBERS=1 FIGMA_GTK_TEST_GROUP_DRAG_PLAIN=1 run_check group-extract-drag --self-test
FIGMA_GTK_TEST_GROUP_DRAG=1 FIGMA_GTK_TEST_GROUP_DRAG_OVERVIEW=1 FIGMA_GTK_TEST_GROUP_DRAG_MEMBERS=1 FIGMA_GTK_TEST_GROUP_DRAG_JOIN=1 run_check group-transfer-drag --self-test
FIGMA_GTK_TEST_TAB_CHROME=1 run_check tab-chrome --self-test
FIGMA_GTK_TEST_MIDDLE_CLICK=1 run_check middle-click --self-test
FIGMA_GTK_TEST_OVERVIEW_NEW_TAB=1 run_check overview-new-tab --self-test
FIGMA_GTK_TEST_DOWNLOADS=1 run_check downloads --self-test
FIGMA_GTK_TEST_HOME_CARD=1 run_check home --self-test
FIGMA_GTK_TEST_TAB_SLIDE=1 run_check tab-slide --render-test
FIGMA_GTK_TEST_RENDER_OVERVIEW=1 run_check overview --render-test
FIGMA_GTK_TEST_RENDER_OVERVIEW=1 FIGMA_GTK_TEST_RENDER_GROUPS=1 run_check group-previews --render-test
FIGMA_GTK_TEST_RENDER_OVERVIEW=1 FIGMA_GTK_TEST_RENDER_GROUPS=1 FIGMA_GTK_TEST_DRAWER_LAYOUT=1 run_check drawer-layout --render-test
FIGMA_GTK_TEST_RENDER_OVERVIEW=1 FIGMA_GTK_TEST_DRAG=1 run_check drag --render-test
FIGMA_GTK_TEST_RENDER_OVERVIEW=1 FIGMA_GTK_TEST_DRAG=1 FIGMA_GTK_TEST_HEADER_DRAG=1 run_check header-drag --render-test
FIGMA_GTK_TEST_RENDER_STATE=1 run_check render-state --render-test
FIGMA_GTK_TEST_COPY=1 run_check copy --render-test
FIGMA_GTK_TEST_CLIPBOARD=1 FIGMA_GTK_TEST_CURSOR=1 run_check render --render-test
GDK_SCALE=2 FIGMA_GTK_TEST_NARROW=1 FIGMA_GTK_TEST_CLIPBOARD=1 FIGMA_GTK_TEST_CURSOR=1 run_check render-scale2 --render-test
export FIGMA_GTK_AUTH_TEST_URL="http://127.0.0.1:$(cat "$check_dir/port")/profile?role=main"
FIGMA_GTK_TEST_ZANE_ACCOUNT=1 run_check zane-account --auth-test
export FIGMA_GTK_AUTH_TEST_URL="http://127.0.0.1:$(cat "$check_dir/port")/start/close"
run_check auth-close --auth-test
export FIGMA_GTK_AUTH_TEST_URL="http://127.0.0.1:$(cat "$check_dir/port")/start/blank"
run_check auth-blank --auth-test
export FIGMA_GTK_AUTH_TEST_URL="http://127.0.0.1:$(cat "$check_dir/port")/notifications"
FIGMA_GTK_TEST_NOTIFICATIONS=1 run_check notifications --auth-test
if [[ -z "${FIGMA_GTK_CHECKS:-}" || " $FIGMA_GTK_CHECKS " == *" notifications "* ]]; then
  rg -q '^Native notification received$' "$check_dir/notifications.log"
  rg -q '^Chromium notification received$' "$check_dir/notifications.log"
  printf 'Native and Chromium notification delivery: PASS\n'
fi
printf 'UI check artifacts: %s\n' "$check_dir"
