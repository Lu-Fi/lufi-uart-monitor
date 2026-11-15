#pragma once
/* live view: polls /data, decodes T30<->XR871 frames, redacts WiFi config */
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lu-Fi UART Monitor</title>
<link rel="icon" type="image/png" href="/favicon.png">
<style>
:root{--bg:#f3f4f6;--panel:#fff;--fg:#1b1f24;--mut:#68717d;--bd:#d8dce2;--bd2:#e8ebef;--acc:#2f6fde;--acc-bg:#e8f0fd;--ok:#1f9d55;--err:#d93848;
--con:#0e1116;--con-fg:#d5dae1;--con-bd:#262c36;--con-hd:#161b22;--sh:0 1px 2px rgba(16,24,40,.06),0 1px 3px rgba(16,24,40,.08)}
@media (prefers-color-scheme:dark){:root{--bg:#0b0d10;--panel:#14181e;--fg:#e6e9ed;--mut:#8b94a1;--bd:#2a313b;--bd2:#20262e;--acc:#5b93f0;--acc-bg:#1a2638;--sh:0 1px 2px rgba(0,0,0,.4)}}
*{box-sizing:border-box}[hidden]{display:none!important}
html,body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
a{color:var(--acc);text-decoration:none}
button,input,select{font:inherit;color:inherit}
.bar{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;align-items:center;gap:10px 18px;padding:10px 20px;background:var(--panel);border-bottom:1px solid var(--bd);box-shadow:var(--sh)}
.brand{display:flex;align-items:center;gap:10px;font-weight:600;font-size:15px;letter-spacing:.2px}
.logo{width:48px;height:48px;display:block;margin:-6px 0}
.brand small{display:block;font-weight:400;font-size:11.5px;color:var(--mut)}
.tabs{display:flex;gap:2px;padding:3px;border:1px solid var(--bd);border-radius:9px;background:var(--bg)}
.tabs a{padding:4px 14px;border-radius:6px;color:var(--mut);font-weight:500}
.tabs a.on{background:var(--panel);color:var(--fg);box-shadow:var(--sh)}
.pills{display:flex;flex-wrap:wrap;gap:6px;margin-left:auto}
.pill{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border:1px solid var(--bd);border-radius:999px;background:var(--bg);color:var(--mut);font-size:12.5px;white-space:nowrap}
.dot{width:8px;height:8px;border-radius:50%;background:var(--mut)}.dot.ok{background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 25%,transparent)}.dot.err{background:var(--err)}
main{max-width:1600px;margin:0 auto;padding:16px 20px 24px;display:flex;flex-direction:column;gap:16px}
.card{background:var(--panel);border:1px solid var(--bd);border-radius:12px;box-shadow:var(--sh);overflow:hidden}
.card>h2,.card>summary{margin:0;padding:11px 16px;font-size:13px;font-weight:600;text-transform:uppercase;letter-spacing:.6px;color:var(--mut);border-bottom:1px solid var(--bd2)}
details.card>summary{cursor:pointer;list-style:none;border-bottom:0}details.card[open]>summary{border-bottom:1px solid var(--bd2)}
details.card>summary::before{content:"\25B8";display:inline-block;margin-right:8px;transition:transform .15s}details.card[open]>summary::before{transform:rotate(90deg)}
.tools{display:flex;flex-wrap:wrap;align-items:center;gap:8px;padding:10px 12px;border-bottom:1px solid var(--bd2)}
.sep{width:1px;align-self:stretch;background:var(--bd2);margin:0 4px}
.chip{display:inline-flex;align-items:center;gap:7px;padding:4px 11px;border:1px solid var(--bd);border-radius:999px;cursor:pointer;user-select:none;font-size:13px;color:var(--mut);background:var(--panel)}
.chip input{position:absolute;opacity:0;pointer-events:none}
.chip:has(input:checked){color:var(--fg);border-color:var(--acc);background:var(--acc-bg)}
.chip:has(input:focus-visible){outline:2px solid var(--acc);outline-offset:2px}
.chip i{width:9px;height:9px;border-radius:50%;background:var(--bd)}
.chip:has(input:checked) i{background:var(--k,var(--acc))}
.btn{padding:5px 13px;border:1px solid var(--bd);border-radius:8px;background:var(--panel);cursor:pointer;font-size:13px;font-weight:500}
.btn:hover{border-color:var(--acc);color:var(--acc)}.btn.pri{background:var(--acc);border-color:var(--acc);color:#fff}.btn.pri:hover{filter:brightness(1.08);color:#fff}
.right{margin-left:auto;display:flex;gap:8px}
.con{--fg:var(--con-fg);--bg:var(--con);margin:12px;border:1px solid var(--con-bd);border-radius:10px;background:var(--con);overflow:hidden;box-shadow:inset 0 1px 0 rgba(255,255,255,.03)}
.con-hd{display:flex;align-items:center;gap:8px;padding:7px 12px;background:var(--con-hd);border-bottom:1px solid var(--con-bd);color:#8b949e;font:12px ui-monospace,monospace}
.con-hd b{width:10px;height:10px;border-radius:50%;background:#3a414c;transition:background .3s,box-shadow .3s}.con-hd b.on{background:var(--c);box-shadow:0 0 6px var(--c);transition:none}.con-hd b.st{background:var(--c)}.con-hd span{margin-left:6px}
input[type=text],input[type=number],input[type=password],input:not([type]),select{padding:5px 9px;border:1px solid var(--bd);border-radius:8px;background:var(--panel);min-width:0}
input:focus,select:focus{outline:2px solid color-mix(in srgb,var(--acc) 40%,transparent);border-color:var(--acc)}
@media (max-width:640px){.bar{padding:10px 16px}main{padding:12px 16px}.pills{margin-left:0}.right{margin-left:0}.con{margin:8px}}

#log{font:12.5px/1.5 ui-monospace,"SF Mono",Menlo,Consolas,monospace;color:var(--con-fg);padding:10px 14px;white-space:pre-wrap;word-break:break-all;height:calc(100vh - 300px);min-height:240px;overflow-y:auto}
.send{display:flex;flex-wrap:wrap;align-items:center;gap:8px;padding:8px 10px;border-top:1px solid var(--con-bd);background:var(--con-hd);font:12.5px ui-monospace,monospace;color:#8b949e}
.send input:not([type]),.send select{background:var(--con);color:var(--con-fg);border-color:var(--con-bd);font:13px ui-monospace,monospace}
.send #stx{flex:1;min-width:160px}.send .pr{color:#4cc27f;font-weight:700;transition:color .3s,text-shadow .3s}.send .pr.tx{color:#e0a050;text-shadow:0 0 6px #e0a050;transition:none}.send .pr.err{color:#e5534b;text-shadow:0 0 6px #e5534b;transition:none}
.send #sres{min-width:110px}.send #sres.err{color:#e5534b}
#log div:hover{background:rgba(255,255,255,.035)}
#log .A{color:#4cc27f}#log .B{color:#6ea4f5}#log .C{color:#e0a050}#log .x{color:#ff6b7a}#log .t{color:#5d6672}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px;padding:14px 16px 16px}
.box{border:1px solid var(--bd2);border-radius:10px;padding:12px 14px}
.box h3{margin:0 0 10px;font-size:13px;font-weight:600}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin:6px 0}
.row label.k{width:22px;font-weight:600}
.hint{color:var(--mut);font-size:12.5px;margin:8px 0 0}
code{font:12px ui-monospace,monospace;background:var(--bg);border:1px solid var(--bd2);border-radius:5px;padding:1px 5px}
</style></head><body>
<div class="bar">
 <div class="brand"><img class="logo" src="/logo.webp" alt="UART Monitor logo"><div>Lu-Fi UART Monitor<small>3-channel serial monitor · ESP32</small></div></div>
 <nav class="tabs"><a class="on" id="tm" href="/" title="Live view of all channels">Monitor</a><a href="/term" title="Interactive console for one channel">Terminal</a><a id="ts" href="/settings" title="Channels, baud rates, WiFi, access point">Settings</a><a href="/help" title="Quick guide">Help</a></nav>
 <div class="pills"><span class="pill" title="Network: green = ESP32 reachable. Shows the home WiFi address and signal strength, or the ESP32's own access point"><span class="dot" id="sd"></span><span id="sw">…</span></span>
 <span class="pill" id="su" title="Time since the ESP32 started">up –</span><span class="pill" id="sb" title="Bytes received per channel since the ESP32 started. If a number does not grow, nothing arrives on that RX pin">bytes –</span><span class="pill" id="sq" hidden title="Heartbeat frames hidden by the &quot;hide heartbeats&quot; filter"></span></div>
</div>
<main>
<section class="card" id="mon">
 <div class="tools">
  <label class="chip" title="Show or hide this channel in the view (reception continues)" style="--k:#4cc27f"><input type="checkbox" id="fA" checked><i></i><span id="n0">A</span></label>
  <label class="chip" title="Show or hide this channel in the view (reception continues)" style="--k:#6ea4f5"><input type="checkbox" id="fB" checked><i></i><span id="n1">B</span></label>
  <label class="chip" title="Show or hide this channel in the view (reception continues)" style="--k:#e0a050"><input type="checkbox" id="fC" checked><i></i><span id="n2">C</span></label>
  <span class="sep"></span>
  <label class="chip" title="Hide the periodic heartbeat/ping frames of binary protocols (aa 55 aa 55 …), the count is shown at the top"><input type="checkbox" id="hb" checked><i></i>hide heartbeats</label>
  <label class="chip" title="interpret control characters: colours, CR overwrite, backspace, erase line, UTF-8"><input type="checkbox" id="ansi"><i></i>ANSI</label>
  <label class="chip" title="Freeze the view. Reception continues and is shown when you un-pause"><input type="checkbox" id="pause"><i></i>pause</label>
  <div class="right"><button class="btn" id="clr" title="Empty the view. Stays empty after reload (F5) until the ESP32 restarts">Clear</button><button class="btn pri" id="save" title="Download the current view as a text file">Save log</button></div>
 </div>
 <div class="con"><div class="con-hd"><b id="l0" title="A receive" style="--c:#4cc27f"></b><b id="l1" title="B receive" style="--c:#6ea4f5"></b><b id="l2" title="C receive" style="--c:#e0a050"></b><span>live · all channels</span></div><div id="log"></div>
 <div class="send"><span class="pr" id="spr" title="Lights up yellow while sending, red if the send failed">❯</span><select id="sch" title="TX pin that sends the line"><option value="0">TX A</option><option value="1">TX B</option></select><input id="stx" autocomplete="off" spellcheck="false" placeholder="type a line and press Enter · hex:aa55… for raw bytes" title="Enter sends the line. ↑/↓ = previous lines. hex:… sends raw bytes, e.g. hex:aa55aa55"><select id="seol" title="Line ending added to text (not to hex:)"><option value="crlf">CR LF</option><option value="cr">CR</option><option value="lf">LF</option><option value="none">none</option></select><button class="btn pri" id="sbt" title="Send the line (or press Enter). TX is driven only for this send">Send</button><span id="sres" title="Result of the last send"></span></div></div>
</section>
<section class="card" id="set" hidden><h2>Settings</h2>
 <div class="grid">
  <div class="box" style="grid-column:1/-1"><h3>Channels</h3><div id="cfg"></div><p class="hint"><b>Keys to</b>: where terminal/telnet keystrokes of that channel are sent. Channel C has no TX pin of its own. <b>TX on</b>: drive the TX pin permanently (only while the device is powered).</p></div>
  <form class="box" method="post" action="/wifi"><h3>Home WiFi</h3>
   <p class="hint" id="wst" style="margin:0 0 6px" title="Network the ESP32 is set up for (the password is never shown)">…</p>
   <div class="row"><input name="ssid" title="Name of your home WiFi" placeholder="SSID" maxlength="32" style="flex:1"> <input name="pass" type="password" title="Password of your home WiFi, stored only on the ESP32" placeholder="password" maxlength="64" style="flex:1"></div>
   <div class="row"><button class="btn pri" title="Save the home WiFi and restart the ESP32">Save + restart</button> <button class="btn" name="forget" value="1" title="Delete the saved home WiFi and restart (access point only)">Forget</button></div></form>
  <div class="box"><h3>Access point</h3>
   <p class="hint" id="apst" style="margin:0 0 6px">…</p>
   <div class="row"><select id="apmode" title="The ESP32's own WiFi network (192.168.4.1)"><option value="auto">auto: only without home WiFi</option><option value="on">always on</option></select><span class="hint" id="apres" style="margin:0"></span></div>
   <p class="hint">Automatic: comes on 30 s after the home WiFi is lost (or not set up), goes off again once the home WiFi is back and nobody has used it for 60 s.</p></div>
  <div class="box"><h3>Access</h3>
   <p class="hint">TCP <code>:2300</code> all channels (tools/uartmon.py) · <code>:2301-2303</code> raw A/B/C · telnet <code>:2321-2323</code> console · <code>/term</code> web terminal · <code>POST /update</code> firmware</p></div>
 </div>
</section>
</main>
<script>
const NAMES={0x00:'hello',0x02:'ping',0x12:'version',0x13:'factory-default',0x18:'basic-params',
0x19:'version+uuid',0x21:'force-iframe',0x40:'info/clock',0x83:'power/sleep',0x85:'wake-source',
0xa2:'audio-test',0xa4:'fw-update',0xa9:'heartbeat',0xc0:'wifi-config',0xc4:'probe-pin',
0xc5:'ready-pin',0xf1:'marker'};
const QUIET=new Set([0xa9,0x02]);
const RXP=[16,18,21],TXP=[17,19];
function chLabel(i,c){return 'ABC'[i]+' · '+(c&&c.name?c.name:'RX GPIO'+RXP[i])}
function keysTo(t){return t<0?'nowhere (read only)':'TX '+'AB'[t]+' (GPIO'+TXP[t]+')'}
function blink(e){e.classList.add('on');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('on'),90)}
function mark(k){try{const v=JSON.parse(localStorage.getItem(k));return v}catch(e){return null}}
async function boot(){const s=await(await fetch('/status')).json();return{h:s.head,b:Date.now()/1000-s.uptime}}
function valid(v,st){return v&&Math.abs(v.b-st.b)<120&&v.s<=st.h}
const CH='ABC',MAGIC=[0xaa,0x55,0xaa,0x55];
/* the same page serves /settings: show the settings card instead of the monitor */
const SET=location.pathname==='/settings';
if(SET){document.getElementById('mon').hidden=true;document.getElementById('set').hidden=false;
 document.getElementById('tm').className='';document.getElementById('ts').className='on';
 document.title='Lu-Fi UART Settings';document.querySelector('.brand div').firstChild.textContent='Lu-Fi UART Settings'}
const log=document.getElementById('log');let next=-300,lines=[],quiet=0;
function hex(a){return Array.from(a,b=>b.toString(16).padStart(2,'0')).join(' ')}
function ts(ms){return (ms/1000).toFixed(3).padStart(9)}
/* per-channel stream parser, same rules as tools/uartmon.py */
function P(ch){return{ch,mi:0,f:null,text:'',tt:0,last:0}}
const ps=[P(0),P(1),P(2)];
function out(ch,t,html){lines.push({ch,html:`<span class="t">${ts(t)}</span> <span class="${CH[ch]}">${CH[ch]}</span> ${html}`})}
function esc(s){return s.replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
function flushText(p){
 if(p.cells&&p.cells.length){out(p.ch,p.tt,cellsHtml(p.cells));p.cells=[];p.col=0;p.text=''}
 else if(p.text){out(p.ch,p.tt,esc(p.text));p.text=''}}
/* optional ANSI interpretation (checkbox "ANSI"): per channel a line of
 * cells {c,s} with cursor column, SGR state and a small escape parser */
const ansiBox=document.getElementById('ansi');
ansiBox.checked=true;try{if(localStorage.getItem('ansi')==='0')ansiBox.checked=false}catch(e){}
ansiBox.onchange=()=>{try{localStorage.setItem('ansi',ansiBox.checked?'1':'0')}catch(e){}};
const PAL=['#000','#c23','#2a2','#b80','#36c','#a3a','#299','#aaa','#666','#f45','#3c3','#db2','#59f','#d5d','#3cc','#fff'];
function c256(n){if(n<16)return PAL[n];if(n>=232){const v=8+10*(n-232);return`rgb(${v},${v},${v})`}
 n-=16;const f=x=>x?55+40*x:0;return`rgb(${f(Math.floor(n/36))},${f(Math.floor(n/6)%6)},${f(n%6)})`}
function sgr(p,ps){const s=p.sty;if(!ps.length)ps=[0];
 for(let i=0;i<ps.length;i++){const v=ps[i];
  if(v==0){s.fg=s.bg=null;s.b=s.u=s.it=s.inv=s.dim=false}
  else if(v==1)s.b=true;else if(v==2)s.dim=true;else if(v==3)s.it=true;else if(v==4)s.u=true;else if(v==7)s.inv=true;
  else if(v==22){s.b=s.dim=false}else if(v==23)s.it=false;else if(v==24)s.u=false;else if(v==27)s.inv=false;
  else if(v>=30&&v<=37)s.fg=PAL[v-30];else if(v>=90&&v<=97)s.fg=PAL[v-82];else if(v==39)s.fg=null;
  else if(v>=40&&v<=47)s.bg=PAL[v-40];else if(v>=100&&v<=107)s.bg=PAL[v-92];else if(v==49)s.bg=null;
  else if((v==38||v==48)&&ps[i+1]==5){const c=c256(ps[i+2]&255);v==38?s.fg=c:s.bg=c;i+=2}
  else if((v==38||v==48)&&ps[i+1]==2){const c=`rgb(${ps[i+2]|0},${ps[i+3]|0},${ps[i+4]|0})`;v==38?s.fg=c:s.bg=c;i+=4}}
 let fg=s.fg,bg=s.bg;if(s.inv){[fg,bg]=[bg||'var(--fg)',fg||'var(--bg)']}
 p.css=(fg?`color:${fg};`:'')+(bg?`background:${bg};`:'')+(s.b?'font-weight:bold;':'')+(s.dim?'opacity:.6;':'')+
  (s.it?'font-style:italic;':'')+(s.u?'text-decoration:underline;':'')}
function cellsHtml(cs){let h='',cur=null,buf='';
 const end=()=>{if(buf)h+=cur?`<span style="${cur}">${esc(buf)}</span>`:esc(buf);buf=''};
 for(const x of cs){const c=x?x.c:' ',s=x?x.s:'';if(s!==cur){end();cur=s}buf+=c}end();return h}
function put(p,c,t){if(!p.cells.length&&!p.col)p.tt=t;while(p.cells.length<p.col)p.cells.push(null);p.cells[p.col++]={c,s:p.css};
 if(p.cells.length>400)flushText(p)}
function csi(p,seq){const fin=seq.slice(-1),ps=seq.slice(0,-1).replace(/^[?>=]/,'').split(';').filter(x=>x!=='').map(Number),n=ps[0]||1;
 if(fin=='m')sgr(p,ps);
 else if(fin=='K'){if(!ps[0])p.cells.length=Math.min(p.cells.length,p.col);else if(ps[0]==2){p.cells=[];}}
 else if(fin=='G')p.col=Math.max(0,n-1);else if(fin=='C')p.col+=n;else if(fin=='D')p.col=Math.max(0,p.col-n)}
function ansiByte(p,b,t){
 if(!p.cells){p.cells=[];p.col=0;p.sty={};p.css='';p.u8=[]}
 if(p.esc!==undefined&&p.esc!==null){
  const c=String.fromCharCode(b);
  if(p.esc===''){p.esc=c=='['?'[':c==']'?']':null;return}
  if(p.esc[0]=='['){p.esc+=c;if(b>=0x40&&b<=0x7e){csi(p,p.esc.slice(1));p.esc=null}else if(p.esc.length>40)p.esc=null;return}
  if(p.esc[0]==']'){if(b==7||(b==0x5c&&p.esc.endsWith('\x1b'))||p.esc.length>200)p.esc=null;else p.esc+=c;return}
  p.esc=null}
 if(b==27){p.esc='';return}
 if(b==10){p.cr=false;flushText(p);return}
 if(b==13){p.cr=true;return}
 if(p.cr){p.cr=false;p.col=0}
 if(b==8){p.col=Math.max(0,p.col-1);return}
 if(b==9){do put(p,' ',t);while(p.col%8);return}
 if(b==0||b==7)return;
 if(b<32||b==127){put(p,'^'+String.fromCharCode(b^64),t);return}
 if(b<128){p.u8=[];put(p,String.fromCharCode(b),t);return}
 p.u8.push(b);const l=p.u8[0]>=0xf0?4:p.u8[0]>=0xe0?3:p.u8[0]>=0xc0?2:1;
 if(p.u8.length>=l){put(p,new TextDecoder().decode(new Uint8Array(p.u8)),t);p.u8=[]}}
function textByte(p,b,t){
 if(ansiBox.checked)return ansiByte(p,b,t);
 if(p.cells&&p.cells.length)flushText(p);
 if(b==0)return;
 if(!p.text)p.tt=t;
 if(b==10){flushText(p);return}
 if(b==13)return;
 p.text+=(b>=32&&b<127)?String.fromCharCode(b):`<${b.toString(16).padStart(2,'0')}>`;
 if(p.text.length>200)flushText(p)}
function frame(p,f,t){
 const plen=f[4]|f[5]<<8,cmd=f[6],par=f.length>=12?(f[10]|f[11]<<8):null,pl=f.slice(12);
 if(document.getElementById('hb').checked&&QUIET.has(cmd)){quiet++;return}
 let s=`cmd 0x${cmd.toString(16).padStart(2,'0')} ${NAMES[cmd]||'?'}`+(par!==null?` p=${par}`:' (8-byte)')+` len=${plen}`;
 if(cmd==0xc0&&plen)s+=` ssid ${pl[0]} B, psk ${pl[33]} B <span class="x">[redacted]</span>`;
 else if(cmd==0xf1&&plen)s+=` "${esc(String.fromCharCode(...pl.filter(b=>b>=32&&b<127)))}"`;
 else if(cmd==0x85&&plen>=4)s+=` reason 0x${((pl[0]|pl[1]<<8|pl[2]<<16|pl[3]<<24)>>>0).toString(16)}`;
 else if(plen)s+=' '+hex(pl.slice(0,32))+(plen>32?' …':'');
 flushText(p);out(p.ch,t,s)}
function feed(p,b,t){
 if(p.f){
  if(p.f.length==8&&(p.f[4]|p.f[5]<<8)==0&&t-p.last>20){frame(p,p.f,p.ft);p.f=null}
  else{
   p.f.push(b);p.last=t;const n=p.f.length,plen=p.f[4]|p.f[5]<<8;
   if(n>=6&&plen>116){const j=p.f;p.f=null;j.forEach(x=>textByte(p,x,t));return}
   if(n==12&&plen==0&&MAGIC.every((m,i)=>p.f[8+i]==m)){frame(p,p.f.slice(0,8),p.ft);p.f=MAGIC.slice();p.ft=t;return}
   if(n>=6&&n==12+plen){frame(p,p.f,p.ft);p.f=null}
   return}}
 if(b==MAGIC[p.mi]){if(++p.mi==4){p.mi=0;p.f=MAGIC.slice();p.ft=t;p.last=t}return}
 for(let i=0;i<p.mi;i++)textByte(p,MAGIC[i],t);
 p.mi=b==MAGIC[0]?1:0;if(!p.mi)textByte(p,b,t)}
function render(){
 if(!lines.length)return;
 const at=log.scrollTop+log.clientHeight>=log.scrollHeight-20,fr=document.createDocumentFragment();
 for(const l of lines){if(!document.getElementById('f'+CH[l.ch]).checked)continue;
  const d=document.createElement('div');d.innerHTML=l.html;d.dataset.ch=l.ch;fr.appendChild(d)}
 lines=[];log.appendChild(fr);
 while(log.childNodes.length>5000)log.removeChild(log.firstChild);
 if(at)log.scrollTop=log.scrollHeight}
async function poll(){
 try{const r=await fetch('/data?from='+next);next=+r.headers.get('X-Next');
  const a=new Uint8Array(await r.arrayBuffer());let i=0;
  while(i+7<=a.length&&a[i]==0xa5){const ch=a[i+1],t=a[i+2]|a[i+3]<<8|a[i+4]<<16|a[i+5]<<24,n=a[i+6];
   if(ch<3){blink(leds[ch]);for(let k=0;k<n;k++)feed(ps[ch],a[i+7+k],t>>>0)}i+=7+n}
  if(!document.getElementById('pause').checked)render();
  setTimeout(poll,a.length?50:300)}catch(e){setTimeout(poll,1000)}}
const $=id=>document.getElementById(id);
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return(d?d+'d ':'')+(d||h?h+'h ':'')+m+'m '+(s%60)+'s'}
async function status(){
 try{const s=await(await fetch('/status')).json();
  $('sd').className='dot ok';
  $('sw').textContent=s.sta_ip?`WiFi ${s.sta_ip} · ${s.rssi} dBm`:`AP ${s.ap}`;
  const w=$('wst');w.textContent='';const b=t=>{const e=document.createElement('b');e.textContent=t;return e};
  if(!s.sta_ssid)w.append('Not set up: only the access point is available.');
  else if(s.sta_ip)w.append('Connected to ',b(s.sta_ssid),` · ${s.rssi} dBm · ${s.sta_ip}`+(s.rssi<-78?' · weak signal':''));
  else w.append('Set up for ',b(s.sta_ssid),' · not connected');
  const a=$('apst');a.textContent='';
  if(s.ap_on)a.append(b(s.ap),` · ${s.ap_ip} · ${s.ap_clients} client${s.ap_clients==1?'':'s'}`+(s.ap_mode=='auto'?' · on because the home WiFi is not connected':''));
  else a.append(b(s.ap),' · off (home WiFi connected)');
  if(document.activeElement!==$('apmode'))$('apmode').value=s.ap_mode;
  $('apmode').disabled=!s.sta_ssid;$('apmode').title=s.sta_ssid?"The ESP32's own WiFi network (192.168.4.1)":'Always on while no home WiFi is set up';
  $('su').textContent='up '+dur(s.uptime);
  $('sb').textContent='bytes '+s.ch.map((c,i)=>CH[i]+' '+c.bytes).join(' · ');
  $('sq').hidden=!quiet;$('sq').textContent=quiet+' heartbeats hidden';
  const c=$('cfg');if(!c.dataset.done){c.dataset.done=1;
   c.innerHTML=s.ch.map((x,i)=>`<div class="row"><label class="k">${CH[i]}</label>`+
    `<input id=nm${i} maxlength=24 title="Your label for this channel, e.g. the connected device. Shown in the chips and the terminal" placeholder="name, e.g. router" value="${x.name}" style="width:150px">`+
    `<span class="hint" style="margin:0;width:62px" title="ESP32 pin that receives this channel: connect it to the device's TX">RX ${x.rx_pin}</span>`+
    `<input type=number id=b${i} title="Baud rate (1200 – 3000000). Garbled text usually means a wrong baud rate" value=${x.baud} style="width:100px"> baud `+
    `<label class="hint" style="margin:0" title="Which TX pin the terminal/telnet keystrokes of this channel go out on. &quot;nowhere&quot; = read only">keys to <select id=k${i}>${[0,1,-1].map(t=>`<option value=${t} ${t==x.ctx?'selected':''}>${keysTo(t)}</option>`).join('')}</select></label> `+
    (i<2?`<label class="chip" title="Drive this TX pin permanently. Only while the device is powered: a driven pin can back-power an unpowered board"><input type=checkbox id=t${i} ${x.tx?'checked':''}><i></i>TX on</label>`:'<span class="hint" style="margin:0" title="Channel C has no TX pin of its own">receive only</span>')+'</div>').join('')+
    '<div class="row"><button class="btn pri" id=ap title="Save name, baud rate, keys to and TX on for all channels (kept after restart)">Apply</button></div>';
   $('ap').onclick=async()=>{const q=[0,1,2].map(i=>`baud${i}=${$('b'+i).value}&ctx${i}=${$('k'+i).value}&name${i}=${encodeURIComponent($('nm'+i).value)}`).concat([0,1].map(i=>`tx${i}=${$('t'+i).checked?1:0}`)).join('&');
    const r=await fetch('/cfg?'+q);$('ap').textContent=r.ok?'Saved ✓':'Error';setTimeout(()=>$('ap').textContent='Apply',1500);c.dataset.done='';status()}}
  s.ch.forEach((x,i)=>{$('n'+i).textContent=chLabel(i,x);leds[i].title=chLabel(i,x)+' receive'});
  [0,1].forEach(i=>$('sch').options[i].textContent='TX '+'AB'[i]+(s.ch[i].name?' · '+s.ch[i].name:''));
 }catch(e){$('sd').className='dot err';$('sw').textContent='offline'}setTimeout(status,2000)}
setInterval(()=>ps.forEach(p=>{const t=performance.now(),has=p.text||(p.cells&&p.cells.length);if(has&&p.textAge&&t-p.textAge>500){flushText(p);render()}p.textAge=has?(p.textAge||t):0}),250);
const leds=[0,1,2].map(i=>$('l'+i));
/* Clear survives F5: remember the ring position, replay only what came after */
$('clr').onclick=async()=>{log.textContent='';lines=[];try{const st=await boot();localStorage.setItem('clr_mon',JSON.stringify({s:next>=0?next:st.h,b:st.b}))}catch(e){}};
$('save').onclick=()=>{const b=new Blob([log.innerText],{type:'text/plain'}),a=document.createElement('a');a.href=URL.createObjectURL(b);a.download="lufi-uart.log";a.click()};
/* send bar: Enter sends, no dialogs, ↑/↓ history, choices remembered */
const shist=[],EOL={crlf:'\r\n',cr:'\r',lf:'\n',none:''};let sh=0;
try{const v=JSON.parse(localStorage.getItem('send')||'{}');if(v.ch)$('sch').value=v.ch;if(EOL[v.eol]!=null)$('seol').value=v.eol}catch(e){}
function ssave(){try{localStorage.setItem('send',JSON.stringify({ch:$('sch').value,eol:$('seol').value}))}catch(e){}}
$('sch').onchange=ssave;$('seol').onchange=ssave;
function sled(c,ms){const e=$('spr');e.className='pr '+c;clearTimeout(e._t);e._t=setTimeout(()=>e.className='pr',ms)}
function sres(t,err){if(err)sled('err',800);const r=$('sres');r.textContent=t;r.className=err?'err':'';clearTimeout(r._t);r._t=setTimeout(()=>r.textContent='',3000)}
async function send(){const v=$('stx').value,ch=$('sch').value,hex=v.startsWith('hex:');
 const eol=EOL[$('seol').value]??'\r\n';if(!v&&!eol)return;
 if(v&&shist[shist.length-1]!==v)shist.push(v);sh=shist.length;$('stx').value='';
 sled('tx',150);
 try{const r=await fetch('/send?ch='+ch+'&'+(hex?'hex='+v.slice(4).replace(/\s/g,''):'text='+encodeURIComponent(v+eol)));
  const t=(await r.text()).trim();r.ok?sres('✓ '+t.replace(' sent','')+' → TX '+'AB'[ch]):sres('✗ '+t,1)}catch(e){sres('✗ ESP32 not reachable',1)}
 $('stx').focus()}
$('sbt').onclick=send;
$('apmode').onchange=async e=>{
 try{const r=await fetch('/ap?mode='+e.target.value),t=(await r.text()).trim();$('apres').textContent=(r.ok?'✓ ':'✗ ')+t}
 catch(x){$('apres').textContent='✗ no answer'}setTimeout(()=>$('apres').textContent='',4000)};
$('stx').onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();send()}
 else if(e.key==='ArrowUp'&&sh>0){e.preventDefault();$('stx').value=shist[--sh]}
 else if(e.key==='ArrowDown'){e.preventDefault();sh=Math.min(sh+1,shist.length);$('stx').value=shist[sh]||''}};
(async()=>{try{const st=await boot(),v=mark('clr_mon');if(valid(v,st))next=v.s>st.h-300?v.s:-300}catch(e){}if(!SET)poll();status()})();
</script></body></html>)HTML";

/* web terminal: xterm.js (embedded, src/web_assets.h, no internet needed) over
 * ws://<ip>/ws?ch=N */
static const char TERM_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lu-Fi UART Terminal</title>
<link rel="stylesheet" href="/xterm.css">
<link rel="icon" type="image/png" href="/favicon.png">
<style>
:root{--bg:#f3f4f6;--panel:#fff;--fg:#1b1f24;--mut:#68717d;--bd:#d8dce2;--bd2:#e8ebef;--acc:#2f6fde;--acc-bg:#e8f0fd;--ok:#1f9d55;--err:#d93848;
--con:#0e1116;--con-fg:#d5dae1;--con-bd:#262c36;--con-hd:#161b22;--sh:0 1px 2px rgba(16,24,40,.06),0 1px 3px rgba(16,24,40,.08)}
@media (prefers-color-scheme:dark){:root{--bg:#0b0d10;--panel:#14181e;--fg:#e6e9ed;--mut:#8b94a1;--bd:#2a313b;--bd2:#20262e;--acc:#5b93f0;--acc-bg:#1a2638;--sh:0 1px 2px rgba(0,0,0,.4)}}
*{box-sizing:border-box}[hidden]{display:none!important}
html,body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
a{color:var(--acc);text-decoration:none}
button,input,select{font:inherit;color:inherit}
.bar{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;align-items:center;gap:10px 18px;padding:10px 20px;background:var(--panel);border-bottom:1px solid var(--bd);box-shadow:var(--sh)}
.brand{display:flex;align-items:center;gap:10px;font-weight:600;font-size:15px;letter-spacing:.2px}
.logo{width:28px;height:28px;border-radius:7px;background:linear-gradient(135deg,var(--acc),#7a5cf0);color:#fff;display:grid;place-items:center;font:700 12px ui-monospace,monospace;letter-spacing:-.5px}
.brand small{display:block;font-weight:400;font-size:11.5px;color:var(--mut)}
.tabs{display:flex;gap:2px;padding:3px;border:1px solid var(--bd);border-radius:9px;background:var(--bg)}
.tabs a{padding:4px 14px;border-radius:6px;color:var(--mut);font-weight:500}
.tabs a.on{background:var(--panel);color:var(--fg);box-shadow:var(--sh)}
.pills{display:flex;flex-wrap:wrap;gap:6px;margin-left:auto}
.pill{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border:1px solid var(--bd);border-radius:999px;background:var(--bg);color:var(--mut);font-size:12.5px;white-space:nowrap}
.dot{width:8px;height:8px;border-radius:50%;background:var(--mut)}.dot.ok{background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 25%,transparent)}.dot.err{background:var(--err)}
main{max-width:1600px;margin:0 auto;padding:16px 20px 24px;display:flex;flex-direction:column;gap:16px}
.card{background:var(--panel);border:1px solid var(--bd);border-radius:12px;box-shadow:var(--sh);overflow:hidden}
.card>h2,.card>summary{margin:0;padding:11px 16px;font-size:13px;font-weight:600;text-transform:uppercase;letter-spacing:.6px;color:var(--mut);border-bottom:1px solid var(--bd2)}
details.card>summary{cursor:pointer;list-style:none;border-bottom:0}details.card[open]>summary{border-bottom:1px solid var(--bd2)}
details.card>summary::before{content:"\25B8";display:inline-block;margin-right:8px;transition:transform .15s}details.card[open]>summary::before{transform:rotate(90deg)}
.tools{display:flex;flex-wrap:wrap;align-items:center;gap:8px;padding:10px 12px;border-bottom:1px solid var(--bd2)}
.sep{width:1px;align-self:stretch;background:var(--bd2);margin:0 4px}
.chip{display:inline-flex;align-items:center;gap:7px;padding:4px 11px;border:1px solid var(--bd);border-radius:999px;cursor:pointer;user-select:none;font-size:13px;color:var(--mut);background:var(--panel)}
.chip input{position:absolute;opacity:0;pointer-events:none}
.chip:has(input:checked){color:var(--fg);border-color:var(--acc);background:var(--acc-bg)}
.chip:has(input:focus-visible){outline:2px solid var(--acc);outline-offset:2px}
.chip i{width:9px;height:9px;border-radius:50%;background:var(--bd)}
.chip:has(input:checked) i{background:var(--k,var(--acc))}
.btn{padding:5px 13px;border:1px solid var(--bd);border-radius:8px;background:var(--panel);cursor:pointer;font-size:13px;font-weight:500}
.btn:hover{border-color:var(--acc);color:var(--acc)}.btn.pri{background:var(--acc);border-color:var(--acc);color:#fff}.btn.pri:hover{filter:brightness(1.08);color:#fff}
.right{margin-left:auto;display:flex;gap:8px}
.con{--fg:var(--con-fg);--bg:var(--con);margin:12px;border:1px solid var(--con-bd);border-radius:10px;background:var(--con);overflow:hidden;box-shadow:inset 0 1px 0 rgba(255,255,255,.03)}
.con-hd{display:flex;align-items:center;gap:8px;padding:7px 12px;background:var(--con-hd);border-bottom:1px solid var(--con-bd);color:#8b949e;font:12px ui-monospace,monospace}
.con-hd b{width:10px;height:10px;border-radius:50%;background:#3a414c;transition:background .3s,box-shadow .3s}.con-hd b.on{background:var(--c);box-shadow:0 0 6px var(--c);transition:none}.con-hd b.st{background:var(--c)}.con-hd span{margin-left:6px}
input[type=text],input[type=number],input[type=password],input:not([type]),select{padding:5px 9px;border:1px solid var(--bd);border-radius:8px;background:var(--panel);min-width:0}
input:focus,select:focus{outline:2px solid color-mix(in srgb,var(--acc) 40%,transparent);border-color:var(--acc)}
@media (max-width:640px){.bar{padding:10px 16px}main{padding:12px 16px}.pills{margin-left:0}.right{margin-left:0}.con{margin:8px}}

html,body{height:100%}body{display:flex;flex-direction:column}
main{flex:1;min-height:0;width:100%}
.card.fill{flex:1;min-height:0;display:flex;flex-direction:column}
.con.fill{flex:1;min-height:0;display:flex;flex-direction:column}
#t{flex:1;min-height:0;padding:8px 4px 4px 10px}
.hint{color:var(--mut);font-size:12.5px}
</style></head><body>
<div class="bar">
 <div class="brand"><img class="logo" src="/logo.webp" alt="UART Monitor logo"><div>Lu-Fi UART Terminal<small>3-channel serial monitor · ESP32</small></div></div>
 <nav class="tabs"><a href="/" title="Live view of all channels">Monitor</a><a class="on" href="/term" title="Interactive console for one channel">Terminal</a><a href="/settings" title="Channels, baud rates, WiFi, access point">Settings</a><a href="/help" title="Quick guide">Help</a></nav>
 <div class="pills"><span class="pill" title="Connection of this terminal to the ESP32"><span class="dot" id="sd"></span><span id="st">…</span></span><span class="pill" id="sz">–</span></div>
</div>
<main>
<section class="card fill">
 <div class="tools">
  <select id="ch" title="Channel shown in this terminal (remembered for next time)"><option value="0">A</option><option value="1">B</option><option value="2">C</option></select>
  <span class="hint" id="route" title="Where your keystrokes go and whether TX is active right now">…</span>
  <div class="right"><button class="btn" id="clr" title="Empty the terminal. Stays empty after reload (F5) until the ESP32 restarts">Clear</button></div>
 </div>
 <div class="con fill"><div class="con-hd"><b id="lr" title="receive" style="--c:#4cc27f"></b><b id="lt" title="transmit" style="--c:#e0a050"></b><b id="lc" title="connection" style="--c:#e5534b"></b><span id="ttl">console</span></div><div id="t"></div></div>
</section>
</main>
<script src="/xterm.js"></script>
<script src="/xterm-addon-fit.js"></script>
<script>
const st=document.getElementById('st'),sd=document.getElementById('sd'),sz=document.getElementById('sz'),sel=document.getElementById('ch');
const q=new URLSearchParams(location.search);let last=null;try{last=localStorage.getItem('tch')}catch(e){}
sel.value=q.get('ch')||last||'0';
if(typeof Terminal==='undefined'){st.textContent='xterm.js could not be loaded';throw 0}
const term=new Terminal({cursorBlink:true,scrollback:10000,convertEol:false,fontFamily:'ui-monospace,"SF Mono",Menlo,Consolas,monospace',fontSize:14,
 theme:{background:'#0e1116',foreground:'#d5dae1',cursor:'#5b93f0',selectionBackground:'#2f6fde66'}});
const fit=new FitAddon.FitAddon();term.loadAddon(fit);term.open(document.getElementById('t'));
function refit(){fit.fit();sz.textContent=term.cols+'×'+term.rows;sz.title='set on the far side: stty rows '+term.rows+' cols '+term.cols}
refit();addEventListener('resize',refit);
const RXP=[16,18,21],TXP=[17,19];
function chLabel(i,c){return 'ABC'[i]+' · '+(c&&c.name?c.name:'RX GPIO'+RXP[i])}
function keysTo(t){return t<0?'nowhere (read only)':'TX '+'AB'[t]+' (GPIO'+TXP[t]+')'}
function blink(e){e.classList.add('on');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('on'),90)}
function mark(k){try{const v=JSON.parse(localStorage.getItem(k));return v}catch(e){return null}}
async function boot(){const s=await(await fetch('/status')).json();return{h:s.head,b:Date.now()/1000-s.uptime}}
function valid(v,st){return v&&Math.abs(v.b-st.b)<120&&v.s<=st.h}
const enc=new TextEncoder(),lr=document.getElementById('lr'),lt=document.getElementById('lt'),lc=document.getElementById('lc');let ws,retry;
let ctx=null;
async function tstat(){try{const s=await(await fetch('/status')).json();
 s.ch.forEach((x,i)=>sel.options[i].textContent=chLabel(i,x));
 const ch=+sel.value,t=s.ch[ch].ctx;ctx=t;document.getElementById('ttl').textContent=chLabel(ch,s.ch[ch]);
 const r=document.getElementById('route'),act=t>=0&&(s.ch[t].tx_console>0||s.ch[t].tx);
 r.innerHTML=t<0?'<b style="color:#e5534b">read only</b>: keystrokes are not sent · change under Settings → keys to':
  'keys → <b>'+keysTo(t)+'</b>'+(t!=ch?' <b style="color:#e0a050">(other channel than shown!)</b>':'')+
  (act?' · <b style="color:#e0a050">TX active</b>':' · TX idle, switches on at the first keystroke')+' · Settings → keys to';
 lt.style.setProperty('--c',t<0?'#e5534b':'#e0a050');lt.title=t<0?'transmit: blocked (read only)':'transmit → '+keysTo(t);
}catch(e){}setTimeout(tstat,2000)}
function link(ok){lc.classList.add('st');lc.style.setProperty('--c',ok?'#4cc27f':'#e5534b');lc.title=ok?'connected':'disconnected'}
async function connect(){
 clearTimeout(retry);if(ws){ws.onclose=null;ws.close()}
 let from='';try{const st=await boot(),v=mark('clr_t'+sel.value);if(valid(v,st))from='&from='+v.s}catch(e){}
 ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws?ch='+sel.value+from);ws.binaryType='arraybuffer';
 sd.className='dot';st.textContent='connecting…';
 ws.onopen=()=>{link(1);sd.className='dot ok';st.textContent='connected';term.focus()};
 ws.onmessage=e=>{blink(lr);term.write(new Uint8Array(e.data))};
 ws.onclose=()=>{link(0);sd.className='dot err';st.textContent='disconnected, retrying…';retry=setTimeout(connect,2000)};
}
term.onData(d=>{if(ws&&ws.readyState===1){blink(lt);ws.send(enc.encode(d))}});
term.onBinary(d=>{if(ws&&ws.readyState===1){blink(lt);ws.send(Uint8Array.from(d,c=>c.charCodeAt(0)))}});
sel.onchange=()=>{try{localStorage.setItem('tch',sel.value)}catch(e){}history.replaceState(null,'','?ch='+sel.value);term.reset();connect();tstat()};
document.getElementById('clr').onclick=async()=>{term.clear();term.write('\x1b[2J\x1b[H');try{const st=await boot();localStorage.setItem('clr_t'+sel.value,JSON.stringify({s:st.h,b:st.b}))}catch(e){}};
try{localStorage.setItem('tch',sel.value)}catch(e){}
connect();tstat();
</script></body></html>)HTML";


/* help page */
static const char HELP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lu-Fi UART Help</title>
<link rel="icon" type="image/png" href="/favicon.png">
<style>
:root{--bg:#f3f4f6;--panel:#fff;--fg:#1b1f24;--mut:#68717d;--bd:#d8dce2;--bd2:#e8ebef;--acc:#2f6fde;--acc-bg:#e8f0fd;--ok:#1f9d55;--err:#d93848;
--con:#0e1116;--con-fg:#d5dae1;--con-bd:#262c36;--con-hd:#161b22;--sh:0 1px 2px rgba(16,24,40,.06),0 1px 3px rgba(16,24,40,.08)}
@media (prefers-color-scheme:dark){:root{--bg:#0b0d10;--panel:#14181e;--fg:#e6e9ed;--mut:#8b94a1;--bd:#2a313b;--bd2:#20262e;--acc:#5b93f0;--acc-bg:#1a2638;--sh:0 1px 2px rgba(0,0,0,.4)}}
*{box-sizing:border-box}[hidden]{display:none!important}
html,body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
a{color:var(--acc);text-decoration:none}
button,input,select{font:inherit;color:inherit}
.bar{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;align-items:center;gap:10px 18px;padding:10px 20px;background:var(--panel);border-bottom:1px solid var(--bd);box-shadow:var(--sh)}
.brand{display:flex;align-items:center;gap:10px;font-weight:600;font-size:15px;letter-spacing:.2px}
.logo{width:48px;height:48px;display:block;margin:-6px 0}
.brand small{display:block;font-weight:400;font-size:11.5px;color:var(--mut)}
.tabs{display:flex;gap:2px;padding:3px;border:1px solid var(--bd);border-radius:9px;background:var(--bg)}
.tabs a{padding:4px 14px;border-radius:6px;color:var(--mut);font-weight:500}
.tabs a.on{background:var(--panel);color:var(--fg);box-shadow:var(--sh)}
.pills{display:flex;flex-wrap:wrap;gap:6px;margin-left:auto}
.pill{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border:1px solid var(--bd);border-radius:999px;background:var(--bg);color:var(--mut);font-size:12.5px;white-space:nowrap}
.dot{width:8px;height:8px;border-radius:50%;background:var(--mut)}.dot.ok{background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 25%,transparent)}.dot.err{background:var(--err)}
main{max-width:1600px;margin:0 auto;padding:16px 20px 24px;display:flex;flex-direction:column;gap:16px}
.card{background:var(--panel);border:1px solid var(--bd);border-radius:12px;box-shadow:var(--sh);overflow:hidden}
.card>h2,.card>summary{margin:0;padding:11px 16px;font-size:13px;font-weight:600;text-transform:uppercase;letter-spacing:.6px;color:var(--mut);border-bottom:1px solid var(--bd2)}
details.card>summary{cursor:pointer;list-style:none;border-bottom:0}details.card[open]>summary{border-bottom:1px solid var(--bd2)}
details.card>summary::before{content:"\25B8";display:inline-block;margin-right:8px;transition:transform .15s}details.card[open]>summary::before{transform:rotate(90deg)}
.tools{display:flex;flex-wrap:wrap;align-items:center;gap:8px;padding:10px 12px;border-bottom:1px solid var(--bd2)}
.sep{width:1px;align-self:stretch;background:var(--bd2);margin:0 4px}
.chip{display:inline-flex;align-items:center;gap:7px;padding:4px 11px;border:1px solid var(--bd);border-radius:999px;cursor:pointer;user-select:none;font-size:13px;color:var(--mut);background:var(--panel)}
.chip input{position:absolute;opacity:0;pointer-events:none}
.chip:has(input:checked){color:var(--fg);border-color:var(--acc);background:var(--acc-bg)}
.chip:has(input:focus-visible){outline:2px solid var(--acc);outline-offset:2px}
.chip i{width:9px;height:9px;border-radius:50%;background:var(--bd)}
.chip:has(input:checked) i{background:var(--k,var(--acc))}
.btn{padding:5px 13px;border:1px solid var(--bd);border-radius:8px;background:var(--panel);cursor:pointer;font-size:13px;font-weight:500}
.btn:hover{border-color:var(--acc);color:var(--acc)}.btn.pri{background:var(--acc);border-color:var(--acc);color:#fff}.btn.pri:hover{filter:brightness(1.08);color:#fff}
.right{margin-left:auto;display:flex;gap:8px}
.con{--fg:var(--con-fg);--bg:var(--con);margin:12px;border:1px solid var(--con-bd);border-radius:10px;background:var(--con);overflow:hidden;box-shadow:inset 0 1px 0 rgba(255,255,255,.03)}
.con-hd{display:flex;align-items:center;gap:8px;padding:7px 12px;background:var(--con-hd);border-bottom:1px solid var(--con-bd);color:#8b949e;font:12px ui-monospace,monospace}
.con-hd b{width:10px;height:10px;border-radius:50%;background:#3a414c;transition:background .3s,box-shadow .3s}.con-hd b.on{background:var(--c);box-shadow:0 0 6px var(--c);transition:none}.con-hd b.st{background:var(--c)}.con-hd span{margin-left:6px}
input[type=text],input[type=number],input[type=password],input:not([type]),select{padding:5px 9px;border:1px solid var(--bd);border-radius:8px;background:var(--panel);min-width:0}
input:focus,select:focus{outline:2px solid color-mix(in srgb,var(--acc) 40%,transparent);border-color:var(--acc)}
@media (max-width:640px){.bar{padding:10px 16px}main{padding:12px 16px}.pills{margin-left:0}.right{margin-left:0}.con{margin:8px}}


.doc{padding:6px 22px 18px;max-width:900px}
.doc h3{margin:20px 0 6px;font-size:15px}
.doc ul{padding-left:20px;margin:6px 0}.doc li{margin:3px 0}
code,kbd{font:12.5px ui-monospace,monospace;background:var(--bg);border:1px solid var(--bd2);border-radius:5px;padding:1px 5px}
kbd{border-bottom-width:2px}
table{border-collapse:collapse;margin:8px 0;width:100%;font-size:13.5px}
td,th{border:1px solid var(--bd2);padding:6px 10px;text-align:left;vertical-align:top}th{background:var(--bg);font-weight:600}
.k{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px}
.warn{border-left:3px solid #e0a050;background:color-mix(in srgb,#e0a050 10%,transparent);padding:8px 12px;border-radius:6px;margin:10px 0}
.toc{display:flex;flex-wrap:wrap;gap:6px;padding:12px 16px;border-bottom:1px solid var(--bd2)}
.toc a{padding:3px 10px;border:1px solid var(--bd);border-radius:999px;font-size:12.5px;color:var(--mut)}
.toc a:hover{color:var(--acc);border-color:var(--acc)}
</style></head><body>
<div class="bar">
 <div class="brand"><img class="logo" src="/logo.webp" alt="UART Monitor logo"><div>Lu-Fi UART Help<small>3-channel serial monitor · ESP32</small></div></div>
 <nav class="tabs"><a href="/" title="Live view of all channels">Monitor</a><a href="/term" title="Interactive console for one channel">Terminal</a><a href="/settings" title="Channels, baud rates, WiFi, access point">Settings</a><a class="on" href="/help" title="Quick guide">Help</a></nav>
</div>
<main>
<section class="card">
 <h2>Quick guide</h2>
 <nav class="toc"><a href="#wiring">Wiring</a><a href="#monitor">Monitor</a><a href="#terminal">Terminal</a><a href="#tx">Sending &amp; safety</a><a href="#settings">Settings</a><a href="#network">Network access</a><a href="#update">Update</a></nav>
 <div class="doc">

<h3 id="wiring">Wiring</h3>
<p>Three UART channels, all 3.3 V, default 115200 baud 8N1. <b>Always connect the device's GND to the ESP32's GND.</b></p>
<table><tr><th>Channel</th><th>Receive (RX)</th><th>Transmit (TX)</th></tr>
<tr><td><span class="k" style="background:#4cc27f"></span>A</td><td>GPIO16</td><td>GPIO17 (only while TX is on)</td></tr>
<tr><td><span class="k" style="background:#6ea4f5"></span>B</td><td>GPIO18</td><td>GPIO19 (only while TX is on)</td></tr>
<tr><td><span class="k" style="background:#e0a050"></span>C</td><td>GPIO21</td><td>– (receive only)</td></tr></table>
<p>ESP32 RX goes to the device's <i>TX</i> pin, ESP32 TX to the device's <i>RX</i> pin. Never connect 5 V signals.</p>

<h3 id="monitor">Monitor</h3>
<ul>
<li><b>Channel chips A/B/C</b> show or hide a channel. Every line starts with a timestamp (seconds since the ESP32 started) and the channel.</li>
<li><b>ANSI</b> (on by default) renders colours, bold, progress bars (<code>\r</code>), backspace and UTF-8. Off = every control character visible as <code>&lt;1b&gt;</code>, useful for protocol debugging.</li>
<li><b>hide heartbeats</b> hides the constant T30⇄XR871 heartbeat frames; the count is shown at the top right.</li>
<li><b>pause</b> freezes the view while reception continues. <b>Clear</b> empties the view (also after F5, until the ESP32 restarts), <b>Save log</b> downloads it as a text file.</li>
<li>The three dots in the console frame light up on traffic: A / B / C receive (in the channel colours). In the terminal: receive, transmit, connection.</li>
<li>Binary frames (<code>aa 55 aa 55 …</code>) are shown decoded; WiFi credentials inside them are shown as lengths only.</li>
</ul>

<h3 id="terminal">Terminal</h3>
<ul>
<li>A full terminal in the browser: colours, cursor keys, <kbd>Ctrl</kbd>+<kbd>C</kbd>, <code>top</code>, <code>vi</code>. Pick the channel at the top.</li>
<li>Recent history is shown first when it opens. Scrollback: 10,000 lines (mouse wheel).</li>
<li>A serial line has no window size: set the size shown at the top (e.g. <code>118×38</code>) on the far side with <code>stty rows 38 cols 118</code>.</li>
<li>The line next to the channel picker shows where your keystrokes go, e.g. <i>keys → TX A (GPIO17)</i>, or <i>read only</i>. If you type and nothing happens, check this first. Change it under Settings → <b>keys to</b>.</li>
<li>Channel C has no TX pin of its own; by default its keystrokes go out on TX B (GPIO19).</li>
<li>The terminal opens the channel you used last.</li>
</ul>

<h3 id="tx">Sending &amp; safety</h3>
<div class="warn">A TX pin driving into an <b>unpowered</b> device can back-power it through the pin. That is why all TX pins are high impedance by default.</div>
<ul>
<li>Terminal and telnet switch TX on at the <b>first keystroke</b> and off again when the last terminal closes. Just watching never transmits.</li>
<li>Switch TX on permanently only under <b>Settings → TX on</b>, and only while the device is powered.</li>
<li>Send single lines with the bar under the monitor: type, <kbd>Enter</kbd>. <kbd>↑</kbd>/<kbd>↓</kbd> recall earlier lines, the line ending (CR LF, CR, LF, none) is selectable, <code>hex:aa55…</code> sends raw bytes. TX is driven only for that one send.</li>
</ul>

<h3 id="settings">Settings</h3>
<ul>
<li><b>Name</b> per channel (e.g. the connected device), shown in the chips, the terminal and the frame title.</li>
<li><b>Baud rate</b> per channel (1200 – 3000000), saved. Garbled characters usually mean a wrong baud rate, a missing GND or old noise from plugging in.</li>
<li><b>keys to</b>: which TX pin a channel's terminal sends on (TX A, TX B or nowhere).</li>
<li><b>Home WiFi</b> joins your network and shows which network is set up (never the password).</li>
<li><b>Access point</b>: the ESP32's own network <code>abus-uartmon-xxxx</code> (address <code>192.168.4.1</code>). By default it is on <i>only when the home WiFi is not connected</i>: it comes on 30 s after the home WiFi is lost (or when none is set up) and goes off again once the home WiFi is back and nobody has used it for 60 s. Settings → Access point → <i>always on</i> keeps it on.</li>
</ul>

<h3 id="network">Network access</h3>
<table><tr><th>Access</th><th>Purpose</th></tr>
<tr><td><code>http://&lt;ip&gt;/</code></td><td>this web interface</td></tr>
<tr><td><code>telnet &lt;ip&gt; 2321</code> / <code>2322</code> / <code>2323</code></td><td>interactive console A / B / C (quit: <kbd>Ctrl</kbd>+<kbd>]</kbd>, <code>quit</code>)</td></tr>
<tr><td><code>nc &lt;ip&gt; 2301</code> … <code>2303</code></td><td>raw byte stream of one channel</td></tr>
<tr><td><code>:2300</code></td><td>all channels with timestamps (tools/uartmon.py)</td></tr>
<tr><td><code>ws://&lt;ip&gt;/ws?ch=N</code></td><td>WebSocket used by the web terminal</td></tr></table>

<h3 id="update">Firmware update</h3>
<p>In the project folder <code>esp32mon</code>: <code>./update.sh</code> builds the firmware and installs it over WiFi. The first time, plug the ESP32 in via USB: the update password is read and saved, after that no cable is needed.</p>
 </div>
</section>
</main>
</body></html>)HTML";
