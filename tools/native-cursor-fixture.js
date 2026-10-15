// Synthetic editor module registry for native-cursor integration checks.
// It runs only in the private offline diagnostic, never in a Figma file.
(() => {
  const effects = [];
  class Component { constructor(props) { this.props = props; } }
  const React = {Component, createElement:(type, props, ...children) => ({type, props:{...props, children}}), useState:() => [0,()=>{}], useEffect:effect => effects.push(effect)};
  function append(node, parent) {
    if (node == null || node === false) return;
    if (Array.isArray(node)) { node.forEach(item => append(item, parent)); return; }
    if (typeof node !== 'object') { parent.append(document.createTextNode(String(node))); return; }
    const type = typeof node.type === 'object' ? node.type.type : node.type;
    if (typeof type === 'function') {
      if (type.prototype instanceof Component) {
        const component = new type(node.props);
        try { append(component.render(), parent); } catch { component.componentDidCatch?.(); }
      } else append(type(node.props), parent);
      return;
    }
    const element = document.createElement(type);
    for (const [name,value] of Object.entries(node.props)) {
      if (name === 'children') continue;
      if (name === 'style') Object.assign(element.style, value);
      else if (name === 'className') element.className = value;
      else if (value != null) element.setAttribute(name, String(value));
    }
    append(node.props.children, element); parent.append(element);
  }
  const root={guid:'0:0',type:'DOCUMENT',childrenNodes:[]};
  const page={guid:'1:1',type:'CANVAS',name:'Workbench',childrenNodes:[],parentNode:root};
  const archive={guid:'2:1',type:'CANVAS',name:'Archive',childrenNodes:[],parentNode:root};
  const frame={guid:'1:2',type:'FRAME',name:'IE',childrenNodes:[],parentNode:page,containingCanvas:'1:1',absoluteBoundingBox:{x:100,y:200,w:1000,h:600}};
  const text={guid:'1:3',type:'TEXT',name:'Title',characters:'Original design',childrenNodes:[],parentNode:frame,containingCanvas:'1:1'};frame.childrenNodes=[text];
  page.childrenNodes=[frame];root.childrenNodes=[page,archive];
  // Archive exists in page metadata but is not loaded in the scene yet.
  window.fixtureNodes={'0:0':root,'1:1':page,'1:2':frame,'1:3':text};
  window.fixtureCurrentPage='1:1';
  window.fixtureState={openFile:{canEdit:true},versionHistory:{},mirror:{appModel:{currentPage:'1:1',pagesList:[{nodeId:'1:1',name:'Workbench'},{nodeId:'2:1',name:'Archive'}]}}};
  window.fixtureStore={getState:()=>window.fixtureState,dispatch:()=>{window.fixtureDispatches=(window.fixtureDispatches??0)+1;}};
  const loadPage=async id=>{
    window.fixturePageTarget=id;
    if(id==='2:1') window.fixtureNodes[id]=archive;
    window.fixtureCurrentPage=id;window.fixtureState.mirror.appModel.currentPage=id;
    await window.fixturePageReady?.();
  };
  for(const parent of [root,page,archive]) {
    parent.appendChild=node=>{if(node.parentNode) node.parentNode.childrenNodes=node.parentNode.childrenNodes.filter(child=>child!==node);node.parentNode=parent;node.containingCanvas=parent.guid;parent.childrenNodes.push(node);};
    parent.insertChild=(node,index)=>{node.parentNode.childrenNodes=node.parentNode.childrenNodes.filter(child=>child!==node);node.parentNode=parent;parent.childrenNodes.splice(index,0,node);};
  }
  let serial=100;
  function nativeNode(type) {
    const node={guid:'99:'+(++serial),type,name:type,childrenNodes:[],parentNode:null,size:{x:100,y:100},x:0,y:0,fills:[],characters:'',absoluteBoundingBox:{x:0,y:0,w:100,h:100}};
    Object.defineProperty(node,'absoluteBoundingBox',{get:()=>({x:node.x,y:node.y,w:node.size.x,h:node.size.y}),set:box=>{if(box){node.x=box.x;node.y=box.y;node.size={x:box.w,y:box.h};}},enumerable:true});
    node.appendChild=child=>{if(child.parentNode) child.parentNode.childrenNodes=child.parentNode.childrenNodes.filter(item=>item!==child);child.parentNode=node;child.containingCanvas=node.containingCanvas;node.childrenNodes.push(child);};
    node.clone=()=>{
      const copy=nativeNode(node.type);
      for(const key of ['name','characters','size','x','y','fills','absoluteBoundingBox','hasMissingFont']) if(node[key]!==undefined) copy[key]=JSON.parse(JSON.stringify(node[key]));
      for(const child of node.childrenNodes) copy.appendChild(window.fixtureNodes[child.clone()]);
      copy.containingCanvas=node.containingCanvas;return copy.guid;
    };
    window.fixtureNodes[node.guid]=node;return node;
  }
  for(const node of [frame,text]) {
    node.clone=()=>{const copy=nativeNode(node.type);copy.name=node.name;copy.characters=node.characters;copy.containingCanvas=node.containingCanvas;copy.absoluteBoundingBox=node.absoluteBoundingBox?{...node.absoluteBoundingBox}:undefined;for(const child of node.childrenNodes) copy.appendChild(window.fixtureNodes[child.clone()]);return copy.guid;};
  }
  window.fixtureCreateNode=nativeNode;window.fixtureSelectedIds=[];
  let frameStart,frameTool=false,penTool=false,penNode;
  window.fixturePenClick=(x,y)=>{
    const zoom=window.fixtureZoom??1,wx=(x-innerWidth/2)/zoom+(window.fixturePanX??0),wy=(y-innerHeight/2)/zoom+(window.fixturePanY??0);
    if(!penNode) {
      let parent=window.fixtureNodes[window.fixtureCurrentPage];
      for(const node of Object.values(window.fixtureNodes)) {
        const box=node.absoluteBoundingBox;
        if(node.type==='FRAME'&&node.containingCanvas===parent.guid&&wx>=(node.x??box?.x)&&wx<=(node.x??box?.x)+(node.size?.x??box?.w)&&wy>=(node.y??box?.y)&&wy<=(node.y??box?.y)+(node.size?.y??box?.h)) parent=node;
      }
      penNode=nativeNode('VECTOR');parent.appendChild(penNode);penNode.fixtureInputPoints=[];
      window.fixtureInputPaths=(window.fixtureInputPaths??0)+1;
    }
    penNode.fixtureInputPoints.push({x:wx,y:wy});window.fixtureSelectedIds=[penNode.guid];
  };
  window.fixturePenFinish=()=>{penNode=null;};
  document.addEventListener?.('mousedown',event=>{
    if(!event.isTrusted||event.button!==0) return;
    if(penTool) {window.fixturePenClick(event.clientX,event.clientY);return;}
    if(frameTool) {frameStart={x:event.clientX,y:event.clientY};return;}
    const zoom=window.fixtureZoom??1,ox=window.fixturePanX??0,oy=window.fixturePanY??0;
    for(const node of Object.values(window.fixtureNodes)) {
      const box=node.absoluteBoundingBox;if(node.type!=='FRAME'||!box) continue;
      const x=innerWidth/2+(box.x-ox)*zoom+12,y=innerHeight/2+(box.y-oy)*zoom-12;
      if(Math.abs(event.clientX-x)<3&&Math.abs(event.clientY-y)<3) window.fixtureSelectedIds=[node.guid];
    }
  });
  document.addEventListener?.('keydown',event=>{
    if(event.isTrusted&&event.code==='Escape') {window.fixtureSelectedIds=[];frameTool=false;penTool=false;window.fixturePenFinish();if(document.activeElement?.closest?.('[data-testid*="cursor-chat"]'))document.activeElement.blur();}
    if(event.isTrusted&&event.code==='KeyF'&&!event.ctrlKey) frameTool=true;
    if(event.isTrusted&&event.code==='KeyP'&&!event.ctrlKey) penTool=true;
    if(event.isTrusted&&event.code==='KeyV'&&!event.ctrlKey) penTool=false;
    if(!event.isTrusted||!event.ctrlKey||event.code!=='KeyD') return;
    event.preventDefault();const source=window.fixtureNodes[window.fixtureSelectedIds[0]];
    if(source?.type==='FRAME') {const copy=window.fixtureNodes[source.clone()];source.parentNode.appendChild(copy);window.fixtureSelectedIds=[copy.guid];window.fixtureInputDuplicates=(window.fixtureInputDuplicates??0)+1;}
  });
  document.addEventListener?.('mouseup',event=>{
    if(!event.isTrusted||event.button!==0||!frameTool||!frameStart) return;
    const node=nativeNode('FRAME');window.fixtureNodes[window.fixtureCurrentPage].appendChild(node);window.fixtureSelectedIds=[node.guid];frameTool=false;frameStart=null;
    window.fixtureInputFrames=(window.fixtureInputFrames??0)+1;
  });
  const factories = {
    9:(module)=>{module.exports={api:{sendChatMessage(text){window.fixtureChat=text;},observeUser(id){window.fixtureObserving=id;},setName(name){window.fixtureAccountName=name;}}};},
    10:(module)=>{const signature='initialOptions: extractPageLocales:';module.exports={signature,initialOptions:()=>({user_data:{id:'fixture-account'},editing_file:{team:{starter_team:true}}})};},
    17:(module)=>{const signature='Failed to retrieve vector network, network data is invalid normalizedSize';
      module.exports={signature,write(node,network){const error='Invalid StyleId';if(window.fixtureDisablePathWriter)throw Error('Private path codec unavailable');if(!node.relativeTransform||!error)throw Error('transform');node.fixtureNetwork=JSON.parse(JSON.stringify(network));}};
    },
    13:(module)=>{const signature='getActiveTSSceneGraph: HEADLESS_UNSAFE';module.exports={getActiveTSSceneGraph:()=>({get:id=>window.fixtureNodes[id],createNode:nativeNode,getRoot:()=>window.fixtureNodes['0:0'],getDirectlySelectedNodes:()=>window.fixtureSelectedIds.map(id=>window.fixtureNodes[id]),getCurrentPage:()=>window.fixtureNodes[window.fixtureCurrentPage],setCurrentPageAsync:loadPage,setCurrentPageFromNodeAsync:loadPage})};},
    14:(module)=>{module.exports={async switchPage(id,old,history,dispatch){const markers='action_page_switch_initiated INCREMENTALLY_LOAD_NEW_PAGE';await loadPage(id);dispatch({});if(!markers) await window.fixtureScene.setCurrentPageFromNodeAsync(id);}};},
    15:(module)=>{module.exports={transaction:{system(){},user(name,fn){window.fixtureUndo=(window.fixtureUndo??[]).concat(name);window.fixtureInTransaction=true;try{fn();}finally{window.fixtureInTransaction=false;}}}};},
    16:(module,exports,load)=>{const t=load(15);function select(){t.transaction.system("select-reference-make-node",()=>{});}module.exports={select};},
    18:(module)=>{module.exports={history:{commit(){if(!window.fixtureInTransaction)throw Error('commit-outside-transaction');if(window.fixtureFailCommit)throw Error('fixture-save-failed');window.fixtureCommits=(window.fixtureCommits??0)+1;}}};},
    19:(module,exports,load)=>{const h=load(18);const signature='stamp-manual-keyframe stampManualKeyframeAtCurrentValueForPaint remove-property-keyframe';function stamp(){h.history?.commit();}module.exports={stamp,signature};},
    1:(module) => { module.exports = React; },
    2:(module) => { module.exports = {Kinds:{DEFAULT:'POINTER'}}; },
    3:(module, exports, load) => {
      const r=load(1), c=load(2); const path='multiplayer_cursors/ui3/';
      const peer=load(9);function nativeChat(){peer.api?.sendChatMessage('','');}
      function Cursor(props) {
        r.useState(0); const defaultKind=c.Kinds.DEFAULT;
        const checked = {chatMessage:props.chatMessage, disableMessageFade:props.disableMessageFade, isHoveringWidgetWithHiddenCursors:props.isHoveringWidgetWithHiddenCursors};
        if (props.sessionId !== undefined || props.name !== 'Zane' || props.cursorType !== defaultKind || !path || checked.isHoveringWidgetWithHiddenCursors) throw Error('Invalid native cursor props');
        // The live editor's animation wrapper uses its root's Redux provider.
        // It fails in an isolated cursor root, even when the message is empty.
        if (props.useChatAnimation) throw Error('Missing editor context for chat animation');
        if (props.color !== '#9747FF') throw Error('Invalid Figma purple cursor asset');
        return r.createElement('div', {className:'multiplayer_cursors--overlay--fixture'},
          r.createElement('span', {style:{position:'absolute',fontSize:'24px',color:props.color}}, '↖'),
          r.createElement('div', {className:'multiplayer_cursors--nameContainer--fixture', style:{position:'absolute',left:'22px',top:'20px',background:props.color,color:'white',padding:'10px',borderRadius:'7px',width:'260px'}},
            r.createElement('b', null, props.name), r.createElement('div', {className:'multiplayer_cursors--messageText--fixture'}, checked.chatMessage[0])));
      }
      module.exports = {NativeCursor:{type:Cursor}};
    },
    4:(module) => {
      const source='react-dom.production.min.js';
      module.exports = {hydrateRoot(){}, createRoot(container) {
        if (!source) throw Error();
        return {render(element) {container.replaceChildren(); append(element, container); queueMicrotask(() => effects.splice(0).forEach(effect => effect()));}, unmount(){container.replaceChildren();}};
      }};
    },
    5:(module) => { module.exports = {api:{getViewportInfo:() => ({x:0,y:0,width:innerWidth,height:innerHeight,offsetX:window.fixturePanX ?? 0,offsetY:window.fixturePanY ?? 0,zoomScale:window.fixtureZoom ?? 1})}, ready:{onReady:() => Promise.resolve()}}; },
    6:(module, exports, load) => {
      const v=load(5); const signature='computeViewportSettingsForNode__DO_NOT_USE_DIRECTLY Invalid Viewport';
      v.ready.onReady().then(()=>{}); module.exports = {signature};
    },
    7:(module) => { module.exports = {api:{setViewportCenter(point){window.fixturePanX=point.x;window.fixturePanY=point.y;},setViewportZoomScale(zoom){window.fixtureZoom=zoom;},scrollAndZoomIntoView(ids){window.fixtureFocusedIds=ids;}}}; },
    8:(module,exports,load) => {
      const v=load(7); const signature='pan_to_changed_nodes: SceneGraphHelpers';
      function focus(){v.api.scrollAndZoomIntoView([]);}
      module.exports={signature,focus};
    }
  };
  const cache = {};
  const require = id => {
    if (!cache[id]) { const module = {exports:{}}; cache[id] = module; factories[id](module, module.exports, require); }
    return cache[id].exports;
  };
  require.m = factories;
  window.webpackChunkSyntheticFigma = [];
  window.webpackChunkSyntheticFigma.push = chunk => { chunk[2](require); return 0; };
  const typography = document.createElement('div'); typography.id='fullscreen-root'; typography.style.cssText='font:14px Arial'; typography.__reactFiber$fixture={dependencies:{firstContext:{memoizedValue:{store:window.fixtureStore}}}};document.body.append(typography);
})();
