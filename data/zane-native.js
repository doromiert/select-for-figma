// Resolve the running editor's multiplayer component by its exports and source
// signatures. The native component owns cursor artwork, name tag and chat layout.
// This presentation never writes design nodes or registers a multiplayer user.
(() => {
function modules() {
  let runtime;
  const cache = new Map();
  const source = value => typeof value === 'function' ? Function.prototype.toString.call(value) : '';
  function requireRuntime() {
    if (runtime) return runtime;
    const found = new Set();
    for (const name of Object.getOwnPropertyNames(window)) {
      if (!/^(webpack|rspack)Chunk/.test(name) || !Array.isArray(window[name])) continue;
      const tag = crypto.randomUUID?.() ?? [...crypto.getRandomValues(new Uint32Array(4))].join('-');
      const queue = window[name], probe = [[`select-zane-${tag}`], {}, candidate => { if (candidate?.m) found.add(candidate); }];
      queue.push(probe);
      if (queue.at(-1) === probe) queue.pop();
    }
    if (found.size !== 1) throw Error('module runtime');
    return runtime = [...found][0];
  }
  function moduleWith(...parts) {
    const key=parts.join('\0');
    if (cache.has(key)) return cache.get(key);
    const require = requireRuntime();
    const matches = Object.entries(require.m).filter(([, factory]) => parts.every(part => source(factory).includes(part)));
    if (matches.length !== 1) throw Error('native module signature');
    const [id, factory] = matches[0];
    const result={code:source(factory), exports:require(id)}; cache.set(key,result); return result;
  }
  function dependency(module, name) {
    if (!/^[\w$]+$/.test(name)) throw Error('dependency name');
    const escaped = name.replace(/\$/g, '\\$');
    const match = module.code.match(new RegExp(`(?:^|[^\\w$])${escaped}\\s*=\\s*[\\w$]+\\(\\s*["']?(\\d+)["']?\\s*\\)`));
    if (!match) throw Error('dependency signature');
    return requireRuntime()(match[1]);
  }
  function only(values, predicate) {
    const matches = [...new Set(values)].filter(predicate);
    if (matches.length !== 1) throw Error('native export signature');
    return matches[0];
  }
  return {moduleWith,dependency,only,source};
}
window.__selectZaneCursorFactory = (notify, action) => {
  const {moduleWith,dependency,only,source} = modules();
  const module = moduleWith('disableMessageFade:', 'multiplayer_cursors/ui3/');
  const Cursor = only(Object.values(module.exports), value => ['chatMessage:', 'disableMessageFade:', 'isHoveringWidgetWithHiddenCursors:'].every(part => source(value?.type).includes(part)));
  const code = source(Cursor.type), reactName = code.match(/([\w$]+)\.useState/), typesName = code.match(/([\w$]+)\.([\w$]+)\.DEFAULT/);
  if (!reactName || !typesName) throw Error('cursor dependencies');
  const React = dependency(module, reactName[1]), types = dependency(module, typesName[1])[typesName[2]];
  const dom = moduleWith('react-dom.production.min.js', 'hydrateRoot', 'createRoot').exports;
  const viewportModule = moduleWith('computeViewportSettingsForNode__DO_NOT_USE_DIRECTLY', 'Invalid Viewport');
  const viewportName = viewportModule.code.match(/([\w$]+)\.[\w$]+\.onReady\(\)\.then/);
  if (!viewportName || typeof React.createElement !== 'function' || typeof dom.createRoot !== 'function' || types?.DEFAULT === undefined) throw Error('native renderer');
  const viewport = only(Object.values(dependency(viewportModule, viewportName[1])), value => typeof value?.getViewportInfo === 'function');
  const host = document.createElement('div'); host.id = 'select-zane-native';
  host.setAttribute('role', 'status'); host.setAttribute('aria-live', 'polite');
  host.style.cssText = 'position:fixed;inset:0;z-index:2147483000;pointer-events:none';
  const mount = document.createElement('div'); mount.style.cssText = 'position:absolute;left:0;top:0;pointer-events:none'; host.append(mount);
  const cursor = document.createElement('div'); cursor.style.cssText = 'position:relative;pointer-events:none'; mount.prepend(cursor);
  const typography = document.querySelector('[class*="multiplayer_cursors--overlay--"]') ?? document.querySelector('#fullscreen-root');
  if (!typography) throw Error('cursor typography');
  const computed = getComputedStyle(typography);
  for (const name of ['font-family','font-size','font-weight','line-height','letter-spacing','font-feature-settings','font-variation-settings']) cursor.style.setProperty(name, computed.getPropertyValue(name));
  document.body.append(host);
  let disposed = false, broken = false, visible = false, hiddenForCapture = false, frame = 0, last = {}, world, move, following = false, navigation, followingUser = false, pointer = {x:80,y:100}, sentPresence = 0, lastBubbleScan = 0;
  const events = new AbortController();
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  const root = dom.createRoot(cursor);
  const style = document.createElement('style');
  style.textContent = '.select-zane-real-message{min-width:0;max-width:min(320px,calc(100vw - 96px));max-height:220px;overflow:auto;white-space:pre-wrap;overflow-wrap:anywhere}.select-zane-real-message div{white-space:pre-wrap;overflow-wrap:anywhere;max-width:100%;min-width:0}#select-zane-native [class*="multiplayer_cursors--messageText--"]{min-width:0;max-width:min(320px,calc(100vw - 96px));max-height:220px;overflow:auto;white-space:pre-wrap;overflow-wrap:anywhere;pointer-events:auto}#select-zane-native [class*="multiplayer_cursors--messageText--"] div{min-width:0;max-width:100%;white-space:pre-wrap;overflow-wrap:anywhere}';
  host.append(style);
  const avatar = document.createElement('button'); avatar.id = 'select-zane-avatar'; avatar.type = 'button';
  avatar.textContent = 'Z'; avatar.setAttribute('aria-label','Follow Zane'); avatar.setAttribute('aria-pressed','false'); avatar.title = 'Follow Zane';
  avatar.style.cssText = 'position:fixed;right:24px;top:80px;z-index:2147483001;width:32px;height:32px;border-radius:50%;border:2px solid transparent;background:#9747FF;color:white;font:600 13px sans-serif;cursor:pointer;box-shadow:0 2px 8px #0003';
  document.body.append(avatar);
  function follow(value) {
    try {
      if (value && !navigation) {
        const module = moduleWith('pan_to_changed_nodes: SceneGraphHelpers', 'scrollAndZoomIntoView');
        const alias = module.code.match(/([\w$]+)\.[\w$]+\.scrollAndZoomIntoView\(/);
        if (!alias) throw Error('follow navigation');
        navigation = only(Object.values(dependency(module,alias[1])), value => typeof value?.setViewportCenter === 'function' && typeof value?.setViewportZoomScale === 'function');
      }
      following = !!value && visible;
      if (following && followingUser) { followingUser=false; action({type:'follow_user_off'}); }
      avatar.setAttribute('aria-pressed',String(following)); avatar.title = following ? 'Stop following Zane' : 'Follow Zane';
      avatar.setAttribute('aria-label',avatar.title); avatar.style.borderColor = following ? 'white' : 'transparent';
      return following;
    } catch { avatar.title = 'Following is unavailable in this Figma version'; return false; }
  }
  avatar.onclick = event => { event.stopPropagation(); follow(!following); };
  const cancelFollow = e => {
    if (e.target?.closest?.('#select-zane-native,#select-zane-avatar') || e.target?.closest?.('[class*="chat_input--"],[data-testid*="cursor-chat"]')) return;
    follow(false);
  };
  document.addEventListener('pointermove',e=>{pointer={x:e.clientX,y:e.clientY};},{capture:true,signal:events.signal});
  document.addEventListener('pointerdown',cancelFollow,{capture:true,signal:events.signal});
  document.addEventListener('wheel',cancelFollow,{capture:true,passive:true,signal:events.signal});
  document.addEventListener('keydown',e=>{if(e.key==='Escape')follow(false);},{capture:true,signal:events.signal});
  const failed = () => { if (!disposed && !broken) { broken = true; visible = false; cancelAnimationFrame(frame); frame = 0; host.hidden = true; avatar.hidden = true; follow(false); notify(false); } };
  class Guard extends React.Component {
    state = {failed:false};
    static getDerivedStateFromError() { return {failed:true}; }
    componentDidCatch() { queueMicrotask(failed); }
    render() { return this.state.failed ? null : this.props.children; }
  }
  function Ready() { React.useEffect(() => { if (!disposed && !broken) notify(true); }, []); return null; }
  function view() {
    const value = viewport.getViewportInfo();
    if (!value || !['x','y','width','height','offsetX','offsetY','zoomScale'].every(key => Number.isFinite(value[key])) || value.zoomScale <= 0) throw Error('viewport');
    return value;
  }
  const toWorld = (p, v) => ({x:(p.x - v.x - v.width/2)/v.zoomScale + v.offsetX, y:(p.y - v.y - v.height/2)/v.zoomScale + v.offsetY});
  const toScreen = (p, v) => ({x:v.x + v.width/2 + (p.x - v.offsetX)*v.zoomScale, y:v.y + v.height/2 + (p.y - v.offsetY)*v.zoomScale});
  function presenceState(v) {
    let userSession;
    try {
      const alias=module.code.match(/([\w$]+)\.([\w$]+)\??\.sendChatMessage\(/);
      if (alias) userSession=Number(dependency(module,alias[1])[alias[2]]?.currentSessionID?.());
    } catch {}
    return {type:'presence',visible,x:world.x,y:world.y,zoom:v.zoomScale,userSession:Number.isSafeInteger(userSession)?userSession:undefined};
  }
  function tick(now) {
    frame = 0; if (disposed || !visible) return;
    try {
      if (followingUser) {
        const v=view();
        if (pointer.x>=v.x && pointer.x<=v.x+v.width && pointer.y>=v.y && pointer.y<=v.y+v.height) {
          world=toWorld({x:pointer.x+32,y:pointer.y+20},v); move=null;
          if (last.account_wanted && now-sentPresence>=100) {sentPresence=now;action(presenceState(v));}
        }
      }
      if (following) {
        const v=view();
        if (Math.hypot(v.offsetX-world.x,v.offsetY-world.y)>0.01) navigation.setViewportCenter(world);
      }
      if(last.account&&now-lastBubbleScan>=500) {
        lastBubbleScan=now;
        for(const name of document.querySelectorAll('[class*="multiplayer_cursors--cursorName--"]')) if(name.textContent==='Zane') {
          for(let parent=name.parentElement,depth=0;parent&&depth<3;depth++,parent=parent.parentElement) {
            const message=parent.querySelector('[class*="multiplayer_cursors--messageText--"]');
            if(message) {message.classList.add('select-zane-real-message');break;}
          }
        }
      }
      const v = view(); let point = toScreen(world, v);
      if (move) {
        const progress = Math.min(1, (now - move.at)/500), eased = 1 - (1 - progress)**3;
        const from = toScreen(move.from, v), target = point;
        point = {x:from.x + (target.x-from.x)*eased, y:from.y + (target.y-from.y)*eased - Math.sin(progress*Math.PI)*24};
        if (progress === 1) move = null;
      }
      host.style.clipPath = `inset(${Math.max(0,v.y)}px ${Math.max(0,innerWidth-v.x-v.width)}px ${Math.max(0,innerHeight-v.y-v.height)}px ${Math.max(0,v.x)}px)`;
      mount.style.transform = `translate3d(${point.x}px,${point.y}px,0)`;
      // Attach our local follow avatar alongside Figma's native participants.
      const anchor = document.querySelector('#self-avatar-with-tools-container');
      if (anchor) {
        const rect = anchor.getBoundingClientRect();
        avatar.style.top = `${rect.top + (rect.height-32)/2}px`;
        avatar.style.right = `${Math.max(16,innerWidth-rect.left+8)}px`;
      }
      frame = requestAnimationFrame(tick);
    } catch { failed(); }
  }
  function update(state) {
    if (disposed || broken) return;
    visible = !!state.visible; host.hidden = !visible || hiddenForCapture || !!state.account; avatar.hidden = !visible || hiddenForCapture || !!state.account;
    followingUser=!!state.follow_user;
    if (followingUser || !visible) follow(false);
    if (visible && (!world || state.x !== last.x || state.y !== last.y || state.space !== last.space || state.revision !== last.revision)) {
      const v = view(), target = state.space === 'canvas' ? toScreen(state,v) : {x:state.x+32,y:state.y+20};
      const from = world ?? toWorld({x:v.x+v.width+40, y:target.y-50}, v);
      world = toWorld(target, v); move = reduced.matches ? null : {from, at:performance.now()};
    }
    last = {...state};
    if (world) action(presenceState(view()));
    root.render(React.createElement(Guard, null,
      // Figma's chat animation reads a Redux provider owned by the editor root.
      // Our separate cursor root cannot use that wrapper; movement stays animated
      // by tick() without depending on the editor's React context.
      !state.account && React.createElement(Cursor, {name:'Zane', color:'#9747FF', cursorType:types.DEFAULT, chatMessage:[String(state.text??''), ''], editorType:'design', disableMessageFade:!!state.busy, useChatAnimation:false, prefersReducedMotion:reduced.matches, isHighFiving:false, isHighFivingSupported:false, isCursorAndChatAreaOffscreen:false, isHoveringWidgetWithHiddenCursors:false}),
      React.createElement(Ready)));
    if (visible && !frame) frame = requestAnimationFrame(tick);
    if (!visible) { cancelAnimationFrame(frame); frame = 0; }
  }
  host.hidden = true; avatar.hidden = true;
  return {
    update,
    near(point) {
      if (!visible || !world || broken) return false;
      if(last.account&&last.account_page) {try{if(editorScene().getCurrentPage()?.guid!==last.account_page) return false;}catch{return false;}}
      const v = view(), position = toScreen(world,v);
      return position.x >= v.x && position.x <= v.x+v.width && position.y >= v.y && position.y <= v.y+v.height && Math.hypot(position.x-point.x,position.y-point.y) <= 220;
    },
    follow,

    captureHidden(value) { hiddenForCapture = value; host.hidden = !visible || value || !!last.account; avatar.hidden = !visible || value || !!last.account; },
    dispose() { disposed = true; cancelAnimationFrame(frame); events.abort(); root.unmount(); host.remove(); avatar.remove(); }
  };
};

// This path is invoked only in the isolated, explicitly connected Figma account
// window. It uses that account's real multiplayer session and never rewrites the
// signed-in user's identity or inserts a fabricated user into their session.
const peerModules=modules();
let summonRequested=0,summonApplied=0,summonTask;
window.__selectZanePeer = async state => {
  window.__selectZaneAccountActive=true;
  const send = value => window.__selectZaneSend('select-zane-account:'+JSON.stringify(value));
  const requested=state.summon?.id;
  // Record the request before resolving modules so startup failures can report
  // their status too. Superseded async calls must not overwrite newer ones.
  if(requested&&summonRequested!==requested) {summonRequested=requested;summonTask=undefined;}
  let stage='identity';
  try {
    const {moduleWith,dependency,only} = peerModules;
    window.__selectZaneBeginDesignSession(state.designSession,state.designBaseline);
    const options=moduleWith('initialOptions:','extractPageLocales:').exports.initialOptions();
    const user=options.user_data;
    if (user?.email) send({identity:true,email:user.email,userId:user.id});
    if(!user?.id) {send({ready:false,status:'sign_in_required',stage,summonId:requested});return;}
    const key=location.pathname.match(/^\/(?:file|design|board|proto|slides|deck|site|buzz|make)\/([^/]+)/)?.[1];
    if (state.fileKey && key!==state.fileKey) {send({ready:false,status:'opening_file',stage:'file',summonId:requested});return;}
    stage='multiplayer';
    const cursor = moduleWith('disableMessageFade:', 'multiplayer_cursors/ui3/');
    const chat = cursor.code.match(/([\w$]+)\.([\w$]+)\??\.sendChatMessage\(/);
    if (!chat) throw Error('account chat');
    const multiplayer = dependency(cursor,chat[1])[chat[2]];
    if (typeof multiplayer?.sendChatMessage !== 'function') throw Error('account session');
    multiplayer.setName?.('Zane');
    stage='viewport';
    const module = moduleWith('pan_to_changed_nodes: SceneGraphHelpers','scrollAndZoomIntoView');
    const alias = module.code.match(/([\w$]+)\.[\w$]+\.scrollAndZoomIntoView\(/);
    if (!alias) throw Error('account navigation');
    const navigation = only(Object.values(dependency(module,alias[1])), value=>typeof value?.setViewportCenter==='function'&&typeof value?.setViewportZoomScale==='function');
    const viewportModule = moduleWith('computeViewportSettingsForNode__DO_NOT_USE_DIRECTLY','Invalid Viewport');
    const viewportName = viewportModule.code.match(/([\w$]+)\.[\w$]+\.onReady\(\)\.then/);
    const viewport = only(Object.values(dependency(viewportModule,viewportName[1])),value=>typeof value?.getViewportInfo==='function');
    const observe=state.followUser&&Number.isSafeInteger(state.userSession)&&state.userSession>=0 ? state.userSession : -1;
    if (window.__selectZaneObservedSession!==observe && typeof multiplayer.observeUser==='function') {
      multiplayer.observeUser(observe); window.__selectZaneObservedSession=observe;
    }
    if(state.summon?.id && state.summon.id!==summonApplied) {
      const id=state.summon.id;
      stage='page';
      if(!summonTask) {
        summonTask=bringToCaller(state.summon,()=>summonRequested===id&&window.__selectZaneAccountActive!==false).then(()=>{if(summonRequested===id) summonApplied=id;});
      }
      const task=summonTask;
      try {await task;} catch(error) {
        // A cold editor can reject the first page switch. Retry it next time;
        // awaiting a permanently rejected promise otherwise traps user mode.
        if(summonTask===task) summonTask=undefined;
        throw error;
      }
      if(summonRequested!==id) return;
    }
    designBaseline();
    stage='publish';
    if (observe===-1&&!state.autonomous) {navigation.setViewportZoomScale(state.zoom); navigation.setViewportCenter({x:state.x,y:state.y});}
    multiplayer.sendChatMessage(String(state.text??''),'');
    const view = viewport.getViewportInfo();
    if (![view.x,view.y,view.width,view.height].every(Number.isFinite)) throw Error('account viewport');
    const point=state.autonomous ? window.__selectZaneActorPoint??{x:view.offsetX,y:view.offsetY} : state;
    const pageId=editorScene().getCurrentPage()?.guid;
    send({ready:true,status:'ready',canEdit:editorStore()?.getState()?.openFile?.canEdit===true,x:view.x+view.width/2+(point.x-view.offsetX)*view.zoomScale,y:view.y+view.height/2+(point.y-view.offsetY)*view.zoomScale,canvasX:point.x,canvasY:point.y,pageId,summonId:state.summon?.id,viewport:view});
  } catch {if(!requested||requested===summonRequested) send({ready:false,status:'editor_unavailable',stage,summonId:requested});}
};

window.__selectZaneShareEditor = async (request, alive) => {
  const key=()=>location.pathname.match(/^\/(?:file|design|board|proto|slides|deck|site|buzz|make)\/([^/]+)/)?.[1];
  try {
    if (key()!==request.fileKey || !alive()) return {ok:false,status:'cancelled'};
    const {moduleWith,dependency}=modules();
    const options=moduleWith('initialOptions:','extractPageLocales:').exports.initialOptions();
    if (!options.user_data?.id) throw Error('not signed in');
    const invitation=moduleWith('Invited Emails','/api/invites/resend');
    const match=invitation.code.match(/([\w$]+)\.([\w$]+)\.post\(\1\.\2\.url`\/api\/invites`/);
    if (!match) throw Error('sharing transport');
    const api=dependency(invitation,match[1])[match[2]];
    const getRoles=async()=>{const response=await api.get(api.url`/api/roles/file/${request.fileKey}`);const meta=response.data?.meta;return Array.isArray(meta)?meta:meta?.roles;};
    const hasAccess=roles=>Array.isArray(roles)&&roles.some(role=>role.user?.email?.toLowerCase()===request.email.toLowerCase()&&role.level>=300);
    const roles=await getRoles();
    if (!Array.isArray(roles)) throw Error('file roles');
    if (hasAccess(roles)) return {ok:true,status:'already'};
    const file=options.editing_file, team=file?.team;
    // Never enroll an account into a paid team or authorize seat billing.
    // Paid-plan access stays with Figma's normal sharing/seat approval UI.
    if (!file || (team && (team.org_team || team.pro_team || team.starter_team!==true)) || file.parent_org_id || file.org_id) return {ok:false,status:'manual-seat'};
    if (key()!==request.fileKey || !alive()) return {ok:false,status:'cancelled'};
    const response=await api.post(api.url`/api/invites`,{emails:[request.email],resource_type:'file',resource_id_or_key:request.fileKey,level:300});
    if (response.data?.error) return {ok:false,status:'manual'};
    return hasAccess(await getRoles()) ? {ok:true,status:'granted'} : {ok:false,status:'manual'};
  } catch { return {ok:false,status:'manual'}; }
};

const navigationModules=modules();
function editorStore() {
  for (const selector of ['#self-avatar-with-tools-container','#fullscreen-root']) {
    const anchor=document.querySelector?.(selector);
    if (!anchor) continue;
    const key=Object.keys(anchor).find(key=>key.startsWith('__reactFiber$'));
    let fiber=key ? anchor[key] : null;
    for (let depth=0;fiber&&depth<100;depth++,fiber=fiber.return) {
      for (let context=fiber.dependencies?.firstContext;context;context=context.next) {
        const store=context.memoizedValue?.store;
        if (typeof store?.getState==='function'&&typeof store?.dispatch==='function'&&store.getState()?.mirror?.appModel) return store;
      }
    }
  }
}
function editorScene() {
  return navigationModules.moduleWith('getActiveTSSceneGraph:','HEADLESS_UNSAFE').exports.getActiveTSSceneGraph();
}
function editorPages(scene,store) {
  const pages=new Map();
  const internal=scene.getInternalCanvas?.()?.guid;
  const add=node=>{
    const id=typeof node==='string' ? node : node?.guid??node?.nodeId??node?.id;
    if (typeof id!=='string'||!/^\d+:\d+$/.test(id)||id===internal||node?.isInternalOnly) return;
    const loaded=scene.get(id);
    const name=typeof node?.name==='string' ? node.name : loaded?.name;
    if (typeof name==='string') pages.set(id,{id,name:name.slice(0,200)});
  };
  for (const node of scene.getRoot?.()?.childrenNodes??[]) if (node.type==='CANVAS') add(node);
  for (const node of store?.getState()?.mirror?.appModel?.pagesList??[]) add(node);
  return [...pages.values()];
}
function editorViewport() {
  const {moduleWith,dependency,only}=navigationModules;
  const module=moduleWith('computeViewportSettingsForNode__DO_NOT_USE_DIRECTLY','Invalid Viewport');
  const alias=module.code.match(/([\w$]+)\.[\w$]+\.onReady\(\)\.then/);
  const value=only(Object.values(dependency(module,alias[1])),value=>typeof value?.getViewportInfo==='function').getViewportInfo();
  return value;
}
function canvasNavigation() {
  const {moduleWith,dependency,only}=navigationModules;
  const module=moduleWith('pan_to_changed_nodes: SceneGraphHelpers','scrollAndZoomIntoView');
  const alias=module.code.match(/([\w$]+)\.[\w$]+\.scrollAndZoomIntoView\(/);
  return only(Object.values(dependency(module,alias[1])),value=>typeof value?.scrollAndZoomIntoView==='function'&&typeof value?.setViewportCenter==='function'&&typeof value?.setViewportZoomScale==='function');
}
async function openPage(scene,store,pageId,alive) {
  if (scene.getCurrentPage?.()?.guid===pageId) return;
  if (!alive()) throw Error('cancelled');
  // Figma's sidebar path also loads incremental pages and updates page state.
  // Looking up every layer before this call rejects pages not loaded yet.
  if (store) {
    const {moduleWith,only,source}=navigationModules;
    const module=moduleWith('action_page_switch_initiated','INCREMENTALLY_LOAD_NEW_PAGE');
    const switchPage=only(Object.values(module.exports),value=>source(value).includes('action_page_switch_initiated')&&source(value).includes('setCurrentPageFromNodeAsync'));
    const state=store.getState();
    await switchPage(pageId,scene.getCurrentPage?.()?.guid??state.mirror.appModel.currentPage,state.versionHistory??{},store.dispatch.bind(store),!!scene.get(pageId)?.childCount,()=>{});
  } else if (typeof scene.setCurrentPageAsync==='function') await scene.setCurrentPageAsync(pageId);
  else await scene.setCurrentPageFromNodeAsync(pageId);
  if (!alive()) throw Error('cancelled');
  if (scene.getCurrentPage?.()?.guid!==pageId) throw Error('page-switch');
}
window.__selectZaneLocation = pointer => {
  designBaseline();
  const scene=editorScene(),view=editorViewport();
  const pageId=scene.getCurrentPage()?.guid??editorStore()?.getState()?.mirror?.appModel?.currentPage,baseline=designPages.get(pageId);
  return {pageId,x:(pointer.x+32-view.x-view.width/2)/view.zoomScale+view.offsetX,y:(pointer.y+20-view.y-view.height/2)/view.zoomScale+view.offsetY,zoom:view.zoomScale,designBaseline:baseline?{pageId,frames:[...baseline.ids].slice(0,5000),complete:baseline.complete&&baseline.ids.size<=5000}:undefined};
};
async function bringToCaller(request,alive) {
  const scene=editorScene(),store=editorStore();
  if(!editorPages(scene,store).some(page=>page.id===request.pageId)) throw Error('caller-page');
  if(!['x','y','zoom'].every(key=>Number.isFinite(request[key]))||request.zoom<=0||request.zoom>256) throw Error('caller-position');
  await openPage(scene,store,request.pageId,alive);
  if(!alive()) throw Error('cancelled-summon');
  const navigation=canvasNavigation();
  navigation.setViewportZoomScale(request.zoom);navigation.setViewportCenter({x:request.x,y:request.y});
  window.__selectZaneActorPoint={x:request.x,y:request.y};
}
window.__selectZanePages = () => {
  try {
    const scene=editorScene(),pages=editorPages(scene,editorStore());
    return {ok:true,pages,currentPageId:scene.getCurrentPage?.()?.guid,viewport:editorViewport()};
  } catch {return {ok:false,status:'The open editor’s page list is unavailable.'};}
};
window.__selectZaneFind = request => {
  try {
    const scene=editorScene(),page=scene.getCurrentPage();
    const query=request.query?.trim().toLocaleLowerCase();
    if (!query||query.length>200||!page) throw Error('query');
    const queue=[...page.childrenNodes],matches=[],seen=new Set();let count=0;
    while (queue.length&&count<10000) {
      const node=queue.shift(); if (!node||seen.has(node.guid)) continue;
      seen.add(node.guid);count++;
      const name=node.name??'';
      if (typeof name==='string'&&name.toLocaleLowerCase().includes(query)) {
        matches.push({id:node.guid,name:name.slice(0,200),type:node.type,bounds:node.absoluteBoundingBox,exact:name.toLocaleLowerCase()===query});
      }
      for (const child of node.childrenNodes??[]) queue.push(child);
    }
    matches.sort((a,b)=>Number(b.exact)-Number(a.exact));
    return {ok:true,pageId:page.guid,matches:matches.slice(0,50),scanned:count,truncated:queue.length>0||matches.length>50,coverage:'Names of loaded layers on the current page.',viewport:editorViewport()};
  } catch {return {ok:false,status:'Local layer search is unavailable on this page.'};}
};
window.__selectZaneStructure = async (request,alive=()=>true) => {
  try {
    const scene=editorScene(),store=editorStore(),pages=editorPages(scene,store);
    if (request.pageId) {if (!pages.some(page=>page.id===request.pageId)) throw Error('page');await openPage(scene,store,request.pageId,alive);}
    if (!alive()) throw Error('cancelled');
    let count=0;
    const shape=(node,depth=0)=>{
      if (++count>500) return {id:node.guid,truncated:true};
      const value={id:node.guid,name:(node.name??'').slice(0,200),type:node.type==='CANVAS'?'PAGE':node.type};
      if(node.type!=='CANVAS') {value.editableWithoutCopy=unframed(node);for(let parent=node;parent;parent=parent.parentNode) {if(owned(parent)) {value.editableWithoutCopy=true;break;}if(parent.type==='FRAME') break;}}
      const box=node.absoluteBoundingBox;
      if (box) Object.assign(value,{x:box.x,y:box.y,width:box.w,height:box.h});
      if (typeof node.characters==='string') value.characters=node.characters.slice(0,1000);
      const children=node.childrenNodes??[];
      if (children.length&&depth<5) {value.children=[];for (const child of children) {if(count>=500){value.truncated=true;break;}value.children.push(shape(child,depth+1));}}
      else if (children.length) value.truncated=true;
      return value;
    };
    const page=scene.getCurrentPage();if(!page) throw Error('page');
    designBaseline();
    return {ok:true,pages,page:shape(page),coverage:'Loaded layers on this page, up to five nested levels and 500 nodes.',viewport:editorViewport()};
  } catch {return {ok:false,status:'Could not read the loaded layer structure in this editor.'};}
};
function editTransaction() {
  const {moduleWith,dependency}=navigationModules;
  const module=moduleWith('select-reference-make-node');
  const binding=module.code.match(/([\w$]+)\.([\w$]+)\.system\("select-reference-make-node"/);
  const transaction=dependency(module,binding[1])[binding[2]];
  if (typeof transaction?.user!=='function') throw Error('transaction');
  return transaction;
}
function editCommit() {
  const {moduleWith,dependency}=navigationModules;
  const module=moduleWith('stamp-manual-keyframe','stampManualKeyframeAtCurrentValueForPaint','remove-property-keyframe');
  const binding=module.code.match(/([\w$]+)\.([\w$]+)\?\.commit\(\)/);
  if(!binding) throw Error('editor-commit-binding');
  const editor=dependency(module,binding[1])[binding[2]];
  if(typeof editor?.commit!=='function') throw Error('editor-commit-unavailable');
  return ()=>editor.commit();
}
window.__selectZaneOrganize = async (request,alive=()=>true) => {
  let stage='editor';
  try {
    const scene=editorScene(),store=editorStore();
    if (store?.getState()?.openFile?.canEdit!==true) return {ok:false,status:'The connected editor does not have edit permission for this file.'};
    const transaction=editTransaction(),commit=editCommit();
    let pages=editorPages(scene,store),node=request.nodeId ? scene.get(request.nodeId) : null;
    const operation=request.operation;
    const name=typeof request.name==='string' ? request.name.trim().slice(0,200) : '';
    if (!alive()) throw Error('cancelled');
    stage='operation';
    if (operation==='create_page') {
      if (!name) throw Error('name');
      // Use the regular page button, including Figma's own plan/permission gate.
      const button=[...document.querySelectorAll('button')].find(button=>!button.disabled&&button.getClientRects().length&&/^(?:add|new|create)(?: a)?(?: new)? page$/i.test(button.getAttribute('aria-label')??button.getAttribute('title')??''));
      if (!button) return {ok:false,status:'Figma’s New Page button is unavailable. Create the page in the editor first.'};
      const before=new Set(pages.map(page=>page.id));button.click();
      for(let i=0;i<20;i++) {
        await new Promise(resolve=>setTimeout(resolve,100));if(!alive()) throw Error('cancelled');
        pages=editorPages(scene,store);const created=pages.find(page=>!before.has(page.id));
        if(created) {node=scene.get(created.id);transaction.user('select-zane-name-new-page',()=>{node.name=name;commit();});return {ok:true,createdNodeIds:[node.guid],mutatedNodeIds:[],designAppearanceChanged:false};}
      }
      return {ok:false,status:'Figma did not create a page. Its normal permission or plan limit may have blocked creation.'};
    }
    if (!node||node.type==='DOCUMENT') throw Error('node');
    if (operation==='rename') {
      if(!name) throw Error('name');
      transaction.user('select-zane-rename',()=>{node.name=name;commit();});
    } else if (operation==='reorder_page') {
      if (node.type!=='CANVAS'||!Number.isInteger(request.index)||request.index<0||request.index>=pages.length) throw Error('page-order');
      const others=pages.filter(page=>page.id!==node.guid),root=scene.getRoot();
      const children=root.childrenNodes.filter(child=>child.guid!==node.guid);
      const next=others[request.index];const index=next ? children.findIndex(child=>child.guid===next.id) : children.length;
      if(index<0) throw Error('page-order');
      transaction.user('select-zane-reorder-page',()=>{root.insertChild(node,index);commit();});
    } else if (operation==='move_frame_to_page') {
      if(node.type!=='FRAME'||!['CANVAS','SECTION'].includes(node.parentNode?.type)||!pages.some(page=>page.id===request.pageId)) throw Error('frame');
      // Keep the source object loaded while opening the destination page.
      await openPage(scene,store,request.pageId,alive);if(!alive()) throw Error('cancelled');
      const destination=scene.get(request.pageId);node=scene.get(request.nodeId);
      if(!destination||!node) throw Error('frame');
      transaction.user('select-zane-move-frame-to-page',()=>{destination.appendChild(node);commit();});
    } else throw Error('operation');
    return {ok:true,createdNodeIds:[],mutatedNodeIds:[node.guid],designAppearanceChanged:false,viewport:editorViewport()};
  } catch(error) {return {ok:false,code:error?.message,status:'Organization could not complete in the connected editor.',stage};}
};
const frameCopies=new Set(),duplicatedFrames=new Set(),copySources=new Map(),copySnapshots=new Map(),copyMappings=new Map(),designPages=new Map();let designSession;
function designBaseline() {
  try {
    const scene=editorScene(),page=scene.getCurrentPage();editorViewport();
    if(!page||designPages.has(page.guid)) return;
    const ids=new Set(),queue=[...page.childrenNodes];let count=0;
    while(queue.length&&count++<20000) {const node=queue.shift();if(node.type==='FRAME') ids.add(node.guid);queue.push(...node.childrenNodes??[]);}
    designPages.set(page.guid,{ids,complete:queue.length===0});
  } catch {}
}
window.__selectZaneBeginDesignSession=(id,seed) => {
  if(id&&designSession!==id) {designSession=id;frameCopies.clear();duplicatedFrames.clear();copySources.clear();copySnapshots.clear();copyMappings.clear();designPages.clear();}
  if(seed?.pageId&&Array.isArray(seed.frames)&&typeof seed.complete==='boolean') designPages.set(seed.pageId,{ids:new Set(seed.frames),complete:seed.complete});
  designBaseline();
};
function freshFrame(node) {const page=designPages.get(node.containingCanvas);return node.type==='FRAME'&&page?.complete&&![...designPages.values()].some(page=>page.ids.has(node.guid));}
function owned(node,allowed=[]) {return frameCopies.has(node.guid)||freshFrame(node)||node.type!=='FRAME'&&allowed.includes(node.guid)&&unframed(node);}
function frameAncestor(node) {let frame;for(let parent=node;parent;parent=parent.parentNode) if(parent.type==='FRAME') frame=parent;return frame;}
function unframed(node) {
  if(!node||['DOCUMENT','CANVAS','FRAME'].includes(node.type)||frameAncestor(node)) return false;
  const queue=[node];let count=0;while(queue.length&&count++<20000) {const child=queue.shift();if(child.type==='FRAME') return false;queue.push(...child.childrenNodes??[]);}
  return queue.length===0;
}
function inputBlocked(x,y) {
  for(const element of [document.activeElement,document.elementFromPoint?.(x,y)]) {
    const control=element?.closest?.('input,textarea,button,[contenteditable="true"],[role="dialog"],[role="menu"]');
    if(!control) continue;
    if(control.matches?.('input,textarea')&&control.getAttribute?.('aria-hidden')==='true') continue;
    const rect=control.getBoundingClientRect?.();if(!rect||rect.width>4&&rect.height>4&&rect.right>0&&rect.bottom>0) return true;
  }
  return false;
}
async function inputFrame(scene,source,rect,sendInput,alive,lease,sequence) {
  if(typeof sendInput!=='function') throw Error('input-unavailable');
  const navigation=canvasNavigation(),view=editorViewport();
  const zoom=Math.min((view.width-100)/Math.max(rect.w,1),(view.height-120)/Math.max(rect.h,1),1);
  if(!Number.isFinite(zoom)||zoom<=0) throw Error('viewport');
  navigation.setViewportZoomScale(zoom);navigation.setViewportCenter({x:rect.x+rect.w/2,y:rect.y+rect.h/2});
  await new Promise(resolve=>setTimeout(resolve,100));if(!alive()) throw Error('cancelled');
  const v=editorViewport(),screen=(x,y)=>({x:v.x+v.width/2+(x-v.offsetX)*v.zoomScale,y:v.y+v.height/2+(y-v.offsetY)*v.zoomScale});
  const input=async (operation,p={},shortcut='')=>{
    if(!alive()) throw Error('cancelled');
    const x=p.x??v.x+v.width/2,y=p.y??v.y+v.height/2;
    if(shortcut!=='escape'&&inputBlocked(x,y)) throw Error('active-input');
    const result=await canvasInput(sendInput,{inputLease:lease,sequence:++sequence.value,operation,x,y,dx:p.dx??0,dy:p.dy??0,shortcut},alive);
    if(!result?.ok) throw Error('input-rejected');
    await new Promise(resolve=>setTimeout(resolve,100));if(!alive()) throw Error('cancelled');
  };
  await input('shortcut',{},'escape');await input('shortcut',{},'escape');
  if(source) {
    const point=screen(rect.x,rect.y);point.x+=12;point.y-=12;
    await input('click',point);
    let selected=scene.getDirectlySelectedNodes?.()??[];
    if(selected.length!==1||selected[0].guid!==source.guid) {
      // Hidden or overlapping frame captions can miss. Click the contents and
      // use Figma's normal Select Parent shortcut, verifying each selection.
      await input('click',screen(rect.x+rect.w/2,rect.y+rect.h/2));
      for(let i=0;i<12;i++) {
        selected=scene.getDirectlySelectedNodes?.()??[];
        if(selected.length!==1||selected[0].guid===source.guid) break;
        let inside=false;for(let node=selected[0];node;node=node.parentNode) if(node.guid===source.guid) inside=true;
        if(!inside) break;
        await input('shortcut',{},'select_parent');
      }
      selected=scene.getDirectlySelectedNodes?.()??[];
    }
    if(selected.length!==1||selected[0].guid!==source.guid) throw Error('frame-selection');
    const parent=source.parentNode,before=new Set(parent.childrenNodes.map(node=>node.guid));
    await input('shortcut',{},'duplicate');
    for(let i=0;i<20;i++) {
      if(!alive()) throw Error('cancelled');
      const selected=scene.getDirectlySelectedNodes?.()??[];
      const candidates=parent.childrenNodes.filter(node=>node.type==='FRAME'&&!before.has(node.guid));
      if(candidates.length===1&&selected.length===1&&selected[0].guid===candidates[0].guid) return candidates[0];
      await new Promise(resolve=>setTimeout(resolve,50));
    }
    throw Error('duplicate-not-created');
  }
  const page=scene.getCurrentPage(),before=new Set(page.childrenNodes.map(node=>node.guid));
  await input('shortcut',{},'frame_tool');
  const from=screen(rect.x,rect.y),to=screen(rect.x+rect.w,rect.y+rect.h);
  await input('frame_drag',{...from,dx:to.x-from.x,dy:to.y-from.y});
  for(let i=0;i<20;i++) {
    if(!alive()) throw Error('cancelled');
    const added=page.childrenNodes.filter(node=>node.type==='FRAME'&&!before.has(node.guid));
    if(added.length===1) return added[0];
    await new Promise(resolve=>setTimeout(resolve,50));
  }
  throw Error('frame-not-created');
}
function pathWriter() {
  const {moduleWith,only,source}=navigationModules;
  const module=moduleWith('Failed to retrieve vector network, network data is invalid','Invalid StyleId','normalizedSize');
  return only(Object.values(module.exports),value=>typeof value==='function'&&source(value).includes('Invalid StyleId')&&source(value).includes('relativeTransform'));
}
function penNetwork(change) {
  const points=change.points;
  if(!Array.isArray(points)||points.length<2||points.length>256||change.closed!==undefined&&typeof change.closed!=='boolean') throw Error('path');
  const finite=value=>Number.isFinite(value)&&Math.abs(value)<=100000;
  for(const point of points) {
    if(!point||!finite(point.x)||!finite(point.y)) throw Error('path-point');
    for(const key of ['handleIn','handleOut']) if(point[key]!==undefined&&(!point[key]||!finite(point[key].x)||!finite(point[key].y))) throw Error('path-handle');
  }
  if(change.strokeWidth!==undefined&&(!Number.isFinite(change.strokeWidth)||change.strokeWidth<0.1||change.strokeWidth>128)) throw Error('stroke');
  const segments=[];
  for(let i=0;i<points.length-(change.closed?0:1);i++) {
    const end=(i+1)%points.length;
    segments.push({start:i,end,tangentStart:{...points[i].handleOut??{x:0,y:0}},tangentEnd:{...points[end].handleIn??{x:0,y:0}}});
  }
  return {vertices:points.map(({x,y})=>({x,y,strokeCap:'ROUND',strokeJoin:'ROUND',cornerRadius:0,handleMirroring:'NONE'})),segments,regions:[]};
}
// Trace cubic curves with ordinary Pen clicks. This deliberately avoids the
// private vector-network codec in collaborator mode; the result is an editable
// polyline, with curves approximated to one canvas unit.
function penTrace(network) {
  const result=[network.vertices[0]],distance=(p,a,b)=>{
    const dx=b.x-a.x,dy=b.y-a.y,n=dx*dx+dy*dy;
    const t=n?Math.max(0,Math.min(1,((p.x-a.x)*dx+(p.y-a.y)*dy)/n)):0;
    return Math.hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
  },mid=(a,b)=>({x:(a.x+b.x)/2,y:(a.y+b.y)/2});
  const curve=(a,b,c,d,depth=0)=>{
    if(result.length>=128) throw Error('pen-input-budget');
    if(Math.max(distance(b,a,d),distance(c,a,d))<=1) {result.push(d);return;}
    if(depth>=12) throw Error('pen-curve-too-large');
    const ab=mid(a,b),bc=mid(b,c),cd=mid(c,d),abc=mid(ab,bc),bcd=mid(bc,cd),m=mid(abc,bcd);
    curve(a,ab,abc,m,depth+1);curve(m,bcd,cd,d,depth+1);
  };
  for(const segment of network.segments) {
    const a=network.vertices[segment.start],d=network.vertices[segment.end];
    curve(a,{x:a.x+segment.tangentStart.x,y:a.y+segment.tangentStart.y},{x:d.x+segment.tangentEnd.x,y:d.y+segment.tangentEnd.y},d);
  }
  return result;
}
async function inputPath(scene,parent,points,sendInput,alive,lease,sequence) {
  if(typeof sendInput!=='function') throw Error('input-unavailable');
  if(parent.type==='FRAME'&&points.some(p=>p.x<0||p.y<0||p.x>parent.size.x||p.y>parent.size.y)) throw Error('pen-outside-workspace');
  const world=point=>{
    let p={x:point.x,y:point.y};
    for(let node=parent;node&&node.type!=='CANVAS';node=node.parentNode) {
      const m=node.relativeTransform??{m00:1,m01:0,m02:node.x??0,m10:0,m11:1,m12:node.y??0};
      p={x:m.m00*p.x+m.m01*p.y+m.m02,y:m.m10*p.x+m.m11*p.y+m.m12};
    }
    return p;
  };
  const coordinates=points.map(world),xs=coordinates.map(p=>p.x),ys=coordinates.map(p=>p.y);
  const rect={x:Math.min(...xs),y:Math.min(...ys),w:Math.max(...xs)-Math.min(...xs),h:Math.max(...ys)-Math.min(...ys)};
  const page=scene.getCurrentPage(),queue=[...page.childrenNodes],before=new Set();
  while(queue.length) {const node=queue.shift();before.add(node.guid);queue.push(...node.childrenNodes??[]);}
  // Input must not reach another protected frame. A fresh workspace may itself
  // live inside an older frame, whose existing descendants remain protected.
  const ancestors=new Set();for(let node=parent;node;node=node.parentNode) ancestors.add(node.guid);
  for(const id of before) {
    const node=scene.get(id),box=node.absoluteBoundingBox;
    if(node.type==='FRAME'&&!owned(node)&&!ancestors.has(id)&&box&&rect.x<=box.x+box.w&&rect.x+rect.w>=box.x&&rect.y<=box.y+box.h&&rect.y+rect.h>=box.y) throw Error('protected-pen-area');
  }
  const navigation=canvasNavigation(),v=editorViewport();
  navigation.setViewportZoomScale(Math.min((v.width-100)/Math.max(rect.w,20),(v.height-120)/Math.max(rect.h,20),8));
  navigation.setViewportCenter({x:rect.x+rect.w/2,y:rect.y+rect.h/2});
  await new Promise(resolve=>setTimeout(resolve,100));
  const view=editorViewport(),screen=p=>({x:view.x+view.width/2+(p.x-view.offsetX)*view.zoomScale,y:view.y+view.height/2+(p.y-view.offsetY)*view.zoomScale});
  const input=async(operation,p={},shortcut='')=>{
    if(!alive()) throw Error('cancelled');
    const x=p.x??view.x+view.width/2,y=p.y??view.y+view.height/2;
    if(shortcut!=='escape'&&inputBlocked(x,y)) throw Error('active-input');
    const result=await canvasInput(sendInput,{inputLease:lease,sequence:++sequence.value,operation,x,y,dx:0,dy:0,shortcut},alive);
    if(!result?.ok) throw Error('pen-input-rejected');
    await new Promise(resolve=>setTimeout(resolve,60));if(!alive()) throw Error('cancelled');
  };
  try {
    await input('shortcut',{},'escape');await input('shortcut',{},'escape');await input('shortcut',{},'pen_tool');
    for(const point of coordinates) {
      await input('click',screen(point));
      if((scene.getDirectlySelectedNodes?.()??[]).some(node=>node.type==='VECTOR'&&before.has(node.guid))) throw Error('pen-connected-existing-path');
    }
    await input('shortcut',{},'escape');await input('shortcut',{},'escape');await input('shortcut',{},'select_tool');
    for(let i=0;i<20;i++) {
      if(!alive()) throw Error('cancelled');
      const added=[],nodes=[...page.childrenNodes];
      while(nodes.length) {const node=nodes.shift();if(node.type==='VECTOR'&&!before.has(node.guid)) added.push(node);nodes.push(...node.childrenNodes??[]);}
      if(added.length===1) return added[0];
      if(added.length>1) throw Error('pen-ambiguous-result');
      await new Promise(resolve=>setTimeout(resolve,50));
    }
    throw Error('pen-path-not-created');
  } finally {
    // Leave edit mode even on a rejected step. Cancellation is enforced by the
    // host lease, so this cannot keep acting after a new summon.
    if(alive()) {try {await input('shortcut',{},'escape');} catch {}}
  }
}
window.__selectZaneEditCopy = async (request,alive=()=>true,sendInput,sequence={value:0}) => {
  let copy,original,originalBefore,parentBefore,parent,stage='editor';
  const appearance=node=>JSON.stringify((function read(node,count={n:0}) {if(++count.n>10000) throw Error('source-size');return {id:node.guid,name:node.name,type:node.type,size:node.size,transform:node.relativeTransform,fills:node.fills,text:node.type==='TEXT'?node.characters:undefined,children:(node.childrenNodes??[]).map(child=>read(child,count))};})(node));
  try {
    const scene=editorScene(),store=editorStore();
    window.__selectZaneBeginDesignSession(request.designSession);
    if(store?.getState()?.openFile?.canEdit!==true) return {ok:false,status:'The connected editor does not have edit permission.'};
    if(!Array.isArray(request.changes)||request.changes.length>100) throw Error('changes');
    const operations=new Set(['set_text','set_fill','resize','move','set_auto_layout','create_text','create_rectangle','create_path']);
    const networks=new Map();let pointCount=0;
    for(const change of request.changes) {
      if(!change||!operations.has(change.operation)) throw Error('operation');
      if(change.color!==undefined&&!/^#[0-9a-fA-F]{6}$/.test(change.color)) throw Error('color');
      for(const key of ['x','y','width','height','gap','padding']) if(change[key]!==undefined&&(!Number.isFinite(change[key])||Math.abs(change[key])>1e9)) throw Error('number');
      if(change.operation==='move'&&(change.x===undefined||change.y===undefined)) throw Error('coordinates');
      if(change.operation==='resize'&&(!(change.width>0)||!(change.height>0))) throw Error('size');
      if(change.operation==='set_auto_layout'&&!['NONE','HORIZONTAL','VERTICAL'].includes(change.layout)) throw Error('layout');
      if(change.operation==='create_path') {networks.set(change,penNetwork(change));pointCount+=change.points.length;}
    }
    if(pointCount>2048) throw Error('path-budget');
    // Resolve compatibility and validate all geometry before making a duplicate.
    const traces=new Map();
    if(request.userInput) {let clicks=0;for(const [change,network] of networks) {const trace=penTrace(network);traces.set(change,trace);clicks+=trace.length+7;}if(clicks>400) throw Error('pen-input-budget');}
    const writePath=networks.size&&!request.userInput?pathWriter():null;
    let transaction,commit;
    const prepareEdit=()=>{transaction??=editTransaction();commit??=editCommit();};
    const edit=(name,fn)=>{prepareEdit();transaction.user(name,()=>{fn();commit();});};
    if(!alive()) throw Error('cancelled');
    const mapping=new Map();
    const allowed=request.allowedRoots??request.allowedCopies??[];
    if(request.copyId) {
      if(!allowed.includes(request.copyId)) return {ok:false,status:'Original frames cannot be edited as a copy. Use frameId to prepare an editable workspace.'};
      copy=scene.get(request.copyId);if(!copy||!owned(copy,allowed)) throw Error('copy');
    } else if(request.newFrame) {
      const frame=request.newFrame;
      for(const key of ['x','y','width','height']) if(frame[key]!==undefined&&(!Number.isFinite(frame[key])||Math.abs(frame[key])>1e7)) throw Error('new-frame-size');
      if(!(frame.width>0)||!(frame.height>0)) throw Error('new-frame-size');
      const page=scene.getCurrentPage(),edge=page.childrenNodes.reduce((edge,node)=>{const box=node.absoluteBoundingBox;return Math.max(edge,box?box.x+box.w:0);},0);
      const rect={x:frame.x??edge+96,y:frame.y??0,w:frame.width,h:frame.height};
      prepareEdit();
      if(request.userInput) {
        if(page.childrenNodes.some(node=>{const box=node.absoluteBoundingBox;return box&&rect.x<box.x+box.w&&rect.x+rect.w>box.x&&rect.y<box.y+box.h&&rect.y+rect.h>box.y;})) throw Error('occupied-frame-area');
        copy=await inputFrame(scene,null,rect,sendInput,alive,request.inputLease,sequence);
      } else edit('select-zane-new-frame',()=>{copy=scene.createNode('FRAME');page.appendChild(copy);});
      frameCopies.add(copy.guid);
      edit('select-zane-place-new-frame',()=>{copy.name=(frame.name??'Zane frame').slice(0,200);copy.x=rect.x;copy.y=rect.y;copy.size={x:rect.w,y:rect.h};});
    } else if(request.canvas) copy=scene.getCurrentPage();
    else {
      const target=scene.get(request.rootId??request.frameId);if(!target) throw Error('target');
      await openPage(scene,store,target.containingCanvas,alive);designBaseline();
      for(let node=target;node;node=node.parentNode) {if(owned(node,allowed)) {copy=node;break;}if(node.type==='FRAME') break;}
      if(!copy&&unframed(target)) copy=target;
      if(!copy) {for(let node=target;node;node=node.parentNode) {if(node.type==='FRAME') {if(owned(node,allowed)) break;original=node;break;}}if(!original) throw Error('protected-target');}
      const previous=original&&copySources.get(original.guid);
      if(previous&&scene.get(previous)&&owned(scene.get(previous),allowed)&&copySnapshots.get(previous)===appearance(original)) {copy=scene.get(previous);for(const [from,to] of copyMappings.get(previous)??[]) mapping.set(from,to);original=null;}
    }
    if(copy&&copy.type!=='CANVAS') await openPage(scene,store,copy.containingCanvas,alive);
    if(!alive()) throw Error('cancelled');
    // The native clone operation preserves full Figma content and relationships.
    // Attach only to the page, outside the source's original layout container.
    stage='duplicate';
    if(original) {
      originalBefore=appearance(original);
      parent=original.parentNode;
      if(parent?.type==='FRAME') parentBefore=JSON.stringify({children:parent.childrenGuids??parent.childrenNodes.map(node=>node.guid),size:parent.size,transform:parent.relativeTransform});
      const page=scene.get(original.containingCanvas),box=original.absoluteBoundingBox;
      const edge=Math.max(box?.x??0,...page.childrenNodes.map(node=>{const b=node.absoluteBoundingBox;return b?b.x+b.w:0;}));
      prepareEdit();
      if(request.userInput) copy=await inputFrame(scene,original,box,sendInput,alive,request.inputLease,sequence);
      edit('select-zane-duplicate-frame',()=>{
        if(!request.userInput) copy=scene.get(original.clone());if(!copy||copy.guid===original.guid) throw Error('clone');
        page.appendChild(copy);copy.name=original.name+' — Zane';
        copy.x=edge+96;copy.y=box?.y??original.y;
      });
      frameCopies.add(copy.guid);duplicatedFrames.add(copy.guid);copySources.set(original.guid,copy.guid);copySnapshots.set(copy.guid,originalBefore);
      const pair=(from,to)=>{mapping.set(from.guid,to.guid);const a=from.childrenNodes??[],b=to.childrenNodes??[];if(a.length!==b.length) throw Error('clone-tree');for(let i=0;i<a.length;i++) pair(a[i],b[i]);};
      pair(original,copy);
      copyMappings.set(copy.guid,new Map(mapping));
    }
    if(copy.type==='FRAME'&&freshFrame(copy)) frameCopies.add(copy.guid);
    const within=node=>{if(copy.type==='CANVAS') return node.containingCanvas===copy.guid&&unframed(node);for(let parent=node;parent;parent=parent.parentNode) {if(parent.guid===copy.guid) return true;if(parent.type==='FRAME'&&!owned(parent,allowed)) return false;}return false;};
    const get=id=>{const node=scene.get(mapping.get(id)??id);if(!node||!within(node)) throw Error('restricted-target');return node;};
    // Validate every existing target before editing, including explicit parents.
    for(const change of request.changes) {if(change.nodeId) get(change.nodeId);if(change.parentId) get(change.parentId);}
    const fill=color=>[{type:'SOLID',color:{r:parseInt(color.slice(1,3),16)/255,g:parseInt(color.slice(3,5),16)/255,b:parseInt(color.slice(5,7),16)/255,a:1}}];
    stage='edit-copy';const created=[];
    for(const change of request.changes) {
      let inputNode;
      if(request.userInput&&change.operation==='create_path') {
        const parent=change.parentId ? get(change.parentId) : copy;
        if(!['FRAME','CANVAS','GROUP'].includes(parent.type)||parent.stackMode&&parent.stackMode!=='NONE') throw Error('path-parent');
        stage='pen-input';inputNode=await inputPath(scene,parent,traces.get(change),sendInput,alive,request.inputLease,sequence);
      }
      edit('select-zane-edit-frame-copy',()=>{
        const targetId=change.nodeId??request.rootId??request.frameId;
        let node=targetId ? get(targetId) : copy;
        if(node.type==='CANVAS'&&!change.operation.startsWith('create_')) throw Error('canvas-target');
        if(change.operation==='create_path') {
          const parent=change.parentId ? get(change.parentId) : copy;
          if(!['FRAME','CANVAS','GROUP'].includes(parent.type)||parent.stackMode&&parent.stackMode!=='NONE') throw Error('path-parent');
          node=inputNode??scene.createNode('VECTOR');
          if(inputNode&&node.parentNode?.guid!==parent.guid) throw Error('pen-wrong-parent');
          if(!inputNode) parent.appendChild(node);created.push(node.guid);
          node.name=(change.name??'Zane pen path').slice(0,200);
          node.fills=[];node.strokes=fill(change.color??'#000000');node.strokeWeight=change.strokeWidth??2;
          if(!inputNode) {node.relativeTransform={m00:1,m01:0,m02:0,m10:0,m11:1,m12:0};writePath(node,networks.get(change),scene);}
        } else if(change.operation==='create_text'||change.operation==='create_rectangle') {
          const parent=change.parentId ? get(change.parentId) : copy;
          node=scene.createNode(change.operation==='create_text'?'TEXT':'RECTANGLE');parent.appendChild(node);created.push(node.guid);
          node.name=(change.name??(change.operation==='create_text'?'Zane text':'Zane rectangle')).slice(0,200);
          if(change.operation==='create_text') node.characters=(change.text??'').slice(0,10000);
          if(change.width!==undefined&&change.height!==undefined) node.size={x:change.width,y:change.height};
          if(change.x!==undefined) node.x=change.x;if(change.y!==undefined) node.y=change.y;
          if(change.color) node.fills=fill(change.color);
        } else if(change.operation==='set_text') {
          if(node.type!=='TEXT'||node.hasMissingFont) throw Error('text-font');node.characters=(change.text??'').slice(0,10000);
        } else if(change.operation==='set_fill') {if(!change.color) throw Error('color');node.fills=fill(change.color);}
        else if(change.operation==='resize') node.size={x:change.width,y:change.height};
        else if(change.operation==='move') {node.x=change.x;node.y=change.y;}
        else if(change.operation==='set_auto_layout') {
          if(!['FRAME','SYMBOL'].includes(node.type)) throw Error('layout');node.stackMode=change.layout;
          if(change.gap!==undefined) node.stackSpacing=change.gap;
          if(change.padding!==undefined) for(const key of ['stackTopPadding','stackRightPadding','stackBottomPadding','stackLeftPadding']) node[key]=change.padding;
        }
      });
    }
    if(parentBefore&&parentBefore!==JSON.stringify({children:parent.childrenGuids??parent.childrenNodes.map(node=>node.guid),size:parent.size,transform:parent.relativeTransform})) throw Error('source-container-changed');
    if(original&&appearance(original)!==originalBefore) return {ok:false,copyId:copy.guid,originalId:original.guid,originalUnchanged:false,status:'The original changed during duplication. Stop and inspect before continuing.'};
    return {ok:true,source:request.userInput?'standard_input':'native_editor',rootId:copy.type==='CANVAS'?undefined:copy.guid,copyId:duplicatedFrames.has(copy.guid)?copy.guid:undefined,copied:!!original,originalId:original?.guid,originalUnchanged:true,nodeIdMap:Object.fromEntries(mapping),createdNodeIds:created,viewport:editorViewport()};
  } catch(error) {return {ok:false,source:request.userInput?'standard_input':'native_editor',code:error?.message,status:'The editor action did not complete. No MCP was used. Inspect any returned workspace before retrying.',rootId:copy&&copy.type!=='CANVAS'?copy.guid:undefined,copyId:copy&&duplicatedFrames.has(copy.guid)?copy.guid:undefined,originalId:original?.guid,stage};}
};
// Scripts are declarative canvas navigation, not arbitrary JS or desktop input.
// Each input is acknowledged by the native host and checked again before replay.
const inputAcks=new Map();let inputSerial=0;
window.__selectZaneInputAck=(id,result)=>inputAcks.get(id)?.(result);
function canvasInput(sendInput,event,alive) {
  return new Promise(resolve=>{
    const id=++inputSerial;
    const timer=setTimeout(()=>{inputAcks.delete(id);resolve({ok:false,status:'Canvas input timed out.'});},2000);
    inputAcks.set(id,result=>{clearTimeout(timer);inputAcks.delete(id);try {resolve(alive()?result:{ok:false,status:'Request cancelled.'});} catch {resolve({ok:false,status:'Request cancelled.'});}});
    try {sendInput({inputId:id,...event});} catch {clearTimeout(timer);inputAcks.delete(id);resolve({ok:false,status:'Native canvas input is unavailable.'});}
  });
}
window.__selectZaneCanvasActions=async (request,alive=()=>true,sendInput) => {
  let completed=0;
  try {
    const actions=request.actions,shortcuts=new Set(['fit_page','fit_selection','zoom_100','zoom_in','zoom_out']);
    if(!Array.isArray(actions)||!actions.length||actions.length>32||typeof sendInput!=='function') throw Error('actions');
    let duration=0;
    for(const action of actions) {
      if(!action||!['pan_drag','zoom_wheel','shortcut','pause'].includes(action.operation)) throw Error('operation');
      for(const key of ['x','y','dx','dy','milliseconds']) if(action[key]!==undefined&&!Number.isFinite(action[key])) throw Error('number');
      if(action.operation==='pan_drag'&&(!Number.isFinite(action.dx)||!Number.isFinite(action.dy)||Math.abs(action.dx)>2000||Math.abs(action.dy)>2000)) throw Error('pan');
      if(action.operation==='zoom_wheel'&&(!Number.isFinite(action.dy)||Math.abs(action.dy)>600||action.dy===0)) throw Error('wheel');
      if(action.operation==='shortcut'&&!shortcuts.has(action.shortcut)) throw Error('shortcut');
      if(action.operation==='pause'&&(!Number.isInteger(action.milliseconds)||action.milliseconds<0||action.milliseconds>1000)) throw Error('pause');
      duration+=action.operation==='pause'?action.milliseconds:100;
    }
    if(duration>10000) throw Error('duration');
    const path=location.pathname,page=editorScene().getCurrentPage()?.guid;
    const active=()=>alive()&&location.pathname===path&&editorScene().getCurrentPage()?.guid===page;
    for(const action of actions) {
      if(!active()) throw Error('cancelled');
      if(action.operation==='pause') await new Promise(resolve=>setTimeout(resolve,action.milliseconds));
      else {
        const view=editorViewport(),x=action.x??view.x+view.width/2,y=action.y??view.y+view.height/2;
        const inside=(x,y)=>Number.isFinite(x)&&Number.isFinite(y)&&x>=view.x+2&&y>=view.y+2&&x<view.x+view.width-2&&y<view.y+view.height-2;
        if(!inside(x,y)||action.operation==='pan_drag'&&!inside(x+action.dx,y+action.dy)) throw Error('canvas-bounds');
        const blocked=element=>{
          const control=element?.closest?.('input,textarea,button,[contenteditable="true"],[role="dialog"],[role="menu"]');
          if(!control) return false;
          // Figma keeps an invisible textarea focused to receive canvas input.
          // A visible chat/text field must retain the user's keystrokes.
          if(control.matches?.('input,textarea')&&control.getAttribute?.('aria-hidden')==='true') return false;
          const rect=control.getBoundingClientRect?.();
          return !rect||rect.width>4&&rect.height>4&&rect.right>0&&rect.bottom>0;
        };
        if(blocked(document.activeElement)||blocked(document.elementFromPoint?.(x,y))) return {ok:false,completed,status:'A text field or overlay is active. Finish typing or use navigate instead.'};
        const result=await canvasInput(sendInput,{inputLease:request.inputLease,sequence:completed+1,operation:action.operation,x,y,dx:action.dx??0,dy:action.dy??0,shortcut:action.shortcut??''},active);
        if(!result?.ok) return {ok:false,completed,status:result?.status??'Canvas input was rejected.'};
        // Let the real editor consume input and render before the next action.
        await new Promise(resolve=>setTimeout(resolve,100));
      }
      if(!active()) throw Error('cancelled');
      completed++;
    }
    return {ok:true,completed,viewport:editorViewport(),status:'Canvas action sequence completed. Inspect view_canvas to verify the result.'};
  } catch {return {ok:false,completed,status:'Canvas actions stopped: invalid sequence, changed page or cancelled request.'};}
};
// Model code runs on its own thread, without editor objects, storage or network.
// Only validated input requests cross back into the editor.
function behaviorWorker(run) {
  const send=globalThis.postMessage.bind(globalThis),pending=new Map();let serial=0;
  const blocked=['fetch','XMLHttpRequest','WebSocket','WebSocketStream','WebTransport','EventSource','Worker','SharedWorker','importScripts','indexedDB','caches','navigator','BroadcastChannel','FontFace','RTCPeerConnection','Function','eval'];
  for(let object=globalThis;object;object=Object.getPrototypeOf(object)) for(const key of blocked)
    if(object===globalThis||Object.hasOwn(object,key))Object.defineProperty(object,key,{value:undefined,writable:false,configurable:false});
  for(const fn of [()=>{},async()=>{},function*(){},async function*(){}])Object.defineProperty(Object.getPrototypeOf(fn),'constructor',{value:undefined,writable:false,configurable:false});
  for(const key of ['setTimeout','setInterval']) {const timer=globalThis[key].bind(globalThis);Object.defineProperty(globalThis,key,{value:(fn,...args)=>{if(typeof fn!=='function')throw Error('string timers unavailable');return timer(fn,...args);},writable:false,configurable:false});}
  const call=(method,args)=>new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});send({type:'call',id,method,args});});
  const zane=Object.freeze({
    key:key=>call('key',[key]),click:(x,y)=>call('click',[x,y]),move:(x,y)=>call('move',[x,y]),
    drag:(x,y,toX,toY,options={})=>call('drag',[x,y,toX,toY,options]),
    wheel:dy=>call('wheel',[dy]),wait:ms=>call('wait',[ms]),state:()=>call('state',[])
  });
  globalThis.onmessage=async({data})=>{
    if(data.type==='reply') {const p=pending.get(data.id);if(p){pending.delete(data.id);data.ok?p.resolve(data.value):p.reject(Error(data.error));}return;}
    if(data.type==='run') {try {send({type:'done',result:JSON.stringify(await run(zane))?.slice(0,8000)??'null'});} catch(error){send({type:'failed',error:String(error?.message??error).slice(0,300)});}}
  };
  send({type:'ready'});
}
window.__selectZaneScriptBehavior=async(request,alive=()=>true,sendInput)=>{
  let worker,url,root,workspace,completed=0,lastStep='compile',stopped=false,finish,timer,poll;
  const sequence={value:0},design=request.mode==='design',navigationKeys=new Set(['Shift+1','Shift+2','Shift+0','Shift++','-']);
  const editingKeys=new Set(['Escape','Enter','Shift+Enter','P','Shift+P','V','F','R','O','L','T','Delete','Backspace']);
  const path=location.pathname;
  let scene,page,before=new Set();
  const current=()=>alive()&&!stopped&&location.pathname===path&&scene.getCurrentPage()?.guid===page;
  const pageNodes=()=>{const nodes=[],queue=[...scene.getCurrentPage().childrenNodes];while(queue.length&&nodes.length<20000){const node=queue.shift();nodes.push(node);queue.push(...node.childrenNodes??[]);}return nodes;};
  const state=()=>({viewport:editorViewport(),rootId:root?.type==='CANVAS'?undefined:root?.guid??workspace?.rootId,bounds:root?.absoluteBoundingBox,
    selection:(scene.getDirectlySelectedNodes?.()??[]).map(node=>({id:node.guid,type:node.type})),
    createdNodeIds:pageNodes().filter(node=>!before.has(node.guid)).map(node=>node.guid)});
  const within=node=>{if(!root) return false;if(root.type==='CANVAS') return node.containingCanvas===root.guid&&unframed(node);for(let n=node;n;n=n.parentNode){if(n.guid===root.guid)return true;if(n.type==='FRAME'&&!owned(n,request.allowedRoots??[]))return false;}return false;};
  const project=(x,y)=>{
    if(!Number.isFinite(x)||!Number.isFinite(y)||Math.abs(x)>1e7||Math.abs(y)>1e7) throw Error('invalid coordinates');
    if(design&&root.type==='FRAME'&&(x<0||y<0||x>root.size.x||y>root.size.y)) throw Error('point outside editable frame');
    let p={x,y};
    if(design) for(let node=root;node&&node.type!=='CANVAS';node=node.parentNode){const m=node.relativeTransform??{m00:1,m01:0,m02:node.x??0,m10:0,m11:1,m12:node.y??0};p={x:m.m00*p.x+m.m01*p.y+m.m02,y:m.m10*p.x+m.m11*p.y+m.m12};}
    const v=editorViewport();return {x:v.x+v.width/2+(p.x-v.offsetX)*v.zoomScale,y:v.y+v.height/2+(p.y-v.offsetY)*v.zoomScale,world:p};
  };
  const input=async(operation,p={},shortcut='')=>{
    if(!current()) throw Error('script cancelled or page changed');
    const v=editorViewport(),x=p.x??v.x+v.width/2,y=p.y??v.y+v.height/2;
    const inside=(x,y)=>x>=v.x+2&&y>=v.y+2&&x<v.x+v.width-2&&y<v.y+v.height-2;
    if(!inside(x,y)||!inside(x+(p.dx??0),y+(p.dy??0))) throw Error('point outside visible canvas; navigate first');
    if(shortcut!=='key:Escape'&&inputBlocked(x,y)) throw Error('a text field or overlay is active');
    if(design&&shortcut!=='key:Escape'&&(scene.getDirectlySelectedNodes?.()??[]).some(node=>!within(node))) throw Error('selection outside editable workspace; Escape first');
    const ack=await canvasInput(sendInput,{inputLease:request.inputLease,sequence:++sequence.value,operation,x,y,dx:p.dx??0,dy:p.dy??0,shortcut},current);
    if(!ack?.ok) throw Error(ack?.status??'browser rejected input');
    await new Promise(resolve=>setTimeout(resolve,80));if(!current()) throw Error('script cancelled or page changed');
    return state();
  };
  const handle=async(method,args)=>{
    if(!current()) throw Error('script cancelled or page changed');
    if(!Array.isArray(args)||++completed>256) throw Error('script step budget exceeded');
    lastStep=method==='key'?'key '+String(args[0]).slice(0,40):method;
    if(method==='state') return state();
    if(method==='wait') {if(!Number.isInteger(args[0])||args[0]<0||args[0]>2000)throw Error('wait must be 0–2000 ms');await new Promise(resolve=>setTimeout(resolve,args[0]));if(!current())throw Error('script cancelled');return state();}
    if(method==='key') {const key=args[0];if(!navigationKeys.has(key)&&(!design||!editingKeys.has(key)))throw Error('unsupported key: '+String(key).slice(0,40));return input('shortcut',{},'key:'+key);}
    if(method==='wheel') {if(!Number.isFinite(args[0])||!args[0]||Math.abs(args[0])>600)throw Error('wheel must be between -600 and 600');return input('zoom_wheel',{dy:args[0]});}
    if(!['click','move','drag'].includes(method)) throw Error('unsupported script API');
    if(method==='click'&&!design) throw Error('click needs mode=design');
    const from=project(args[0],args[1]);let to=from,operation=method,shortcut='';
    if(method==='drag') {
      const options=args[4]??{};if(!options||typeof options!=='object'||!['left','middle'].includes(options.button??'left'))throw Error('invalid drag button');
      if((options.button??'left')==='left'&&!design)throw Error('left drag needs mode=design');
      if(options.alt!==undefined&&typeof options.alt!=='boolean'||options.shift!==undefined&&typeof options.shift!=='boolean')throw Error('invalid drag modifiers');
      to=project(args[2],args[3]);operation=options.button==='middle'?'pan_drag':'design_drag';shortcut=[options.alt?'Alt':'',options.shift?'Shift':''].filter(Boolean).join('+');
    }
    if(design&&method!=='move'&&operation!=='pan_drag') {
      const rect={x:Math.min(from.world.x,to.world.x),y:Math.min(from.world.y,to.world.y),w:Math.abs(to.world.x-from.world.x),h:Math.abs(to.world.y-from.world.y)};
      const ancestors=new Set();for(let n=root;n;n=n.parentNode)ancestors.add(n.guid);
      for(const node of pageNodes()){const b=node.absoluteBoundingBox;if(node.type==='FRAME'&&!owned(node,request.allowedRoots??[])&&!ancestors.has(node.guid)&&b&&rect.x<=b.x+b.w&&rect.x+rect.w>=b.x&&rect.y<=b.y+b.h&&rect.y+rect.h>=b.y)throw Error('input reaches a protected frame');}
    }
    return input(operation,{x:from.x,y:from.y,dx:to.x-from.x,dy:to.y-from.y},shortcut);
  };
  try {
    scene=editorScene();page=scene.getCurrentPage()?.guid;
    if(typeof request.code!=='string'||!request.code.trim()||request.code.length>20000||/\bimport\s*(?:\(|['"{*])/.test(request.code))throw Error('supply JavaScript without imports, up to 20000 characters');
    if(!['navigation','design'].includes(request.mode??'navigation')||typeof sendInput!=='function')throw Error('invalid script mode or unavailable input');
    const source='('+behaviorWorker.toString()+')(async function(zane){"use strict";\n'+request.code+'\n});';
    url=URL.createObjectURL(new Blob([source],{type:'text/javascript'}));worker=new Worker(url);
    const done=new Promise(resolve=>{finish=resolve;});
    const stop=result=>{if(stopped)return;stopped=true;worker.terminate();finish(result);};
    timer=setTimeout(()=>stop({ok:false,error:'script exceeded 25 seconds',step:lastStep}),25000);
    poll=setInterval(()=>{if(!alive()||location.pathname!==path)stop({ok:false,error:'script cancelled',step:lastStep});},100);
    let chain=Promise.resolve(),calls=0,started=false;
    worker.onerror=event=>{event.preventDefault?.();stop({ok:false,error:String(event.message??'worker blocked by editor policy').slice(0,300),step:lastStep});};
    worker.onmessage=({data})=>{
      if(stopped||!data||typeof data!=='object')return;
      if(data.type==='ready') {
        if(started)return;started=true;
        chain=chain.then(async()=>{
          if(!alive())throw Error('script cancelled');
          if(design) {
            lastStep='prepare workspace';
            const canvas=request.canvas||!request.frameId&&!request.rootId&&!request.newFrame;
            workspace=await window.__selectZaneEditCopy({...request,canvas,changes:[]},alive,sendInput,sequence);
            if(!workspace.ok){lastStep+=' / '+(workspace.stage??'editor');throw Error(workspace.code??workspace.status);}
            root=scene.get(workspace.rootId??scene.getCurrentPage().guid);
            if(root.type!=='CANVAS') {const b=root.absoluteBoundingBox;if(b){const v=editorViewport(),nav=canvasNavigation();nav.setViewportZoomScale(Math.min((v.width-100)/Math.max(b.w,1),(v.height-120)/Math.max(b.h,1),2));nav.setViewportCenter({x:b.x+b.w/2,y:b.y+b.h/2});}}
          }
          page=scene.getCurrentPage().guid;before=new Set(pageNodes().map(node=>node.guid));lastStep='run';
          worker.postMessage({type:'run'});
        }).catch(error=>stop({ok:false,error:String(error.message).slice(0,300),step:lastStep}));
      } else if(data.type==='call') {
        if(!Number.isInteger(data.id)||data.id<1||++calls>256){stop({ok:false,error:'invalid script request or step budget exceeded',step:lastStep});return;}
        chain=chain.then(async()=>{try{const value=await handle(data.method,data.args);if(!stopped)worker.postMessage({type:'reply',id:data.id,ok:true,value});}catch(error){stop({ok:false,error:String(error.message).slice(0,300),step:lastStep});}});
      } else if(data.type==='done') chain=chain.then(()=>stop({ok:true,result:typeof data.result==='string'?data.result.slice(0,8000):'null'}));
      else if(data.type==='failed') stop({ok:false,error:String(data.error).slice(0,300),step:lastStep});
    };
    const result=await done;
    return {...workspace,...result,completed,...state(),status:result.ok?'Script executed. Verify created nodes or view_canvas before claiming success.':'Script stopped at '+(result.step??lastStep)+': '+result.error};
  } catch(error) {return {...workspace,ok:false,completed,step:lastStep,status:'Script could not start: '+String(error.message).slice(0,300)};}
  finally {stopped=true;clearTimeout(timer);clearInterval(poll);worker?.terminate();if(url)URL.revokeObjectURL(url);}
};
window.__selectZaneTool = async (action,args,alive=()=>true,sendInput) => {
  if (!alive()) return {ok:false,status:'Request cancelled.'};
  if (action==='list_pages') return window.__selectZanePages();
  if (action==='find_nodes') return window.__selectZaneFind(args);
  if (action==='read_structure') return window.__selectZaneStructure(args,alive);
  if (action==='organize_file') return window.__selectZaneOrganize(args,alive);
  if (action==='navigate') return window.__selectZaneNavigate(args,alive);
  if (action==='edit_frame_copy'||action==='edit_design') return window.__selectZaneEditCopy(args,alive,sendInput);
  if (action==='canvas_actions') return window.__selectZaneCanvasActions(args,alive,sendInput);
  if (action==='script_behavior') return window.__selectZaneScriptBehavior(args,alive,sendInput);
  if (action==='inspect_file') {
    const comments=[...document.querySelectorAll('[data-testid*="comment"],[class*="comment_body"],[class*="commentMessage"]')].filter(el=>el.getClientRects().length).map(el=>(el.innerText??el.textContent??'').slice(0,1000)).slice(0,30);
    return {ok:true,title:document.title,comments,commentCoverage:'Only comments loaded in this editor, not a complete file comment list.',visibleText:document.body.innerText.slice(0,12000),pages:window.__selectZanePages(),viewport:editorViewport()};
  }
  if (action==='point_at') {
    const view=editorViewport();
    const x=args.x,y=args.y;if(!Number.isFinite(x)||!Number.isFinite(y)||Math.abs(x)>1e9||Math.abs(y)>1e9) return {ok:false,status:'Invalid cursor position.'};
    window.__selectZaneActorPoint=args.space==='canvas'?{x,y}:{x:(x+32-view.x-view.width/2)/view.zoomScale+view.offsetX,y:(y+20-view.y-view.height/2)/view.zoomScale+view.offsetY};
    return {ok:true,status:'Zane’s cursor moved. Design layers and your pointer are unchanged.',viewport:view};
  }
  return {ok:false,status:'Unsupported local tool.'};
};
window.__selectZaneAccountTool = async (id,action,args) => {
  const path=location.pathname,requestSummon=summonRequested;
  if(summonTask) {try{await summonTask;}catch{}}
  if(action==='navigate'||action==='read_structure'||action==='organize_file'||action==='edit_frame_copy'||action==='edit_design'||action==='canvas_actions'||action==='script_behavior') delete window.__selectZaneActorPoint;
  const result=await window.__selectZaneTool(action,args,()=>location.pathname===path&&window.__selectZaneAccountActive!==false&&summonRequested===requestSummon,event=>window.__selectZaneSend('select-zane-account:'+JSON.stringify({type:'input',id,...event})));
  window.__selectZaneSend('select-zane-account:'+JSON.stringify({type:'tool',id,...result}));
};
window.__selectZaneNavigate = async (request,alive=()=>true) => {
  let stage='editor';
  try {
    if (!alive()) throw Error('cancelled');
    const validId=id=>typeof id==='string'&&/^\d+:\d+$/.test(id);
    if (!Array.isArray(request.nodeIds??[])) throw Error('ids');
    const ids=[...new Set(request.nodeIds??[])];
    if (ids.length>50||ids.some(id=>!validId(id))) throw Error('ids');
    if (request.zoomFactor!==undefined&&(!Number.isFinite(request.zoomFactor)||request.zoomFactor<0.1||request.zoomFactor>10)) throw Error('zoom');
    for (const point of [request.center,request.pan]) if (point&&!['x','y'].every(key=>Number.isFinite(point[key])&&Math.abs(point[key])<=1e9)) throw Error('point');
    if (request.fit!==undefined&&request.fit!=='page') throw Error('fit');
    const needsScene=!!(request.pageId||request.pageName||ids.length||request.fit);
    const scene=needsScene ? editorScene() : null,store=needsScene ? editorStore() : null;
    const pages=needsScene ? editorPages(scene,store) : [];
    let pageId=request.pageId;
    stage='resolve-page';
    if (request.pageName) {
      if (typeof request.pageName!=='string') throw Error('name');
      const matches=pages.filter(page=>page.name.toLocaleLowerCase()===request.pageName.trim().toLocaleLowerCase());
      if (matches.length!==1) return {ok:false,status:matches.length?'More than one page has that name. Choose an ID from list_pages.':'Page name not found. Use list_pages for the exact name and ID.',pages};
      if (pageId&&pageId!==matches[0].id) throw Error('page');
      pageId=matches[0].id;
    }
    if (pageId&&(!validId(pageId)||!pages.some(page=>page.id===pageId))) return {ok:false,status:'Page ID not found in this editor. Use list_pages or pageName instead.',pages};
    if (!pageId&&ids.length) {
      const node=scene.get(ids[0]);
      pageId=node?.containingCanvas;
      if (!pageId||!pages.some(page=>page.id===pageId)) throw Error('layer-page');
    }
    const navigation=canvasNavigation();
    stage='switch-page';
    if (pageId) await openPage(scene,store,pageId,alive);
    if (!alive()) return {ok:false,status:'Navigation cancelled.'};
    stage='focus';
    if (ids.length) {
      if (ids.some(id=>!scene.get(id)||['CANVAS','DOCUMENT'].includes(scene.get(id).type)||scene.get(id).containingCanvas!==scene.getCurrentPage()?.guid)) throw Error('layer');
      navigation.scrollAndZoomIntoView(ids);
    }
    stage='viewport';
    if (request.fit==='page') {
      const nodes=scene.getCurrentPage()?.childrenNodes??[];
      const boxes=nodes.map(node=>node.absoluteBoundingBox).filter(box=>box&&['x','y','w','h'].every(key=>Number.isFinite(box[key]))&&box.w>0&&box.h>0);
      if (!boxes.length) return {ok:false,status:'This page has no loaded objects to fit in view.',pageId};
      const left=Math.min(...boxes.map(b=>b.x)),right=Math.max(...boxes.map(b=>b.x+b.w)),top=Math.min(...boxes.map(b=>b.y)),bottom=Math.max(...boxes.map(b=>b.y+b.h));
      const view=editorViewport();
      navigation.setViewportZoomScale(Math.max(0.01,Math.min(1,(view.width-96)/(right-left),(view.height-96)/(bottom-top))));
      navigation.setViewportCenter({x:(left+right)/2,y:(top+bottom)/2});
    }
    if (request.zoomFactor!==undefined) navigation.setViewportZoomScale(Math.max(0.01,Math.min(256,editorViewport().zoomScale*request.zoomFactor)));
    if (request.center) navigation.setViewportCenter(request.center);
    if (request.pan) {const v=editorViewport();navigation.setViewportCenter({x:v.offsetX+request.pan.x/v.zoomScale,y:v.offsetY+request.pan.y/v.zoomScale});}
    return {ok:true,status:'Navigated in your editor; design layers are unchanged.',pageId:scene?.getCurrentPage?.()?.guid,viewport:editorViewport()};
  } catch {return {ok:false,status:'Navigation could not complete.',stage};}
};
})();
