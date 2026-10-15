import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';
import assert from 'node:assert/strict';
import {Worker as ThreadWorker} from 'node:worker_threads';

// Exercise the actual browser worker program on another thread, not a mock
// interpreter that only recognizes the expected drawing script.
function installScriptWorker(context) {
  const sources=new Map();let serial=0;
  context.Blob=class {constructor(parts){this.source=parts.join('');}};
  context.URL={createObjectURL(blob){const id='blob:fixture-'+(++serial);sources.set(id,blob.source);return id;},revokeObjectURL(id){sources.delete(id);}};
  context.Worker=class {
    constructor(url) {
      this.thread=new ThreadWorker(`
        const {parentPort,workerData}=require('node:worker_threads');
        const vm=require('node:vm');
        const scope={postMessage:value=>parentPort.postMessage(value),setTimeout,clearTimeout,setInterval,clearInterval};
        const context=vm.createContext(scope);
        parentPort.on('message',data=>scope.onmessage?.({data}));
        try {vm.runInContext(workerData,context);}catch(error){parentPort.postMessage({fixtureError:error.message});}
      `,{eval:true,workerData:sources.get(url)});
      this.thread.on('message',data=>data.fixtureError?this.onerror?.({message:data.fixtureError}):this.onmessage?.({data}));
      this.thread.on('error',error=>this.onerror?.({message:error.message}));
    }
    postMessage(data){this.thread.postMessage(data);}
    terminate(){this.thread.terminate();}
  };
  return ()=>sources.size;
}

const messages=[];
const window={__selectZaneSend:value=>messages.push(JSON.parse(value.slice('select-zane-account:'.length)))};
const context={window,crypto:webcrypto,innerWidth:1200,innerHeight:900,
  location:{pathname:'/design/fixture'},document:{createElement:()=>({style:{}}),querySelector:()=>window.fixtureAnchor,body:{append(node){window.fixtureAnchor=node;}}},queueMicrotask,setTimeout,clearTimeout};
vm.createContext(context);
context.setInterval=setInterval;context.clearInterval=clearInterval;
const workerURLs=installScriptWorker(context);
let fixture=readFileSync(new URL('../tools/native-cursor-fixture.js',import.meta.url),'utf8');
// The peer uses the editor's real authenticated session object, not a cursor
// React root. Represent that separate boundary with a fixture transport.
fixture=fixture.replace('const r=load(1), c=load(2);', "const r=load(1), c=load(2), m=load(9); function chat(){m.api?.sendChatMessage('','');}");
fixture=fixture.replace('    1:(module)', "    9:(module)=>{module.exports={api:{sendChatMessage(text){window.fixtureChat=text;},observeUser(id){window.fixtureObserving=id;}}};},\n    1:(module)");

fixture=fixture.replace('    1:(module)', `    10:(module)=>{const signature='initialOptions: extractPageLocales:';module.exports={initialOptions:()=>window.fixtureOptions,extractPageLocales(){}};},
    11:(module)=>{module.exports={api:{url:(parts,...values)=>parts.reduce((all,part,i)=>all+part+(values[i]??''),''),
      async get(){return {data:{meta:window.fixtureRoles}};},async post(url,data){window.fixtureInvites.push({url,data});window.fixtureRoles.push({user:{email:data.emails[0]},level:data.level});return {data:{meta:{}}};}}};},
    12:(module,exports,load)=>{const t=load(11);const signatures='Invited Emails /api/invites/resend';function invite(){t.api.post(t.api.url\`/api/invites\`,{});}module.exports={invite,signatures};},
    1:(module)`);
window.fixtureOptions={user_data:{id:'main',email:'main@example.test'},editing_file:{team:{starter_team:true}}};
window.fixtureRoles=[];window.fixtureInvites=[];
vm.runInContext(fixture,context);
vm.runInContext(readFileSync(new URL('../data/zane-native.js',import.meta.url),'utf8'),context);
window.__selectZanePeer({x:5000,y:-2000,zoom:0.5,text:'Organizing the file…'});
assert.equal(window.fixturePanX,5000); assert.equal(window.fixturePanY,-2000);
assert.equal(window.fixtureZoom,0.5); assert.equal(window.fixtureChat,'Organizing the file…');
assert.equal(messages.at(-1).ready,true); assert.equal(messages.at(-1).x,600); assert.equal(messages.at(-1).y,450);
const longReply='Zane read two comments. '+('✅ café 🙂 '.repeat(20));
window.__selectZanePeer({x:5000,y:-2000,zoom:0.5,text:longReply});
assert.equal(window.fixtureChat,longReply,'Timed message text is not silently truncated');
const exactReply='🙂'.repeat(60);window.__selectZanePeer({x:5000,y:-2000,zoom:0.5,text:exactReply});
assert.equal(window.fixtureChat,exactReply,'Exactly 60 Unicode characters are kept intact');
console.log('Account chat preserves message text, including Unicode: PASS');
window.__selectZanePeer({x:5000,y:-2000,zoom:0.5,text:'Following',followUser:true,userSession:42});
assert.equal(window.fixtureObserving,42);
console.log('Account session chat, native follow, canvas navigation and pointer projection: PASS');

await window.__selectZanePeer({x:9999,y:9999,zoom:10,text:'Here!',autonomous:true,summon:{id:1,pageId:'2:1',x:321,y:432,zoom:0.8}});
assert.equal(window.fixtureCurrentPage,'2:1');assert.equal(window.fixturePanX,321);assert.equal(window.fixturePanY,432);assert.equal(window.fixtureZoom,0.8);
await window.__selectZanePeer({x:-9999,y:-9999,zoom:10,text:'Next chunk',autonomous:true,summon:{id:1,pageId:'2:1',x:321,y:432,zoom:0.8}});
assert.equal(window.fixturePanX,321,'Speech updates do not reapply a summon or reset its camera');
await window.__selectZanePeer({x:9999,y:9999,zoom:10,text:'Here again!',autonomous:true,summon:{id:2,pageId:'1:1',x:123,y:234,zoom:0.5}});
assert.equal(window.fixtureCurrentPage,'1:1');assert.equal(window.fixturePanX,123);
let release;const gate=new Promise(resolve=>{release=resolve;});
window.fixturePageReady=()=>window.fixtureCurrentPage==='2:1'?gate:undefined;
const old=window.__selectZanePeer({x:0,y:0,zoom:1,text:'Old call',autonomous:true,summon:{id:3,pageId:'2:1',x:1,y:2,zoom:1}});
await window.__selectZanePeer({x:0,y:0,zoom:1,text:'Newest call',autonomous:true,summon:{id:4,pageId:'1:1',x:777,y:888,zoom:1}});
release();await old;delete window.fixturePageReady;
assert.equal(window.fixtureCurrentPage,'1:1');assert.equal(window.fixturePanX,777);assert.equal(window.fixtureChat,'Newest call','A stale cross-page summon cannot override the newest call');
console.log('Cross-page summons bring the account to the caller; newest call wins: PASS');
window.fixturePageReady=()=>{window.fixtureCurrentPage='1:1';throw Error('Cold page loader');};
const retry={x:0,y:0,zoom:1,text:'Retry summon',autonomous:true,summon:{id:5,pageId:'2:1',x:42,y:53,zoom:0.6}};
await window.__selectZanePeer(retry);
assert.equal(messages.at(-1).ready,false);assert.equal(messages.at(-1).stage,'page');assert.equal(messages.at(-1).summonId,5);
delete window.fixturePageReady;
await window.__selectZanePeer(retry);
assert.equal(messages.at(-1).ready,true,'A rejected summon promise must be retried');assert.equal(window.fixtureCurrentPage,'2:1');assert.equal(window.fixturePanX,42);assert.equal(messages.at(-1).canEdit,true);
const options=window.fixtureOptions;window.fixtureOptions=undefined;
await window.__selectZanePeer({...retry,summon:{...retry.summon,id:6}});
assert.equal(messages.at(-1).ready,false);assert.equal(messages.at(-1).stage,'identity');assert.equal(messages.at(-1).summonId,6,'Failures before page switching still report the requested summon');
window.fixtureOptions=options;
await window.__selectZanePeer({...retry,summon:{...retry.summon,id:6}});assert.equal(messages.at(-1).ready,true);
await window.__selectZaneNavigate({pageName:'Workbench'});
console.log('User-mode cold-load retries recover; early failures report tagged connection status: PASS');
// Reset the private fixture to its initial unloaded-page scenario.
delete window.fixtureNodes['2:1'];window.fixtureZoom=0.5;window.fixtureDispatches=0;
let pages=window.__selectZanePages();assert.equal(pages.pages.length,2);assert.equal(pages.currentPageId,'1:1');
assert.equal(window.fixtureNodes['2:1'],undefined,'Target page starts unloaded');
let navigation=await window.__selectZaneNavigate({pageName:'archive'});
assert.equal(navigation.ok,true);assert.equal(window.fixtureCurrentPage,'2:1');assert.equal(window.fixtureDispatches,1);
navigation=await window.__selectZaneNavigate({pageName:'Workbench',nodeIds:['1:2'],zoomFactor:2});
assert.equal(navigation.ok,true);assert.equal(window.fixturePageTarget,'1:1');
assert.deepEqual(Array.from(window.fixtureFocusedIds),['1:2']);assert.equal(window.fixtureZoom,1);
navigation=await window.__selectZaneNavigate({pageId:'9:99'});assert.equal(navigation.ok,false);assert.equal(window.fixtureCurrentPage,'1:1');
navigation=await window.__selectZaneNavigate({nodeIds:['1:99'],zoomFactor:2});assert.equal(navigation.ok,false);
navigation=await window.__selectZaneNavigate({nodeIds:['1:2'],zoomFactor:100});assert.equal(navigation.ok,false);assert.equal(window.fixtureZoom,1);
navigation=await window.__selectZaneNavigate({nodeIds:['1:1']});assert.equal(navigation.ok,false);
navigation=await window.__selectZaneNavigate({zoomFactor:0.5});assert.equal(navigation.ok,true);assert.equal(window.fixtureZoom,0.5);
navigation=await window.__selectZaneNavigate({fit:'page'});assert.equal(navigation.ok,true);assert.equal(window.fixturePanX,600);assert.equal(window.fixturePanY,500);
const before=window.fixturePanX,zoom=window.fixtureZoom;
navigation=await window.__selectZaneNavigate({pan:{x:100,y:-50}});assert.equal(navigation.ok,true);assert.equal(window.fixturePanX,before+100/zoom);
const matches=window.__selectZaneFind({query:'ie'});assert.equal(matches.ok,true);assert.equal(matches.matches[0].id,'1:2');
const structure=await window.__selectZaneStructure({});assert.equal(structure.ok,true);assert.equal(structure.page.children[0].name,'IE');
let active=true;window.fixturePageReady=()=>{active=false;};window.fixtureFocusedIds=[];
navigation=await window.__selectZaneNavigate({pageId:'2:1',nodeIds:['1:2']},()=>active);
assert.equal(navigation.ok,false);assert.equal(window.fixtureFocusedIds.length,0);delete window.fixturePageReady;
console.log('Unloaded page switches, local name/ID resolution, search, fit, pan, zoom and cancellation: PASS');
await window.__selectZaneNavigate({pageName:'Workbench'});
const originalBox=JSON.stringify(window.fixtureNodes['1:2'].absoluteBoundingBox);
let organized=await window.__selectZaneOrganize({operation:'rename',nodeId:'1:2',name:'Organized IE'});
assert.equal(organized.ok,true);assert.equal(window.fixtureNodes['1:2'].name,'Organized IE');assert.equal(window.fixtureCommits,1);assert.equal(window.fixtureUndo.length,1);
assert.equal(JSON.stringify(window.fixtureNodes['1:2'].absoluteBoundingBox),originalBox);
organized=await window.__selectZaneOrganize({operation:'reorder_page',nodeId:'2:1',index:0});assert.equal(organized.ok,true);assert.equal(window.fixtureNodes['0:0'].childrenNodes[0].guid,'2:1');
organized=await window.__selectZaneOrganize({operation:'move_frame_to_page',nodeId:'1:2',pageId:'2:1'});assert.equal(organized.ok,true);assert.equal(window.fixtureNodes['1:2'].containingCanvas,'2:1');assert.equal(JSON.stringify(window.fixtureNodes['1:2'].absoluteBoundingBox),originalBox);
const source=window.fixtureNodes['1:2'],sourceText=window.fixtureNodes['1:3'];
const beforeOriginal=JSON.stringify({name:source.name,box:source.absoluteBoundingBox,text:sourceText.characters});
let edited=await window.__selectZaneEditCopy({frameId:'1:2',changes:[{operation:'set_text',nodeId:'1:3',text:'Version B'},{operation:'set_fill',color:'#abcdef'}]});
assert.equal(edited.ok,true);assert.equal(edited.originalUnchanged,true);assert.notEqual(edited.copyId,'1:2');
assert.equal(window.fixtureNodes[edited.nodeIdMap['1:3']].characters,'Version B');
assert.equal(JSON.stringify({name:source.name,box:source.absoluteBoundingBox,text:sourceText.characters}),beforeOriginal);
const copyId=edited.copyId;
edited=await window.__selectZaneEditCopy({copyId:'1:2',allowedCopies:['1:2'],changes:[]});assert.equal(edited.ok,false,'An original is never a valid continuation copy');
edited=await window.__selectZaneEditCopy({copyId,allowedCopies:[copyId],changes:[{operation:'set_text',nodeId:'1:3',text:'Overwrite original'}]});assert.equal(edited.ok,false);assert.equal(sourceText.characters,'Original design');
edited=await window.__selectZaneEditCopy({copyId,allowedCopies:[copyId],changes:[{operation:'create_text',text:'Copy only'},{operation:'set_auto_layout',layout:'HORIZONTAL',gap:12,padding:8}]});assert.equal(edited.ok,true);assert.equal(window.fixtureNodes[copyId].stackMode,'HORIZONTAL');assert.equal(window.fixtureNodes[copyId].stackLeftPadding,8);
assert(!source.childrenNodes.some(node=>node.characters==='Copy only'));
console.log('Native duplicate-only design edits, source mapping, continuation and original protection: PASS');
const commitsBefore=window.fixtureCommits;
edited=await window.__selectZaneEditCopy({copyId,allowedCopies:[copyId],changes:[{operation:'set_fill',color:'#123456'}]});
assert.equal(edited.ok,true,JSON.stringify(edited));assert.equal(window.fixtureCommits,commitsBefore+1,'Commit exactly once inside the native edit transaction');
window.fixtureFailCommit=true;
edited=await window.__selectZaneEditCopy({copyId,allowedCopies:[copyId],changes:[{operation:'set_fill',color:'#654321'}]});
assert.equal(edited.ok,false);assert.equal(edited.code,'fixture-save-failed','A real commit failure must still be reported');
assert.equal(edited.rootId,copyId);assert.equal(sourceText.characters,'Original design');window.fixtureFailCommit=false;
console.log('Navigation has no commit method; real edit commits run inside transactions and genuine save failures propagate: PASS');
// A smiley is three editable native pen paths: two eyes and a cubic mouth.
const smiley=[
  {operation:'create_path',name:'Left eye',points:[{x:35,y:30},{x:35,y:36}],color:'#9747ff',strokeWidth:3},
  {operation:'create_path',name:'Right eye',points:[{x:65,y:30},{x:65,y:36}],color:'#9747ff',strokeWidth:3},
  {operation:'create_path',name:'Smile',points:[{x:25,y:55,handleOut:{x:0,y:35}},{x:75,y:55,handleIn:{x:0,y:35}}],color:'#9747ff',strokeWidth:3}
];
edited=await window.__selectZaneEditCopy({frameId:'1:2',changes:[{operation:'set_auto_layout',layout:'NONE'},...smiley]});
assert.equal(edited.ok,true);assert.equal(edited.createdNodeIds.length,3);assert.equal(edited.originalUnchanged,true);
const mouth=window.fixtureNodes[edited.createdNodeIds[2]];
assert.equal(mouth.type,'VECTOR');assert.equal(mouth.parentNode.guid,edited.copyId);assert.equal(mouth.strokeWeight,3);assert.equal(mouth.fills.length,0);
assert.equal(mouth.strokes[0].color.r,151/255);assert.equal(mouth.strokes[0].color.b,1);
assert.deepEqual(JSON.parse(JSON.stringify(mouth.fixtureNetwork.segments)),[{start:0,end:1,tangentStart:{x:0,y:35},tangentEnd:{x:0,y:35}}]);
const pathCopy=edited.copyId;
edited=await window.__selectZaneEditCopy({copyId:pathCopy,allowedCopies:[pathCopy],changes:[{operation:'create_path',closed:true,points:[{x:0,y:0},{x:10,y:0},{x:10,y:10}]}]});
assert.equal(edited.ok,true);assert.equal(window.fixtureNodes[edited.createdNodeIds[0]].fixtureNetwork.segments.at(-1).end,0);
let countNodes=Object.keys(window.fixtureNodes).length;
for(const bad of [{points:[{x:NaN,y:0},{x:0,y:1}]},{points:[{x:0,y:0,handleIn:{x:Infinity,y:1}},{x:1,y:1}]},{points:[{x:0,y:0}]},{strokeWidth:0,points:[{x:0,y:0},{x:1,y:1}]}]) {
  edited=await window.__selectZaneEditCopy({frameId:'1:2',changes:[{operation:'create_path',...bad}]});assert.equal(edited.ok,false);
}
assert.equal(Object.keys(window.fixtureNodes).length,countNodes,'Bad geometry is rejected before duplication');
edited=await window.__selectZaneEditCopy({copyId:pathCopy,allowedCopies:[pathCopy],changes:[{operation:'create_path',parentId:'1:2',points:[{x:0,y:0},{x:1,y:1}]}]});assert.equal(edited.ok,false);
assert.equal(source.childrenNodes.length,1);assert.equal(sourceText.characters,'Original design');
console.log('Smiley pen paths, Bezier handles, closed paths, stroke styling and duplicate protection: PASS');
// New frames and unframed artwork do not need (or create) duplicates.
const reuse=await window.__selectZaneEditCopy({frameId:'1:2',changes:[{operation:'set_text',nodeId:'1:3',text:'Reused workspace'}]});
assert.equal(reuse.rootId,pathCopy);assert.equal(reuse.copied,false);assert.equal(sourceText.characters,'Original design');
const blank=await window.__selectZaneTool('edit_design',{newFrame:{width:200,height:180,name:'Fresh'},changes:[{operation:'create_text',text:'New work'}]});
assert.equal(blank.ok,true);assert.equal(blank.copied,false);assert.equal(window.fixtureNodes[blank.rootId].name,'Fresh');
edited=await window.__selectZaneTool('edit_design',{rootId:blank.rootId,changes:[{operation:'set_fill',color:'#ffcc00'}]});assert.equal(edited.ok,true);assert.equal(edited.rootId,blank.rootId);assert.equal(edited.copied,false);
const fresh=window.fixtureCreateNode('FRAME');window.fixtureNodes[window.fixtureCurrentPage].appendChild(fresh);
edited=await window.__selectZaneTool('edit_design',{frameId:fresh.guid,changes:[{operation:'create_rectangle',name:'Direct new frame'}]});assert.equal(edited.ok,true);assert.equal(edited.rootId,fresh.guid);assert.equal(edited.copied,false);
const outside=await window.__selectZaneTool('edit_design',{canvas:true,changes:[{operation:'create_path',name:'Unframed smile',points:[{x:0,y:0},{x:10,y:10}]}]});assert.equal(outside.ok,true);assert.equal(outside.copied,false);
const art=outside.createdNodeIds[0];assert.equal(window.fixtureNodes[art].parentNode.type,'CANVAS');
edited=await window.__selectZaneTool('edit_design',{rootId:art,changes:[{operation:'move',x:55,y:66}]});assert.equal(edited.ok,true);assert.equal(window.fixtureNodes[art].x,55);
edited=await window.__selectZaneTool('edit_design',{canvas:true,changes:[{operation:'set_text',nodeId:'1:3',text:'Wrong scope'}]});assert.equal(edited.ok,false);assert.equal(sourceText.characters,'Original design');
console.log('New frames, user-created fresh frames, unframed work and workspace reuse avoid copies: PASS');
// In user mode the scene helper must never call the source clone API.
const originalClone=source.clone;source.clone=()=>{throw Error('Native clone forbidden in user mode');};
window.__selectZaneBeginDesignSession('input-test');
const gui=[];
const inputDuplicate=event=>{
  gui.push(event);
  if(event.operation==='click') window.fixtureSelectedIds=[source.guid];
  if(event.shortcut==='duplicate') {const node=window.fixtureNodes[originalClone()];source.parentNode.appendChild(node);window.fixtureSelectedIds=[node.guid];}
  queueMicrotask(()=>window.__selectZaneInputAck(event.inputId,{ok:true}));
};
edited=await window.__selectZaneTool('edit_design',{designSession:'input-test',userInput:true,inputLease:'input-lease',frameId:'1:2',changes:[{operation:'set_text',nodeId:'1:3',text:'Input copy'}]},()=>true,inputDuplicate);
assert.equal(edited.ok,true);assert.equal(edited.source,'standard_input');assert.equal(edited.copied,true);assert.notEqual(edited.rootId,source.guid);
assert(gui.some(event=>event.operation==='click'));assert(gui.some(event=>event.shortcut==='duplicate'));assert(gui.every(event=>event.inputLease==='input-lease'));
assert.equal(sourceText.characters,'Original design');source.clone=originalClone;
console.log('User-mode duplication uses verified selection and keyboard input, with no native-clone fallback: PASS');
window.fixtureDisablePathWriter=true;
const penInputs=[];let drawing=false;
const inputPen=event=>{
  assert(Number.isFinite(event.dx)&&Number.isFinite(event.dy),'Browser input packets require explicit finite deltas, including shortcuts');
  penInputs.push(event);
  if(event.shortcut==='escape') {drawing=false;window.fixturePenFinish();window.fixtureSelectedIds=[];}
  if(event.shortcut==='pen_tool') drawing=true;
  if(event.operation==='frame_drag') {const node=window.fixtureCreateNode('FRAME');window.fixtureNodes[window.fixtureCurrentPage].appendChild(node);window.fixtureSelectedIds=[node.guid];}
  if(event.operation==='click'&&drawing) window.fixturePenClick(event.x,event.y);
  queueMicrotask(()=>window.__selectZaneInputAck(event.inputId,{ok:true}));
};
edited=await window.__selectZaneTool('edit_design',{designSession:'input-test',userInput:true,inputLease:'pen-lease',newFrame:{width:100,height:100},changes:smiley},()=>true,inputPen);
assert.equal(edited.ok,true,JSON.stringify(edited));assert.equal(edited.createdNodeIds.length,3);
for(const id of edited.createdNodeIds) {const node=window.fixtureNodes[id];assert.equal(node.parentNode.guid,edited.rootId);assert(node.fixtureInputPoints.length>=2);assert.equal(node.fixtureNetwork,undefined);}
assert(window.fixtureNodes[edited.createdNodeIds[2]].fixtureInputPoints.length>2,'Bezier mouth is traced through Pen clicks');
assert(penInputs.some(event=>event.shortcut==='pen_tool'));assert(penInputs.every((event,i)=>!i||event.sequence>penInputs[i-1].sequence),'The frame and every path share one increasing input sequence');
assert.equal(sourceText.characters,'Original design');
const beforeOutside=penInputs.length;
const outsideWorkspace=await window.__selectZaneTool('edit_design',{designSession:'input-test',userInput:true,inputLease:'pen-lease',rootId:edited.rootId,changes:[{operation:'create_path',points:[{x:-100,y:0},{x:10,y:10}]}]},()=>true,inputPen);
assert.equal(outsideWorkspace.code,'pen-outside-workspace');assert.equal(penInputs.length,beforeOutside,'Reject out-of-workspace drawing before sending any input');
window.fixtureDisablePathWriter=false;
console.log('Collaborator-mode Pen drawing works with a broken private codec and preserves originals: PASS');
const behaviorCode=`
  if (typeof window !== 'undefined' || typeof fetch !== 'undefined' || typeof indexedDB !== 'undefined' || typeof eval !== 'undefined' || typeof Function !== 'undefined') throw Error('worker isolation');
  await zane.key('Escape');await zane.key('Escape');await zane.key('P');
  for (const [x,y] of [[20,20],[50,60],[80,20]]) await zane.click(x,y);
  await zane.key('Escape');const result=await zane.state();
  if(result.createdNodeIds.length!==1)throw Error('drawing not created');
  return result.createdNodeIds;
`;
const scriptInputs=[];let pen=false;
const behaviorInput=event=>{
  scriptInputs.push(event);
  if(event.shortcut==='key:Escape'){pen=false;window.fixturePenFinish();window.fixtureSelectedIds=[];}
  if(event.shortcut==='key:P')pen=true;
  if(event.operation==='click'&&pen)window.fixturePenClick(event.x,event.y);
  queueMicrotask(()=>window.__selectZaneInputAck(event.inputId,{ok:true}));
};
for(const userInput of [false,true]){
  const behavior=await window.__selectZaneTool('script_behavior',{mode:'design',designSession:'input-test',rootId:edited.rootId,userInput,inputLease:'script-lease',code:behaviorCode},()=>true,behaviorInput);
  assert.equal(behavior.ok,true,JSON.stringify(behavior));assert.equal(behavior.createdNodeIds.length,1);
  assert.equal(window.fixtureNodes[behavior.createdNodeIds[0]].parentNode.guid,edited.rootId);
}
const badScript=await window.__selectZaneTool('script_behavior',{code:'this is not valid javascript'},()=>true,behaviorInput);
assert.equal(badScript.ok,false);assert.equal(badScript.step,'compile');
const callsBefore=scriptInputs.length;
const navigationScript=await window.__selectZaneTool('script_behavior',{code:"await zane.key('P');"},()=>true,behaviorInput);
assert.equal(navigationScript.ok,false);assert.match(navigationScript.status,/unsupported key/);assert.equal(scriptInputs.length,callsBefore);
let scriptAlive=true;setTimeout(()=>scriptAlive=false,100);
const cancelledScript=await window.__selectZaneTool('script_behavior',{code:'while(true) {}'},()=>scriptAlive,behaviorInput);
assert.equal(cancelledScript.ok,false);assert.match(cancelledScript.status,/cancelled/);assert.equal(workerURLs(),0);
const blockedScript=await window.__selectZaneTool('script_behavior',{mode:'design',rootId:edited.rootId,code:'await zane.click(-1,0);'},()=>true,behaviorInput);
assert.equal(blockedScript.ok,false);assert.match(blockedScript.status,/outside editable/);assert.equal(scriptInputs.length,callsBefore);
assert.equal(sourceText.characters,'Original design');
console.log('Model-authored scripts, loops, state feedback, both presence modes, syntax errors, cancellation and workspace boundaries: PASS');
// Empty-canvas scripts must not resolve Figma's private mutation APIs merely
// to prepare their workspace. The normal fixture previously hid this failure.
const canvasWindow={};
const canvasContext={...context,window:canvasWindow,document:{createElement:()=>({style:{}}),querySelector:()=>canvasWindow.fixtureAnchor,body:{append(node){canvasWindow.fixtureAnchor=node;}}},location:{pathname:'/design/no-transactions'}};
vm.createContext(canvasContext);installScriptWorker(canvasContext);
vm.runInContext(fixture.replaceAll('select-reference-make-node','removed-transaction-binding'),canvasContext);
vm.runInContext(readFileSync(new URL('../data/zane-native.js',import.meta.url),'utf8'),canvasContext);
let canvasPen=false;
const canvasInputScript=event=>{
  if(event.shortcut==='key:Escape'){canvasPen=false;canvasWindow.fixturePenFinish();canvasWindow.fixtureSelectedIds=[];}
  if(event.shortcut==='key:P')canvasPen=true;
  if(event.operation==='click'&&canvasPen)canvasWindow.fixturePenClick(event.x,event.y);
  queueMicrotask(()=>canvasWindow.__selectZaneInputAck(event.inputId,{ok:true}));
};
for(const userInput of [false,true]) {
  const drawn=await canvasWindow.__selectZaneTool('script_behavior',{mode:'design',userInput,code:behaviorCode},()=>true,canvasInputScript);
  assert.equal(drawn.ok,true,JSON.stringify(drawn));assert.equal(drawn.createdNodeIds.length,1);
  assert.equal(canvasWindow.fixtureNodes[drawn.createdNodeIds[0]].parentNode.type,'CANVAS');
}
const deniedExisting=await canvasWindow.__selectZaneTool('script_behavior',{mode:'design',frameId:'1:2',code:behaviorCode},()=>true,canvasInputScript);
assert.equal(deniedExisting.ok,false);assert.match(deniedExisting.step,/prepare workspace.*duplicate/);
assert(deniedExisting.error,'Preserve the specific preparation error');
console.log('Fresh drawings run without private transaction bindings; protected-frame failures retain the underlying reason: PASS');
const nested=window.fixtureCreateNode('FRAME');source.appendChild??=node=>{node.parentNode=source;node.containingCanvas=source.containingCanvas;source.childrenNodes.push(node);};source.appendChild(nested);
window.__selectZaneBeginDesignSession('nested-test');
const parentChildren=source.childrenNodes.map(node=>node.guid);
edited=await window.__selectZaneTool('edit_design',{designSession:'nested-test',frameId:nested.guid,changes:[{operation:'set_fill',color:'#ff8800'}]});
assert.equal(edited.ok,true);assert.equal(edited.originalId,nested.guid,'Prepare the nearest frame instead of duplicating a larger containing frame');
assert.equal(window.fixtureNodes[edited.rootId].parentNode.type,'CANVAS');assert.deepEqual(source.childrenNodes.map(node=>node.guid),parentChildren);
console.log('Nested edits copy the smallest containing frame and preserve the original container: PASS');
const inputs=[];
const input=event=>{inputs.push(event);queueMicrotask(()=>window.__selectZaneInputAck(event.inputId,{ok:true}));};
let script=await window.__selectZaneTool('canvas_actions',{actions:[{operation:'pan_drag',dx:40,dy:-30},{operation:'zoom_wheel',dy:120},{operation:'shortcut',shortcut:'fit_page'},{operation:'pause',milliseconds:1}]},()=>true,input);
assert.equal(script.ok,true);assert.equal(script.completed,4);assert.deepEqual(inputs.map(event=>event.operation),['pan_drag','zoom_wheel','shortcut']);
assert.equal(inputs[0].x,600);assert.equal(inputs[0].y,450);assert.equal(inputs[0].dx,40);
for(const bad of [{operation:'shortcut',shortcut:'Delete'},{operation:'shortcut',shortcut:'pen_tool'},{operation:'zoom_wheel',dy:Infinity},{operation:'pause',milliseconds:1001},{operation:'pan_drag',dx:NaN,dy:0},{operation:'click',x:0,y:0}]) {
  const before=inputs.length;script=await window.__selectZaneCanvasActions({actions:[{operation:'shortcut',shortcut:'zoom_in'},bad]},()=>true,input);assert.equal(script.ok,false);assert.equal(inputs.length,before,'Validate all commands before replay');
}
script=await window.__selectZaneCanvasActions({actions:[{operation:'pan_drag',dx:2000,dy:0}]},()=>true,input);assert.equal(script.ok,false);
context.document.activeElement={closest:()=>({})};
script=await window.__selectZaneCanvasActions({actions:[{operation:'shortcut',shortcut:'fit_page'}]},()=>true,input);assert.equal(script.ok,false);assert.match(script.status,/typing/);delete context.document.activeElement;
let running=true;
script=await window.__selectZaneCanvasActions({actions:[{operation:'shortcut',shortcut:'fit_page'},{operation:'shortcut',shortcut:'zoom_in'}]},()=>running,event=>{running=false;window.__selectZaneInputAck(event.inputId,{ok:true});});assert.equal(script.ok,false);assert.equal(script.completed,0);
script=await window.__selectZaneCanvasActions({actions:[{operation:'shortcut',shortcut:'fit_page'},{operation:'shortcut',shortcut:'zoom_in'}]},()=>true,event=>window.__selectZaneInputAck(event.inputId,{ok:false,status:'Request cancelled.'}));assert.equal(script.ok,false);assert.equal(script.completed,0);
assert.equal(sourceText.characters,'Original design');
console.log('Canvas scripts: ordered native inputs, geometry bounds, preflight, typing guard and cancellation: PASS');
window.__selectZanePeer({x:1,y:2,zoom:1,text:'Surveying',autonomous:true});
await window.__selectZaneAccountTool(12,'navigate',{pageName:'Workbench',center:{x:44,y:55},zoomFactor:0.5});assert.equal(messages.at(-1).ok,true);
const ownX=window.fixturePanX,ownY=window.fixturePanY,ownZoom=window.fixtureZoom;
window.__selectZanePeer({x:9999,y:-3000,zoom:10,text:'Found it',autonomous:true});
assert.equal(window.fixturePanX,ownX);assert.equal(window.fixturePanY,ownY);assert.equal(window.fixtureZoom,ownZoom,'Replies must not reset Zane’s independent camera');
assert.equal(messages.at(-1).pageId,'1:1');
console.log('Separate-account tool dispatch keeps its own page and camera between replies: PASS');
const accountSend=window.__selectZaneSend;
window.__selectZaneSend=value=>{
  accountSend(value);const packet=messages.at(-1);
  if(packet.type==='input') window.__selectZaneInputAck(packet.inputId,{ok:true});
};
await window.__selectZaneAccountTool(57,'canvas_actions',{inputLease:'account-tool-57',actions:[{operation:'shortcut',shortcut:'fit_page'}]});
const actorInput=messages.find(message=>message.type==='input'&&message.id===57);
assert.equal(actorInput.inputLease,'account-tool-57');assert.equal(actorInput.shortcut,'fit_page');
assert.equal(messages.at(-1).type,'tool');assert.equal(messages.at(-1).id,57);assert.equal(messages.at(-1).completed,1);
window.__selectZaneSend=accountSend;
console.log('Separate-account canvas scripts use the actor process channel and preserve the native input lease: PASS');
context.document.querySelectorAll=()=>[{disabled:false,getClientRects:()=>[{}],getAttribute:()=> 'Create a new page',click(){
  const page={guid:'3:1',type:'CANVAS',name:'Untitled',childrenNodes:[],parentNode:window.fixtureNodes['0:0']};
  window.fixtureNodes['3:1']=page;window.fixtureNodes['0:0'].childrenNodes.push(page);window.fixtureState.mirror.appModel.pagesList.push({nodeId:'3:1',name:'Untitled'});
}}];
organized=await window.__selectZaneOrganize({operation:'create_page',name:'Created via normal UI'});assert.equal(organized.ok,true);assert.equal(window.fixtureNodes['3:1'].name,'Created via normal UI');
context.document.querySelectorAll=()=>[];
organized=await window.__selectZaneOrganize({operation:'create_page',name:'Blocked'});assert.equal(organized.ok,false,'Creation cannot bypass an unavailable normal page button');
window.fixtureState.openFile.canEdit=false;
organized=await window.__selectZaneOrganize({operation:'rename',nodeId:'1:2',name:'Forbidden'});assert.equal(organized.ok,false);assert.equal(window.fixtureNodes['1:2'].name,'Organized IE');
console.log('Editor organization, undo transactions, preserved design bounds and read-only permission: PASS');

let result=await window.__selectZaneShareEditor({fileKey:'fixture',email:'zane@example.test'},()=>true);
assert.equal(result.status,'granted');assert.equal(window.fixtureInvites.length,1);assert.equal(window.fixtureInvites[0].data.level,300);
result=await window.__selectZaneShareEditor({fileKey:'fixture',email:'zane@example.test'},()=>true);
assert.equal(result.status,'already');assert.equal(window.fixtureInvites.length,1,'Do not invite an editor twice');
window.fixtureRoles=[];window.fixtureOptions.editing_file.team={pro_team:true};
result=await window.__selectZaneShareEditor({fileKey:'fixture',email:'zane@example.test'},()=>true);
assert.equal(result.status,'manual-seat');assert.equal(window.fixtureInvites.length,1,'No automatic paid-seat changes');
result=await window.__selectZaneShareEditor({fileKey:'wrong-file',email:'zane@example.test'},()=>true);
assert.equal(result.status,'cancelled');assert.equal(window.fixtureInvites.length,1,'File-scoped sharing only');
window.fixtureOptions.editing_file.team={starter_team:true};
result=await window.__selectZaneShareEditor({fileKey:'fixture',email:'zane@example.test'},()=>false);
assert.equal(result.status,'cancelled');assert.equal(window.fixtureInvites.length,1,'Leaving cancels unsent invitations');
console.log('Scoped automatic editor access, no duplicates, cancellation and paid-plan boundaries: PASS');
