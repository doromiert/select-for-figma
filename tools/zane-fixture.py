#!/usr/bin/env python3
"""Offline Codex app-server fixture; never contacts Codex or a user account."""
import base64
import json
import os
import sys
import traceback
import time
from pathlib import Path

def failure(kind, value, tb):
    path = os.environ.get("FIGMA_GTK_TEST_ZANE_LOG")
    if path:
        Path(path).write_text("".join(traceback.format_exception(kind, value, tb)))
    else:
        traceback.print_exception(kind, value, tb)

sys.excepthook = failure

assert os.environ.get("FIGMA_GTK_PRIVATE_BUS") == "1"
if sys.argv[1:4] == ["mcp", "list", "--json"]:
    print(json.dumps([{"name":"fixture.figma","enabled":True},{"name":"other","enabled":True}]))
    sys.exit(0)
assert "shell_tool" in sys.argv and "plugins" in sys.argv
assert 'mcp_servers={"fixture.figma"={enabled=false,required=false},"other"={enabled=false,required=false}}' in sys.argv
thread, turn = "fixture-thread", "fixture-turn"
calls = ["read_comments", "inspect_file", "view_canvas", "point_at", "read_structure", "organize_file", "edit_design", "invalid_copy"]
if os.environ.get("FIGMA_GTK_TEST_ZANE_USER_INPUT"):
    calls[7:7]=["new_work","canvas_work"]
if os.environ.get("FIGMA_GTK_TEST_ZANE_SCRIPT"):
    calls.extend(["script_behavior","script_failure"])
if os.environ.get("FIGMA_GTK_TEST_ZANE_CANVAS_SCRIPT"):
    calls=["read_comments","inspect_file","view_canvas","point_at","read_structure","script_behavior","invalid_copy"]
bridge = False
work = False
index = 0
waiting_followup = False
quick_index = 0
quick_calls = [("list_pages",{}),("navigate",{"pageName":"Archive"}),("navigate",{"pageName":"Workbench"}),("find_nodes",{"query":"IE"}),("navigate", {"zoomFactor":2}), ("navigate", {"zoomFactor":0.5}), ("follow_user", {"enabled":True}), ("follow_user", {"enabled":False})]
if (os.environ.get("FIGMA_GTK_TEST_ZANE_ACTIONS") or os.environ.get("FIGMA_GTK_TEST_ZANE_USER_INPUT")):
    quick_calls.insert(0,("canvas_actions",{"actions":[{"operation":"pan_drag","dx":40,"dy":-30},{"operation":"zoom_wheel","dy":120},{"operation":"shortcut","shortcut":"fit_page"},{"operation":"pause","milliseconds":20}]}))
if os.environ.get("FIGMA_GTK_TEST_ZANE_USER_INPUT"):
    quick_calls=[("presence_mode",{"mode":"user"})]
elif os.environ.get("FIGMA_GTK_TEST_ZANE_USER_MODE"):
    quick_calls=[("presence_mode",{}),("presence_mode",{"mode":"user"}),("presence_mode",{}),("presence_mode",{"mode":"local"}),("presence_mode",{})]
else:
    quick_calls.insert(0,("presence_mode",{}))
followup_run = bool(os.environ.get("FIGMA_GTK_TEST_ZANE_FOLLOWUP")) and not Path(os.environ["FIGMA_GTK_TEST_ZANE_LOG"]+".left").exists()

def send(message):
    print(json.dumps(message), flush=True)

def call():
    args = {"x": 180, "y": 200} if calls[index] == "point_at" else {"pageId":"1:1"} if calls[index] == "read_structure" else {"operation":"rename","nodeId":"1:2","name":"Organized"} if calls[index] == "organize_file" else {"frameId":"1:2","changes":[{"operation":"set_text","nodeId":"1:3","text":"Version B"}]} if calls[index] == "edit_design" else {"copyId":"1:2","changes":[]} if calls[index] == "invalid_copy" else {}
    if calls[index] == "edit_design" and (os.environ.get("FIGMA_GTK_TEST_ZANE_ACTIONS") or os.environ.get("FIGMA_GTK_TEST_ZANE_USER_INPUT")):
        args["changes"] += [{"operation":"create_path","name":"Left eye","points":[{"x":35,"y":30},{"x":35,"y":36}]},{"operation":"create_path","name":"Right eye","points":[{"x":65,"y":30},{"x":65,"y":36}]},{"operation":"create_path","name":"Smile","points":[{"x":25,"y":55,"handleOut":{"x":0,"y":35}},{"x":75,"y":55,"handleIn":{"x":0,"y":35}}]}]
    if calls[index] == "new_work": args={"newFrame":{"name":"New work","width":200,"height":180},"changes":[{"operation":"create_text","text":"Fresh work"}]}
    if calls[index] == "canvas_work": args={"canvas":True,"changes":[{"operation":"create_path","points":[{"x":50,"y":50},{"x":70,"y":80}]}]}
    if calls[index] == "script_behavior":
        args={"mode":"design","frameId":"1:2","code":"await zane.key('Escape'); await zane.key('Escape'); await zane.key('P'); for (const [x,y] of [[20,20],[50,60],[80,20]]) await zane.click(x,y); await zane.drag(80,20,80,35,{alt:true}); await zane.key('Escape'); const state=await zane.state(); if(state.createdNodeIds.length!==1)throw Error('No new path'); return state;"}
    if calls[index] == "script_behavior" and os.environ.get("FIGMA_GTK_TEST_ZANE_CANVAS_SCRIPT"):
        args.pop("frameId")
        args["canvas"]=True
        args["code"]="const v=(await zane.state()).viewport; const sx=v.offsetX-50,sy=v.offsetY-30; " + args["code"].replace("click(x,y)","click(sx+x,sy+y)").replace("drag(80,20,80,35", "drag(sx+80,sy+20,sx+80,sy+35")
    if calls[index] == "script_failure": args={"code":"await zane.key('NoSuchKey');"}
    send({"id": f"tool-{index}", "method": "item/tool/call", "params": {"threadId": thread, "turnId": turn, "callId": str(index), "tool": "edit_design" if calls[index] in ("invalid_copy","new_work","canvas_work") else "script_behavior" if calls[index]=="script_failure" else calls[index], "arguments": args}})

def chat_action():
    if quick_index < len(quick_calls):
        name, args = quick_calls[quick_index]
        send({"id":f"quick-{quick_index}","method":"item/tool/call","params":{"threadId":thread,"turnId":turn,"tool":name,"arguments":args}})
    else:
        send({"id":"delegate","method":"item/tool/call","params":{"threadId":thread,"turnId":turn,"callId":"delegate","tool":"delegate_work","arguments":{"request":"read the comments"}}})

for line in sys.stdin:
    msg = json.loads(line)
    method, ident = msg.get("method"), msg.get("id")
    if method == "initialize":
        bridge = msg["params"]["clientInfo"]["name"].endswith("bridge")
        assert not bridge, "Production tools must not start a Figma MCP bridge"
        assert msg["params"]["capabilities"]["experimentalApi"]
        if not bridge and os.environ.get("FIGMA_GTK_TEST_ZANE_CANCEL"):
            marker = Path(os.environ["FIGMA_GTK_TEST_ZANE_LOG"] + ".started")
            if not marker.exists():
                marker.write_text("startup cancellation fixture")
                time.sleep(20)
        send({"id": ident, "result": {"userAgent": "offline fixture"}})
    elif method == "account/read":
        account = {"type":"chatgpt"}
        if os.environ.get("FIGMA_GTK_TEST_ZANE_RECOVER"):
            marker = Path(os.environ["FIGMA_GTK_TEST_ZANE_LOG"] + ".started")
            if not marker.exists():
                marker.write_text("sign-in recovery fixture")
                account = None
        send({"id":ident,"result":{"account":account,"requiresOpenaiAuth":True}})
    elif method == "model/list":
        send({"id":ident,"result":{"data":[
            {"id":"fixture-fast","model":"fixture-fast","displayName":"Fixture Fast","supportedReasoningEfforts":[{"reasoningEffort":"low"}]},
            {"id":"fixture-deep","model":"fixture-deep","displayName":"Fixture Deep","supportedReasoningEfforts":[{"reasoningEffort":"high"},{"reasoningEffort":"xhigh"}]}],"nextCursor":None}})
    elif method == "thread/start":
        assert msg["params"]["config"]["mcp_servers"]["fixture.figma"]["enabled"] is False
        assert msg["params"]["sandbox"] == "read-only"
        assert msg["params"]["ephemeral"]
        if not bridge:
            assert "0.05 seconds per character" in msg["params"]["developerInstructions"]
            assert "User mode means" in msg["params"]["developerInstructions"] and "visible to other users" in msg["params"]["developerInstructions"]
            names = set(t["name"] for t in msg["params"]["dynamicTools"])
            work = "delegate_work" not in names
            thread = "fixture-work" if work else "fixture-chat"
            assert names == ({"read_comments","inspect_file","view_canvas","point_at","read_structure","organize_file","edit_design"} | {"leave","navigate","follow_user","list_pages","find_nodes","speak","canvas_actions","presence_mode","script_behavior"} if work else {"read_comments","inspect_file","view_canvas","point_at","read_structure","leave","navigate","follow_user","list_pages","find_nodes","speak","canvas_actions","presence_mode","delegate_work"})
            if os.environ.get("FIGMA_GTK_TEST_ZANE_MODELS"):
                assert msg["params"]["model"] == ("fixture-deep" if work else "fixture-fast")
        send({"id": ident, "result": {"thread": {"id": thread}}})
    elif method in ("mcpServerStatus/list","mcpServer/tool/call"):
        raise AssertionError("Local editor actions must not use MCP")
    elif method == "turn/start":
        assert "comments" in msg["params"]["input"][0]["text"]
        if os.environ.get("FIGMA_GTK_TEST_ZANE_MODELS"):
            assert msg["params"]["model"] == ("fixture-deep" if work else "fixture-fast") and msg["params"]["effort"] == ("high" if work else "low")
        send({"id": ident, "result": {"turn": {"id": turn}}})
        if work:
            index = 0
            call()
        else:
            quick_index = 0 if (os.environ.get("FIGMA_GTK_TEST_ZANE_CURSOR") or os.environ.get("FIGMA_GTK_TEST_ZANE_USER_MODE") or os.environ.get("FIGMA_GTK_TEST_ZANE_USER_INPUT")) and not os.environ.get("FIGMA_GTK_TEST_ZANE_FOLLOWUP") else len(quick_calls)
            chat_action()
    elif method == "turn/steer":
        assert os.environ.get("FIGMA_GTK_TEST_ZANE_FOLLOWUP") and not bridge and work
        assert msg["params"]["threadId"] == thread and msg["params"]["expectedTurnId"] == turn
        request = msg["params"]["input"][0]["text"]
        send({"id":ident,"result":{"turnId":turn}})
        if "free to go" in request:
            send({"id":"leave","method":"item/tool/call","params":{"threadId":thread,"turnId":turn,"tool":"leave","arguments":{}}})
        else:
            assert "also" in request and waiting_followup
            waiting_followup = False; call()
    elif method == "turn/interrupt":
        send({"id": ident, "result": {}})
        send({"method": "turn/completed", "params": {"turn": {"id": turn, "status": "interrupted"}}})
    elif method is None and ident == "leave":
        assert msg["result"]["success"]
        Path(os.environ["FIGMA_GTK_TEST_ZANE_LOG"]+".left").write_text("leave acknowledged")
    elif method is None and ident == "speak":
        assert msg["result"]["success"]
        # The final text must not replace or repeat the timed tool-selected chunks.
        send({"method":"item/agentMessage/delta","params":{"delta":"This duplicate text must not be displayed."}})
        send({"method":"turn/completed","params":{"turn":{"id":turn,"status":"completed"}}})
    elif method is None and ident == "delegate":
        assert msg["result"]["success"]
        send({"method":"turn/completed","params":{"turn":{"id":turn,"status":"completed"}}})
    elif method is None and str(ident).startswith("quick-"):
        assert not work and not bridge, "Quick actions must stay in the Chat model"
        assert msg["result"]["success"], msg
        content = msg["result"]["contentItems"][0]["text"]
        name, args = quick_calls[quick_index]
        if name == "navigate":
            assert "design layers are unchanged" in content
        elif name == "list_pages":
            assert 'Workbench' in content and 'Archive' in content
        elif name == "find_nodes":
            assert 'IE' in content
        elif name == "canvas_actions":
            assert json.loads(content)["completed"] == 4
        elif name == "presence_mode":
            status=json.loads(content)
            user=bool(os.environ.get("FIGMA_GTK_TEST_ZANE_USER_INPUT")) or bool(os.environ.get("FIGMA_GTK_TEST_ZANE_USER_MODE")) and quick_index in (1,2)
            assert status["mode"] == ("user" if user else "local") and status["ready"]
            assert status["visibleToOthers"] == user
            if user: assert status["canEdit"]
        else:
            assert "following" in content
        quick_index += 1
        chat_action()
    elif method is None and str(ident).startswith("tool-"):
        result = msg["result"]
        if calls[index] == "invalid_copy":
            assert not result["success"] and "Original frames cannot be edited" in result["contentItems"][0]["text"]
        elif calls[index] == "script_failure":
            payload=json.loads(result["contentItems"][0]["text"]);assert not result["success"] and "unsupported key" in payload["error"], payload
        else:
            assert result["success"], result
        content = result["contentItems"][0]
        if index in (0, 1):
            payload = json.loads(content["text"])
            if index == 0 and os.environ.get("FIGMA_GTK_TEST_ZANE_API"):
                assert payload["total"] == 2 and payload["nextOffset"] == -1
                assert payload["comments"][1]["parent_id"] == "root"
            else:
                assert any("Alice" in c and "alignment" in c for c in payload["comments"])
                assert "not a complete" in payload["commentCoverage"]
        elif index == 2:
            assert content["type"] == "inputImage"
            assert base64.b64decode(content["imageUrl"].split(",", 1)[1]).startswith(b"\x89PNG")
        elif index == 3:
            assert "unchanged" in content["text"]
        elif calls[index] == "edit_design":
            payload = json.loads(content["text"])
            assert payload["copyId"] != "1:2" and payload["originalUnchanged"]
            if (os.environ.get("FIGMA_GTK_TEST_ZANE_ACTIONS") or os.environ.get("FIGMA_GTK_TEST_ZANE_USER_INPUT")):
                assert len(payload["createdNodeIds"]) == 3
            if os.environ.get("FIGMA_GTK_TEST_ZANE_USER_INPUT"): assert payload["source"] == "standard_input" and payload["copied"]
        elif calls[index] == "script_behavior":
            payload=json.loads(content["text"]);assert payload["ok"] and len(payload["createdNodeIds"])==1, payload
            assert payload.get("rootId") != "1:2" and payload["originalUnchanged"], payload
            if os.environ.get("FIGMA_GTK_TEST_ZANE_CANVAS_SCRIPT"): assert not payload["copied"] and "copyId" not in payload
        elif calls[index] in ("new_work","canvas_work"):
            payload=json.loads(content["text"]);assert not payload["copied"] and "copyId" not in payload
        index += 1
        if index < len(calls):
            if followup_run and index == 1:
                waiting_followup = True
            else: call()
        else:
            if os.environ.get("FIGMA_GTK_TEST_ZANE_SPEECH"):
                messages=["Zane read two comments.","Done ✅ "+"This meaningful phrase is longer than sixty characters, and keeps every word."]
                send({"id":"speak","method":"item/tool/call","params":{"threadId":thread,"turnId":turn,"tool":"speak","arguments":{"messages":messages}}})
                continue
            for chunk in ("Zane read two comments. ","Alice requested alignment; Bob replied."):
                send({"method":"item/agentMessage/delta","params":{"delta":chunk}})
            if not followup_run:
                send({"method": "turn/completed", "params": {"turn": {"id": turn, "status": "completed"}}})
