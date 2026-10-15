(() => {
  window.__selectZane?.dispose();
  const controller = new AbortController();
  const listen = (type, handler) => document.addEventListener(type, handler, {capture:true, signal:controller.signal});
  const nonce = __BRIDGE_NONCE__;
  const nativeSend = window.__selectZaneSend;
  const send = payload => nativeSend('select-zane:' + nonce + ':' + JSON.stringify(payload));
  const inputStyle=document.createElement?.('style');
  if(inputStyle) {inputStyle.textContent='.select-zane-addressed-chat{max-width:min(320px,calc(100vw - 96px))!important;width:100%!important}';(document.head??document.body)?.append(inputStyle);}
  let armed = false, timer, last = '', draft = '', pointer = {x:80,y:100};
  let cursor, cursorFailed = false, composing = false, addressed = false, afterEnter = false, nativeDraft = false, overflow = false;
  let presence = {visible:false, busy:false}, accessRevision = 0, speechId=-1, speechTimer, speaking=false, speechText="", speechMessages=[], waitingSpeech=-1;
  const editable = el => el && (el.matches('input,textarea') || el.isContentEditable);
  const cursorChat = el => el?.closest('[data-testid*="cursor-chat"],[class*="cursor_chat"],[class*="cursorChat"],[class*="cursor-chat"],[class*="chat_input--"],[aria-label="Cursor chat"]');
  const visibleField = el => {
    if (!editable(el) || cursorChat(el)) return false;
    const rect = el.getBoundingClientRect(), style = getComputedStyle(el);
    return rect.width > 4 && rect.height > 4 && rect.right > 0 && rect.bottom > 0 && rect.left < innerWidth && rect.top < innerHeight && style.opacity !== '0' && style.visibility !== 'hidden' && el.getAttribute('aria-hidden') !== 'true';
  };
  const near = () => {if(!presence.visible) return false;try{return cursor?.near(pointer)??Math.hypot(pointer.x-presence.x,pointer.y-presence.y)<=220;}catch{return false;}};
  const clearDraft = () => { armed = false; addressed = false; afterEnter = false; nativeDraft = false; overflow = false; draft = ''; last = ''; clearTimeout(timer); };
  const extendChat = () => {
    const el=document.activeElement;
    if (!editable(el)||!cursorChat(el)||!(/^hey[\s,]+zane\b/i.test(draft||el.value||'')||(presence.visible&&(addressed||near())))) return;
    // Figma's own chat input has maxLength=50. Keep its native UI, but permit
    // a longer addressed draft before its browser limit discards keystrokes.
    el.maxLength=4000; el.classList?.add('select-zane-addressed-chat');
  };
  const dispatch = value => {
    if (composing || (!armed && !cursorChat(document.activeElement))) return;
    const text = value.trim();
    if (!text || text === last) return;
    const wake = text.match(/^hey[\s,]+zane\b[\s,:!]*(.*)$/is);
    if (!wake && !(presence.visible && (addressed || near()))) return;
    if(wake&&cursorFailed) {cursor?.dispose();cursor=null;cursorFailed=false;}
    last = text;
    clearTimeout(speechTimer);speaking=false;waitingSpeech=-1;
    let summon; if(wake) {try{summon=window.__selectZaneLocation?.(pointer);}catch{}}
    send({type:wake ? 'wake' : 'chat', prompt:wake ? wake[1].trim() : text, ...pointer, summon});
    return true;
  };
  const consider = (immediate = false) => {
    clearTimeout(timer);
    if (composing || (!armed && !cursorChat(document.activeElement))) return;
    const content = () => {
      const el = document.activeElement;
      // Figma's hidden carrier may retain only the slash/last key. Only the
      // actual chat field can override the accumulated native IM draft.
      if (!editable(el)||!cursorChat(el)) return draft;
      const value=el.value??el.innerText??'';
      if (nativeDraft&&draft.length>value.length&&draft.startsWith(value)&&(overflow||value.length>=50)) {overflow=true;return draft;}
      // DOM selection/replacement is authoritative when it differs from the
      // buffered prefix. A capped field must never truncate the native draft.
      return value;
    };
    if (immediate) {const value=content();return dispatch(value)||(!!last&&value.trim()===last);} else timer = setTimeout(() => dispatch(content()),1200);
  };
  const key = value => {
    if (value === '/' && !armed && !visibleField(document.activeElement)) {
      armed = true; last = ''; draft = ''; nativeDraft = false; overflow = false; addressed = near();
    }
    if (value === 'Escape') clearDraft();
    if (value === 'Enter' && !composing && consider(true)) { draft = ''; nativeDraft = false; overflow = false; afterEnter = true; }
  };
  listen('pointermove', e => { pointer = {x:e.clientX,y:e.clientY}; });
  listen('pointerdown', e => {
    if (e.target?.closest?.('#select-zane-native,#select-zane-avatar') || cursorChat(e.target)) return;
    clearDraft();
  });
  listen('keydown', e => { if (!e.ctrlKey && !e.metaKey && !e.altKey && !e.isComposing) key(e.key); });
  listen('input', () => {extendChat();consider();});
  listen('beforeinput', e => {
    if (e.inputType!=='insertFromPaste'||!cursorChat(document.activeElement)) return;
    const value=e.dataTransfer?.getData('text/plain')??e.data;
    if (typeof value!=='string') return;
    const el=document.activeElement,base=nativeDraft ? draft : el.value??'';
    const start=el.selectionStart??base.length,end=el.selectionEnd??start;
    draft=(base.slice(0,start)+value+base.slice(end)).slice(0,4000);nativeDraft=true;extendChat();consider();
  });
  listen('compositionstart', () => { composing = true; clearTimeout(timer); });
  listen('compositionend', () => { composing = false; consider(); });
  const updateCursor = state => {
    try {
      if (!cursor && !cursorFailed) cursor=window.__selectZaneCursorFactory(native=>{cursorFailed=!native;send({type:'cursor',native});},payload=>send(typeof payload==='string'?{type:payload}:payload));
      cursor?.update(state);
    } catch(error) {cursor?.dispose();cursor=null;cursorFailed=true;send({type:'cursor',native:false,detail:String(error?.message??'renderer')});}
  };
  const playSpeech = (id,messages) => {
    clearTimeout(speechTimer);waitingSpeech=-1;speechId=id;speechMessages=messages;speaking=messages.length>0;
    const show=index=>{
      if(controller.signal.aborted||!presence.visible||speechId!==id) return;
      const text=speechMessages[index]??'';speechText=text;
      updateCursor({...presence,text});
      send({type:'speech',id,index});
      if(index<speechMessages.length) speechTimer=setTimeout(()=>show(index+1),Math.max(1800,1200+50*Array.from(text).length));
      else speaking=false;
    };
    if(speaking) show(0);
  };
  const unique = values => [...new Set(values.map(s => s.trim()).filter(Boolean))];
  window.__selectZane = Object.freeze({
    nativeKey(value) {
      if (value === 'Backspace' && armed) { draft = [...draft].slice(0,-1).join(''); consider(); }
      else key(value);
    },
    commit(value) {
      if (!armed || (value === '/' && !draft)) return;
      if (afterEnter) {
        if (!value.trim()) return;
        last = ''; afterEnter = false;
      }
      draft = (draft + value).slice(0,4000); nativeDraft=true;extendChat();consider();
    },
    present(state) {
      presence = {...state};
      if (state.auto_access===false) ++accessRevision;
      if (!state.visible) {clearDraft(); ++accessRevision;clearTimeout(speechTimer);speaking=false;waitingSpeech=-1;}
      if (!state.visible && !cursor) return;
      const speech=state.speech;
      if(!state.visible) {clearTimeout(speechTimer);speaking=false;waitingSpeech=-1;}
      if(speech&&speech.id!==speechId&&speech.wait_account&&state.account_wanted&&!state.account&&speech.messages?.length) {
        updateCursor({...state,text:'Coming over…'});
        if(waitingSpeech!==speech.id) {
          clearTimeout(speechTimer);speaking=false;waitingSpeech=speech.id;
          // Connection trouble must not permanently hide the reply. Show a
          // local preview after ten seconds if the account still cannot arrive.
          speechTimer=setTimeout(()=>{if(waitingSpeech===speech.id&&presence.visible&&!controller.signal.aborted) playSpeech(speech.id,presence.speech?.messages??speech.messages);},10000);
        }
        return;
      }
      if(speech&&speech.id!==speechId) {
        playSpeech(speech.id,Array.isArray(speech.messages)?speech.messages:[]);
        if(speaking) return;
      }
      // Timed chunks remain authoritative while ordinary presence/busy updates arrive.
      if(speech&&speech.id===speechId) speechMessages=Array.isArray(speech.messages)?speech.messages:[];
      updateCursor(speaking ? {...state,text:speechText} : state);
    },
    async navigate(id,args,action="navigate") {
      const path=location.pathname;
      let result;
      try {result=await window.__selectZaneTool(action,args,()=>presence.visible&&!controller.signal.aborted&&location.pathname===path,event=>send({type:'input',id,...event}));}
      catch {result={ok:false,status:'Navigation is unavailable in this editor.'};}
      if (!controller.signal.aborted) send({type:'navigation',id,...result});
    },
    modeLocation(id) {
      let summon;try {summon=window.__selectZaneLocation?.(pointer);}catch{}
      send({type:'mode_location',id,...pointer,summon});
    },
    async ensureAccess(fileKey,email,id) {
      const revision=++accessRevision;
      const result=await window.__selectZaneShareEditor({fileKey,email},()=>revision===accessRevision&&presence.visible&&presence.auto_access!==false&&!controller.signal.aborted);
      send({type:'access',email,id,...result});
    },
    captureHidden(value) { cursor?.captureHidden(value); },
    dispose() { controller.abort(); clearTimeout(timer); clearTimeout(speechTimer);cursor?.dispose(); inputStyle?.remove(); },
    inspect(id) {
      const visible = el => !!el.getClientRects().length && getComputedStyle(el).visibility !== 'hidden';
      const commentElements = [...document.querySelectorAll('[data-testid*="comment"],[class*="comment_body"],[class*="commentBody"],[class*="comment-message"],[class*="commentMessage"],[aria-label*="Comment"]')].filter(visible);
      const comments = unique(commentElements.map(el => el.innerText || el.textContent || '')).slice(0, 30).map(s => s.slice(0, 1000));
      const selection = unique([...document.querySelectorAll('[aria-selected="true"],[data-selected="true"]')].filter(visible).map(el => el.innerText || el.getAttribute('aria-label') || '')).slice(0, 20).map(s => s.slice(0, 500));
      const own = ['#select-zane-native','#select-zane-avatar'].map(id=>document.querySelector(id)).filter(Boolean).map(element=>({element,hidden:element.hidden}));
      for (const {element} of own) element.hidden = true;
      const visibleText = document.body.innerText.slice(0, 12000);
      for (const {element,hidden} of own) element.hidden = hidden;
      send({type:'context', id, title:document.title.slice(0, 1000), url:location.href.slice(0, 2000), selection, comments,
        commentCoverage:'Only comments currently loaded in the visible Figma interface; this is not a complete file comment list.',
        visibleText, width:innerWidth, height:innerHeight});
    }
  });
})();
