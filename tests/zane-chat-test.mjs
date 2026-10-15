import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';

// Exercise the actual bridge with a deterministic clock and native IM commits.
function fixture() {
  let time = 0, sequence = 0, cursorState;
  const timers = new Map(), handlers = new Map(), messages = [];
  const field = (kind, value='') => ({kind, value, matches:()=>kind !== 'body',
    closest:()=>kind === 'chat' ? {} : null, getAttribute:()=>null,
    getBoundingClientRect:()=>({width:kind==='hidden'?1:200,height:30,left:0,top:0,right:200,bottom:30})});
  const document = {activeElement:field('body'), addEventListener(name, handler, options) {
    const entries = handlers.get(name) ?? []; entries.push(handler); handlers.set(name,entries);
    options.signal.addEventListener('abort',()=>handlers.set(name,(handlers.get(name)??[]).filter(value=>value!==handler)));
  }};
  const window = {__selectZaneSend(value) {messages.push(JSON.parse(value.slice('select-zane:test:'.length)));},
    __selectZaneCursorFactory:notify=>({update(state){cursorState=state;notify(true);},
      near:point=>Math.hypot(point.x-cursorState.x,point.y-cursorState.y)<=220, dispose(){}})};
  vm.runInNewContext(readFileSync(new URL('../data/zane.js',import.meta.url),'utf8').replace('__BRIDGE_NONCE__','"test"'),
    {window,document,AbortController,innerWidth:1200,innerHeight:900,getComputedStyle:()=>({opacity:'1',visibility:'visible'}),
      setTimeout(fn,delay){const id=++sequence;timers.set(id,{fn,at:time+delay});return id;},clearTimeout:id=>timers.delete(id)});
  return {bridge:window.__selectZane, messages, document, field,window,state:()=>cursorState,
    event(name, props={}){for(const handler of handlers.get(name)??[])handler({...props});},
    advance(ms){time+=ms;for(const[id,task]of [...timers])if(task.at<=time){timers.delete(id);task.fn();}},
    prompts:()=>messages.filter(value=>['wake','chat'].includes(value.type))};
}
{
  const f=fixture(); f.document.activeElement=f.field('hidden','/');
  f.bridge.nativeKey('/'); f.bridge.commit('hey zane organize the comments'); f.advance(1199);
  assert.equal(f.prompts().length,0); f.advance(1);
  assert.equal(f.prompts()[0].prompt,'organize the comments');
  f.bridge.nativeKey('Enter'); assert.equal(f.prompts().length,1,'Enter after automatic submit does not repeat it');
}
{
  const f=fixture(); f.document.activeElement=f.field('hidden'); f.bridge.nativeKey('/'); f.bridge.commit('hey zane hello');
  f.bridge.nativeKey('Enter'); assert.equal(f.prompts()[0].prompt,'hello');
  f.bridge.present({visible:true,busy:true,x:100,y:100});
  f.event('pointermove',{clientX:100,clientY:100}); f.bridge.nativeKey('Escape');
  f.bridge.nativeKey('/'); f.bridge.commit('also rename the pages'); f.advance(1200);
  assert.equal(f.prompts().at(-1).type,'chat'); assert.equal(f.prompts().at(-1).prompt,'also rename the pages');
  f.bridge.present({visible:true,busy:false,x:100,y:100}); f.bridge.nativeKey('Escape');
  f.bridge.nativeKey('/'); f.bridge.commit("yo you're free to go"); f.bridge.nativeKey('Enter');
  assert.equal(f.prompts().at(-1).prompt,"yo you're free to go");
  f.bridge.present({visible:false}); f.bridge.nativeKey('/'); f.bridge.commit('are you still there'); f.advance(1200);
  assert.equal(f.prompts().length,3,'Leaving disables implicit follow-ups');
  f.bridge.nativeKey('Escape'); f.bridge.nativeKey('/'); f.bridge.commit('hey zane come back'); f.bridge.nativeKey('Enter');
  assert.equal(f.prompts().at(-1).type,'wake'); assert.equal(f.prompts().at(-1).prompt,'come back');
}
{
  const f=fixture(); f.document.activeElement=f.field('hidden','/');
  f.bridge.nativeKey('/'); f.bridge.commit('hey zane hello'); f.bridge.nativeKey('Enter');
  f.bridge.present({visible:true,busy:true,x:80,y:100});
  f.bridge.commit('ok'); f.bridge.nativeKey('Enter'); f.bridge.commit('ok'); f.bridge.nativeKey('Enter');
  assert.deepEqual(f.prompts().map(item=>item.prompt),['hello','ok','ok'],'Enter starts a fresh bubble, including repeated confirmations');
}
{
  const f=fixture(); f.bridge.present({visible:true,busy:true,x:100,y:100});
  f.event('pointermove',{clientX:1000,clientY:800}); f.document.activeElement=f.field('hidden');
  f.bridge.nativeKey('/'); f.bridge.commit('this is for someone else'); f.advance(1200);
  assert.equal(f.prompts().length,0,'Distant cursor chat stays addressed to other people');
  f.bridge.nativeKey('Escape'); f.document.activeElement=f.field('form'); f.bridge.nativeKey('/'); f.bridge.commit('hey zane form text'); f.advance(1200);
  assert.equal(f.prompts().length,0,'Form fields are excluded even near Zane');
}
{
  const f=fixture(); f.document.activeElement=f.field('chat'); f.bridge.nativeKey('/');
  f.document.activeElement.value='hey zane selected text'; f.event('input');
  f.document.activeElement.value='hey zane replacement'; f.event('input'); f.bridge.nativeKey('Enter');
  assert.equal(f.prompts()[0].prompt,'replacement');
  f.bridge.nativeKey('Escape'); f.document.activeElement.value='hey zane 日本語'; f.event('compositionstart'); f.event('input'); f.advance(2000);
  assert.equal(f.prompts().length,1,'Composition is never submitted halfway through');
  f.event('compositionend'); f.advance(1200); assert.equal(f.prompts().at(-1).prompt,'日本語');
  f.document.activeElement.value='hey zane pending'; f.event('input'); f.bridge.dispose(); f.advance(1200);
  assert.equal(f.prompts().length,2,'Disposal cancels pending auto-submission');
}
{
  const f=fixture();f.document.activeElement=f.field('chat');f.bridge.nativeKey('/');
  const full='hey zane '+('organize this page and keep my complete instructions. '.repeat(8));
  f.document.activeElement.value=full.slice(0,50);f.bridge.commit(full);
  assert.equal(f.document.activeElement.maxLength,4000,'Addressed native input is expanded past Figma’s 50-character limit');
  f.bridge.nativeKey('Enter');assert.equal(f.prompts()[0].prompt,full.slice(9).trim(),'A capped DOM field must not discard captured keystrokes');
}
{
  const f=fixture();const second='This thought is deliberately longer so it stays readable.';
  const third='A longer phrase may exceed sixty characters without losing any words or punctuation.';
  f.bridge.present({visible:true,x:100,y:100,speech:{id:1,messages:['Hi.',second,third]}});
  assert.equal(f.state().text,'Hi.');f.advance(1799);assert.equal(f.state().text,'Hi.');
  f.advance(1);assert.equal(f.state().text,second);
  const duration=Math.max(1800,1200+50*Array.from(second).length);
  f.advance(duration-1);assert.equal(f.state().text,second);f.advance(1);assert.equal(f.state().text,third,'Model-selected chunks are not truncated');
  f.advance(1200+50*Array.from(third).length);assert.equal(f.state().text,'','Final bubble clears after its reading time');
  f.bridge.present({visible:true,x:100,y:100,speech:{id:2,messages:['Waiting.',second]}});
  f.document.activeElement=f.field('hidden');f.bridge.nativeKey('/');f.bridge.commit('hey zane come here');f.bridge.nativeKey('Enter');
  const count=f.messages.filter(message=>message.type==='speech').length;f.advance(10000);
  assert.equal(f.messages.filter(message=>message.type==='speech').length,count,'A new message cancels unfinished speech');
}
{
  const f=fixture();f.bridge.present({visible:true,x:900,y:700});
  f.window.__selectZaneLocation=()=>({pageId:'2:1',x:150,y:240,zoom:0.5});
  f.event('pointermove',{clientX:100,clientY:120});f.document.activeElement=f.field('hidden');
  f.bridge.nativeKey('/');f.bridge.commit('hey zane');f.bridge.nativeKey('Enter');
  assert.equal(f.prompts().at(-1).type,'wake');assert.equal(f.prompts().at(-1).summon.pageId,'2:1');
  assert.equal(f.prompts().at(-1).summon.x,150,'Explicit wake carries the caller’s page and world position even far away');
}
{
  const f=fixture();const messages=['I’m here.','Now you can ask me something.'];
  f.bridge.present({visible:true,account_wanted:true,account:false,speech:{id:5,wait_account:true,messages}});
  f.advance(5000);assert.equal(f.messages.filter(value=>value.type==='speech').length,0,'The reply clock waits while the account is travelling');
  f.bridge.present({visible:true,account_wanted:true,account:true,speech:{id:5,wait_account:true,messages}});
  assert.equal(f.state().text,messages[0]);f.advance(1800);assert.equal(f.state().text,messages[1]);
}
console.log('Cursor-chat follow-ups, pause/Enter, proximity, forms, composition and leave/re-wake: PASS');
