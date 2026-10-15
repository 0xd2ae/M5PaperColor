'use strict';
const $=id=>document.getElementById(id);
const $$=(sel,root=document)=>[...root.querySelectorAll(sel)];
const radio=name=>document.querySelector(`input[name="${name}"]:checked`)?.value;
function setRadio(name,value){const el=document.querySelector(`input[name="${name}"][value="${value}"]`);if(el)el.checked=true}
const pad=n=>String(n).padStart(2,'0');

/* ---------- API ---------- */
async function response(r){const text=await r.text();if(!r.ok)throw Error(text||'设备请求失败，请重新打开配置页');try{return JSON.parse(text)}catch(e){throw Error('设备返回内容异常，请重新打开配置页')}}
async function post(path,data,raw=false){const r=await fetch(path,{method:'POST',headers:{'Content-Type':raw?'application/octet-stream':'application/json'},body:raw?data:JSON.stringify(data)});return response(r)}
const getJSON=path=>fetch(path,{cache:'no-store'}).then(response);
async function ok(promise){const r=await promise;if(!r.ok)throw Error(r.message||'操作失败');return r}
let toastTimer=0;
function toast(text,good=true){const t=$('toast');t.textContent=text;t.className='toast'+(good?'':' err');t.hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>t.hidden=true,4500)}
function note(out,text,state){if(!out){toast(text,state!=='err');return}out.textContent=text;out.className='result '+state}
let busy=false;
// Device requests run one at a time; buttons marked .keep (tabs, preview navigation) stay usable.
async function action(fn,out){
  if(busy)return;busy=true;const buttons=$$('button:not(.keep)').filter(b=>!b.disabled);buttons.forEach(b=>b.disabled=true);
  try{const m=await fn(text=>note(out,text,'wait'));if(m)note(out,m,'ok');else if(out)out.textContent=''}
  catch(e){note(out,e.message||'请求失败，请重新连接热点',  'err')}
  finally{busy=false;buttons.forEach(b=>b.disabled=false);updatePush()}
}
function api(){return {host:$('host').value.trim(),api_key:$('key').value.trim()}}

/* ---------- Six-colour pipeline (e6.wasm) ---------- */
// Panel order matches the firmware: black, white, red, yellow, blue, green.
const IDEAL=[[22,27,22],[255,255,247],[187,26,28],[249,222,55],[43,82,172],[53,128,58]];
const KEYS=['exposure','brightness','contrast','saturation','vibrance','temperature','tint','gamma','clarity','sharpen','smoothness','mode','chroma','refine'];
const SLIDERS=[['exposure','曝光 EV',-3,3,.05],['brightness','亮度',-.3,.3,.01],['contrast','对比度',.5,2,.01],['saturation','饱和度',0,2,.01],['vibrance','自然饱和度',-1,1,.01],['temperature','色温',-1,1,.01],['tint','色调',-1,1,.01],['gamma','伽马',.5,2,.01],['clarity','清晰度',0,1,.01],['sharpen','锐化',0,2,.01],['smoothness','平滑度',0,100,1],['chroma','色度保留',.3,.7,.01]];
// Higher saturation than a phone edit: reflective six-colour ink shows only a small gamut.
const PRESETS={
  balanced:{exposure:0,brightness:0,contrast:1.05,saturation:1.25,vibrance:.25,temperature:0,tint:0,gamma:1,clarity:.25,sharpen:.4,smoothness:25,chroma:.44},
  landscape:{exposure:0,brightness:0,contrast:1.08,saturation:1.35,vibrance:.35,temperature:0,tint:0,gamma:1,clarity:.4,sharpen:.5,smoothness:22,chroma:.48},
  portrait:{exposure:.1,brightness:0,contrast:1.02,saturation:1.1,vibrance:.15,temperature:.05,tint:0,gamma:1.05,clarity:.1,sharpen:.2,smoothness:35,chroma:.42},
  anime:{exposure:0,brightness:0,contrast:1.1,saturation:1.3,vibrance:.2,temperature:0,tint:0,gamma:1,clarity:.2,sharpen:.6,smoothness:10,chroma:.5},
  document:{exposure:0,brightness:.03,contrast:1.3,saturation:1.1,vibrance:0,temperature:0,tint:0,gamma:1,clarity:.3,sharpen:.9,smoothness:15,chroma:.44}};
let wasm=null,measured=null;
async function e6(){
  if(wasm)return wasm;
  const bytes=async()=>WebAssembly.compile(await (await fetch('/e6.wasm')).arrayBuffer());
  const module=await (WebAssembly.compileStreaming?WebAssembly.compileStreaming(fetch('/e6.wasm')).catch(bytes):bytes());
  const imports={};for(const i of WebAssembly.Module.imports(module))(imports[i.module]??={})[i.name]=()=>0;
  const instance=await WebAssembly.instantiate(module,imports);wasm=instance.exports;wasm._initialize?.();wasm.e6_init();
  const p=wasm.malloc(18);wasm.e6_palette_rgb(p);const v=new Uint8Array(wasm.memory.buffer,p,18);measured=[0,1,2,3,4,5].map(i=>[v[i*3],v[i*3+1],v[i*3+2]]);wasm.free(p);
  return wasm;
}
// rgba: ImageData at 1.5x of (ow, oh). Returns palette indices.
async function run(image,ow,oh,params,refine=2){
  const e=await e6(),n=image.width*image.height*4,src=e.malloc(n),out=e.malloc(ow*oh),pv=e.malloc(KEYS.length*4);
  try{
    new Uint8Array(e.memory.buffer,src,n).set(image.data);
    const values={...params,mode:+$('ditherMode').value,refine};
    new Float32Array(e.memory.buffer,pv,KEYS.length).set(KEYS.map(k=>values[k]??0));
    if(e.e6_process(src,image.width,image.height,pv,out,ow,oh))throw Error('图像处理失败');
    return new Uint8Array(e.memory.buffer,out,ow*oh).slice();
  }finally{e.free(src);e.free(out);e.free(pv)}
}
async function adjusted(image,params){
  const e=await e6(),n=image.width*image.height*4,src=e.malloc(n),pv=e.malloc(KEYS.length*4);
  try{new Uint8Array(e.memory.buffer,src,n).set(image.data);new Float32Array(e.memory.buffer,pv,KEYS.length).set(KEYS.map(k=>params[k]??0));e.e6_adjust(src,image.width,image.height,pv);
    return new ImageData(new Uint8ClampedArray(new Uint8Array(e.memory.buffer,src,n)),image.width,image.height)}
  finally{e.free(src);e.free(pv)}
}
async function score(image,indices,ow,oh){
  const e=await e6(),n=image.width*image.height*4,src=e.malloc(n),idx=e.malloc(ow*oh);
  try{new Uint8Array(e.memory.buffer,src,n).set(image.data);new Uint8Array(e.memory.buffer,idx,ow*oh).set(indices);return e.e6_score(src,image.width,image.height,idx,ow,oh)}
  finally{e.free(src);e.free(idx)}
}
function pack(indices){const p=new Uint8Array(indices.length/2);for(let i=0;i<p.length;i++)p[i]=indices[2*i]<<4|indices[2*i+1];return p}
function unpack(p){const v=new Uint8Array(p.length*2);for(let i=0;i<p.length;i++){v[2*i]=p[i]>>4;v[2*i+1]=p[i]&15}return v}
function paint(ctx,indices,palette){const im=ctx.createImageData(400,600);for(let i=0;i<indices.length;i++){const c=palette[indices[i]]||[255,0,255];im.data.set(c,i*4);im.data[i*4+3]=255}ctx.putImageData(im,0,0)}
const palette=()=>radio('palette')==='ideal'||!measured?IDEAL:measured;

/* ---------- Preview: frames drawn by the device's own renderer ---------- */
const screenCtx=$('screen').getContext('2d');
const pv={mode:'calendar',year:0,month:0,id:'',frame:null,gen:0,title:'',status:{},error:''};
let device={},items=[];
function deviceDate(){
  if(!device.time_valid||!device.now)return null;
  const d=new Date((device.now+8*3600)*1000);return {year:d.getUTCFullYear(),month:d.getUTCMonth()+1,day:d.getUTCDate(),hour:d.getUTCHours(),minute:d.getUTCMinutes()};
}
function paintFrame(){
  if(!pv.frame)return;const idx=unpack(pv.frame);paint(screenCtx,idx,palette());
  if(!$('zoomLayer').hidden)paint($('zoomCanvas').getContext('2d'),idx,palette());
}
async function fetchFrame(url,init={}){
  const r=await fetch(url,{cache:'no-store',...init});
  if(!r.ok)throw Error((await r.text())||'预览生成失败');
  const data=new Uint8Array(await r.arrayBuffer());if(data.length!==120000)throw Error('预览数据长度异常');
  const status=Object.fromEntries((r.headers.get('X-Status')||'').split(',').filter(Boolean).map(s=>s.split('=')));
  return {data,status};
}
function subtitle(){
  if(pv.error)return pv.error;
  const s=pv.status,parts=[];
  if(s.time==='0')parts.push('设备时间未同步');
  if(pv.mode==='calendar'&&s.weather==='stale')parts.push('天气已过期');
  if(pv.mode==='calendar'&&s.weather==='none')parts.push('暂无天气');
  if(s.card==='0')parts.push('未插 TF 卡');
  return parts.length?parts.join(' · '):'设备绘制，与墨水屏逐像素一致';
}
function showTitle(){$('pvTitle').textContent=pv.title;$('pvSub').textContent=subtitle()}
async function show(url,init,title){
  const g=++pv.gen;pv.title=title;pv.error='';showTitle();$('pvBusy').hidden=false;
  try{const f=await fetchFrame(url,init);if(g!==pv.gen)return;pv.frame=f.data;pv.status=f.status;paintFrame()}
  catch(e){if(g===pv.gen)pv.error=e.message||'预览生成失败'}
  finally{if(g===pv.gen){$('pvBusy').hidden=true;showTitle()}}
}
function previewCalendar(year,month){
  pv.mode='calendar';
  if(year!==undefined){pv.year=year;pv.month=month}
  const d=deviceDate(),y=pv.year||d?.year,m=pv.year?pv.month:d?.month;
  const title=y?`日历 · ${y} 年 ${m} 月`:'日历 · 设备时间未同步';
  show('/api/render?page=calendar'+(pv.year?`&month=${pv.year}-${pad(pv.month)}`:''),{},title);updateControls();
}
function previewAnniversary(id){
  pv.mode='anniversary';pv.id=id||items[0]?.id||'';
  const a=items.find(x=>x.id===pv.id);
  show('/api/render?page=anniversary'+(pv.id?`&id=${pv.id}`:''),{},a?`纪念日 · ${a.name}`:'纪念日页');updateControls();
}
function previewDraft(body,name){pv.mode='draft';show('/api/render',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body},`纪念日 · ${name}（草稿）`);updateControls()}
function shiftMonth(delta){
  const d=deviceDate();let y=pv.year||d?.year||2026,m=(pv.year?pv.month:d?.month||1)+delta;
  while(m<1){m+=12;y--}while(m>12){m-=12;y++}
  if(y<2000||y>2099)return;
  if(d&&y===d.year&&m===d.month)previewCalendar(0,0);else previewCalendar(y,m);
}
function updateControls(){
  const cal=pv.mode==='calendar',ann=pv.mode==='anniversary';
  $('pvNav').hidden=!(cal||(ann&&items.length>1));
  $('pvToday').hidden=!cal;
  $('pvPrev').setAttribute('aria-label',cal?'上月':'上一个纪念日');$('pvNext').setAttribute('aria-label',cal?'下月':'下一个纪念日');
  $('pvNote').hidden=!cal;$('pvNote').textContent='退出配置后墨水屏显示日历';
  updatePush();
  $$('#chips .chip').forEach(c=>c.setAttribute('aria-pressed',String(cal?c.dataset.id==='':ann&&(c.dataset.id===pv.id||c.dataset.id==='-'))));
  measureDock();
}
function updatePush(){
  const b=$('pvPush');
  if(pv.mode==='calendar'){b.hidden=true;return}
  b.hidden=false;
  if(pv.mode==='draft'){const saved=editor.item&&!editor.dirty;b.disabled=busy||!saved;b.textContent=saved?'在墨水屏上显示':'保存后可在墨水屏显示'}
  else{b.disabled=busy||!items.length;b.textContent='在墨水屏上显示'}
}
$('pvPrev').onclick=()=>{if(pv.mode==='calendar')return shiftMonth(-1);const i=items.findIndex(a=>a.id===pv.id);previewAnniversary(items[(i-1+items.length)%items.length].id)};
$('pvNext').onclick=()=>{if(pv.mode==='calendar')return shiftMonth(1);const i=items.findIndex(a=>a.id===pv.id);previewAnniversary(items[(i+1)%items.length].id)};
$('pvToday').onclick=()=>previewCalendar(0,0);
$('pvPush').onclick=()=>action(async()=>{
  const id=pv.mode==='draft'?editor.item?.id:pv.id;if(!id)throw Error('请先保存纪念日');
  const r=await ok(post('/api/preview',{id}));return r.message;
});
$$('input[name=palette]').forEach(r=>r.onchange=()=>{paintFrame();paintThumbs();renderStage()});
// Pixel zoom
function openZoom(){if(!pv.frame)return;$('zoomLayer').hidden=false;applyZoom();paintFrame()}
function applyZoom(){const s=+radio('zoomScale');$('zoomCanvas').style.width=400*s+'px';$('zoomCanvas').style.height=600*s+'px'}
$('device').onclick=openZoom;$('zoomClose').onclick=()=>$('zoomLayer').hidden=true;
$$('input[name=zoomScale]').forEach(r=>r.onchange=applyZoom);
document.addEventListener('keydown',e=>{if(e.key==='Escape'){if(!$('zoomLayer').hidden)$('zoomLayer').hidden=true;else if(!$('editor').hidden)$('aCancel').click()}});
// The card keeps its slot in the dock and floats as a thumbnail bar once the page scrolls past it.
const card=$('previewCard'),wide=matchMedia('(min-width:980px)');
let scroller=window;
function scrollTop(){return scroller===window?window.scrollY:scroller.scrollTop}
function measureDock(){const dock=card.parentElement;if(wide.matches){dock.style.height='';return}if(!card.classList.contains('compact'))dock.style.height=card.offsetHeight+'px'}
function onScroll(){
  if(wide.matches){card.classList.remove('compact');return}
  const y=scrollTop(),limit=card.parentElement.offsetHeight*.6;
  if(!card.classList.contains('compact')&&y>limit)card.classList.add('compact');
  else if(card.classList.contains('compact')&&y<limit*.4)card.classList.remove('compact');
}
window.addEventListener('scroll',onScroll,{passive:true});$('editor').addEventListener('scroll',onScroll,{passive:true});
window.addEventListener('resize',()=>{onScroll();measureDock()});wide.addEventListener?.('change',()=>{onScroll();measureDock()});

/* ---------- Tabs ---------- */
function showTab(name){
  for(const t of $$('.tab')){const on=t.dataset.tab===name;t.setAttribute('aria-selected',String(on));$('tab-'+t.dataset.tab).hidden=!on}
  if(name==='anniv'&&pv.mode==='calendar'&&items.length)previewAnniversary(pv.id);
  if(location.hash!=='#'+name)history.replaceState(null,'','#'+name);
  window.scrollTo(0,0);onScroll();
}
$$('.tab').forEach(t=>t.onclick=()=>showTab(t.dataset.tab));

/* ---------- Screen tab: status ---------- */
const time2=t=>{const d=new Date((t+8*3600)*1000);return `${pad(d.getUTCMonth()+1)}-${pad(d.getUTCDate())} ${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}`};
function stat(label,value,detail,state='good'){
  const el=document.createElement('div');el.className='stat '+(state==='good'?'':state);
  el.innerHTML='<span><i></i></span><b></b><small></small>';el.querySelector('span').append(label);el.querySelector('b').textContent=value;el.querySelector('small').textContent=detail||'';return el;
}
function fonts(){const f=device.fonts||[];return f.length?f.map(s=>s+' px').join('、'):'仅内置字库'}
function renderStatus(){
  const d=deviceDate(),s=device;
  const weather=s.weather==='fresh'?['正常',`更新于 ${time2(s.weather_fetched)}`,'good']:s.weather==='stale'?['已过期',`上次 ${time2(s.weather_fetched)}`,'warn']:['暂无数据','连接 Wi-Fi 并保存城市后获取','bad'];
  const page=s.page==='anniversary'?'纪念日页':'日历页';
  $('stats').replaceChildren(
    stat('设备时间',d?`${d.year}-${pad(d.month)}-${pad(d.day)} ${pad(d.hour)}:${pad(d.minute)}`:'未同步',d?'北京时间':'连接 Wi-Fi 后自动校时',d?'good':'bad'),
    stat('Wi-Fi',s.ssid||'未设置',s.ssid?(s.offline?'上次联网失败':'已保存'):'在“设置”中填写',s.ssid?(s.offline?'warn':'good'):'bad'),
    stat('天气 · '+(s.city||'未设置'),weather[0],weather[1],weather[2]),
    stat('TF 卡',s.card?'已插入':'未检测到',s.card?fonts():'配置无法保存',s.card?'good':'bad'),
    stat('电量',`${s.battery??'—'}%`,s.battery<20?'请尽快充电':'',s.battery<20?'warn':'good'),
    stat('放假调休表',s.holiday_first?`${s.holiday_first}–${s.holiday_last} 年`:'无',d&&s.holiday_last<d.year?'今年的安排尚未导入':'',d&&s.holiday_last<d.year?'warn':'good'),
    stat('纪念日',`${s.anniversaries??0} 个`,s.card?'':'需要 TF 卡',s.card?'good':'off'),
    stat('退出后显示',page,s.celebrate_today?'今天是纪念日':'','good'));
  const dot=(id,good)=>$(id).style.background=good===true?'var(--green)':good===false?'var(--red)':'#d9b60f';
  dot('wifiDot',s.ssid?(s.offline?null:true):false);dot('weatherDot',s.weather==='fresh'?true:s.weather==='stale'?null:false);
  dot('holidayDot',!(d&&s.holiday_last<d.year)||null);dot('tfDot',!!s.card);
  $('holidayInfo').textContent=s.holiday_first?`当前年度表覆盖 ${s.holiday_first}–${s.holiday_last} 年。`:'没有年度表。';
  $('tfStats').replaceChildren(stat('状态',s.card?'已插入':'未检测到',s.card?'':'插卡后重启设备',s.card?'good':'bad'),
    stat('扩展字库',(s.fonts||[]).length?fonts():'未安装',(s.fonts||[]).length===3?'名称大字清晰显示':'缺字显示为 ?',(s.fonts||[]).length===3?'good':'warn'),
    stat('AI 模型',s.onnx?'已安装':'未安装',s.onnx?'编辑图片时可用“AI 自动”':'可选，仅用于图片预处理',s.onnx?'good':'off'));
  $('cardWarning').hidden=s.card!==false;$('annivNoCard').hidden=s.card!==false;$('annivBody').hidden=s.card===false;
}
let settingsFilled=false;
async function refreshStatus(){
  device=await getJSON('/api/status');renderStatus();
  if(settingsFilled)return;settingsFilled=true;
  $('ssid').value=device.ssid||'';$('host').value=device.host||'';
  if(device.has_password)$('passwordHint').textContent='Wi-Fi 名称不变时，留空沿用原密码；改了名称后留空表示开放网络。';
  if(device.has_key)$('keyHint').textContent='API Host 不变时，留空沿用已保存的凭据。';
  if(device.location){$('city').replaceChildren(new Option(device.city,device.location));$('city').value=device.location}
}
function renderChips(){
  const chips=[['','日历'],...items.map(a=>[a.id,a.name])].map(([id,name])=>{const b=document.createElement('button');b.className='chip keep';b.dataset.id=id;b.textContent=name;
    b.onclick=()=>id?previewAnniversary(id):previewCalendar(0,0);return b});
  if(device.card&&!items.length){const b=document.createElement('button');b.className='chip keep';b.dataset.id='-';b.textContent='纪念日页';b.onclick=()=>previewAnniversary('');chips.push(b)}
  $('chips').replaceChildren(...chips);updateControls();
}
function ended(text){$('ended').hidden=false;showTab('screen');toast(text)}
$('exit').onclick=()=>action(async()=>{const r=await ok(post('/api/exit',{}));ended(r.message);return r.message},$('exitResult'));

/* ---------- Settings ---------- */
$('connect').onclick=()=>action(async wait=>{wait('正在连接 Wi-Fi 并校时，最多约 30 秒…');const r=await ok(post('/api/connect',{ssid:$('ssid').value,password:$('password').value}));
  await refreshStatus().catch(()=>{});if(pv.mode==='calendar')previewCalendar();return r.message},$('wifiResult'));
$('search').onclick=()=>action(async wait=>{wait('正在搜索城市…');const r=await post('/api/cities',{...api(),query:$('query').value.trim()});if(!r.location)throw Error(r.message||'没有找到城市');
  $('city').replaceChildren(...r.location.map(c=>new Option([c.adm1,c.adm2,c.name].filter(Boolean).join(' / '),c.id)));return `找到 ${r.location.length} 个结果，请选择后保存`},$('weatherResult'));
$('save').onclick=()=>action(async wait=>{if(!$('city').value)throw Error('请先选择城市');wait('正在验证天气并保存…');const r=await ok(post('/api/save',{...api(),location:$('city').value}));ended(r.message);return r.message},$('weatherResult'));
$('holidays').onchange=()=>{$('holidayFile').textContent=$('holidays').files[0]?.name||'选择年度表 JSON 文件'};
$('import').onclick=()=>action(async wait=>{const f=$('holidays').files[0];if(!f)throw Error('请选择 JSON 文件');if(f.size>60000)throw Error('年度表不能超过 60 KB');
  const content=await f.text();try{JSON.parse(content)}catch(e){throw Error('不是有效的 JSON 文件')}
  wait('正在校验年度表…');const r=await ok(post('/api/holidays',content,true));await refreshStatus().catch(()=>{});if(pv.mode==='calendar')previewCalendar();return r.message},$('holidaysResult'));

/* ---------- Anniversary list ---------- */
const LUNAR_MONTHS=['正','二','三','四','五','六','七','八','九','十','冬','腊'];
const LUNAR_DAYS=Array.from({length:30},(_,i)=>i===9?'初十':i===19?'二十':i===29?'三十':(i<10?'初':i<20?'十':'廿')+'一二三四五六七八九十'[i%10]);
LUNAR_MONTHS.forEach((m,i)=>$('aLunarMonth').append(new Option(m+'月',i+1)));LUNAR_DAYS.forEach((d,i)=>$('aLunarDay').append(new Option(d,i+1)));
const dayNumber=s=>{const [y,m,d]=s.split('-').map(Number);return Date.UTC(y,m-1,d)/864e5};
function todayString(){const d=deviceDate();if(d)return `${d.year}-${pad(d.month)}-${pad(d.day)}`;const t=new Date();return `${t.getFullYear()}-${pad(t.getMonth()+1)}-${pad(t.getDate())}`}
function daysText(a){
  if(!a.date)return '— 天';
  const now=dayNumber(todayString()),base=dayNumber(a.date);
  if(a.lunar)return a.mode==='elapsed'?`已经 ${Math.max(0,now-base)} 天`:'农历每年';
  if(a.mode==='elapsed')return now>=base?`已经 ${now-base} 天`:`还有 ${base-now} 天`;
  let t=base;
  if(a.repeat==='yearly'){const [,m,d]=a.date.split('-').map(Number),y=+todayString().slice(0,4);const pick=yy=>{const last=new Date(Date.UTC(yy,m,0)).getUTCDate();return Date.UTC(yy,m-1,Math.min(d,last))/864e5};t=pick(y);if(t<now)t=pick(y+1)}
  return t===now?'就是今天':t>now?`还有 ${t-now} 天`:`已过 ${now-t} 天`;
}
const thumbs=new Map();let thumbGen=0;
function paintThumbs(){for(const c of $$('#annivList canvas')){const f=thumbs.get(c.dataset.id);if(f)paint(c.getContext('2d'),unpack(f),palette())}}
// Device thumbnails one at a time: the setup server has a single task.
async function loadThumbs(){
  const g=++thumbGen;
  for(const c of $$('#annivList canvas')){
    if(g!==thumbGen)return;
    try{const f=await fetchFrame(`/api/render?page=anniversary&id=${c.dataset.id}`);if(g!==thumbGen)return;thumbs.set(c.dataset.id,f.data);paint(c.getContext('2d'),unpack(f.data),palette())}catch(e){}
  }
}
async function loadList(){
  const r=await getJSON('/api/anniversaries');items=r.items||[];thumbs.clear();
  const list=$('annivList');list.replaceChildren();
  if(!items.length){const p=document.createElement('div');p.className='card empty';p.textContent='还没有纪念日。添加一个，纪念日当天屏幕会自动显示它。';list.append(p)}
  for(const a of items){
    const row=document.createElement('button');row.className='item keep';
    const kind=a.mode==='elapsed'?'经历日':'倒数日',rep=a.lunar?`农历${LUNAR_MONTHS[a.lunar.month-1]}月${LUNAR_DAYS[a.lunar.day-1]}`:a.repeat==='yearly'?'每年':'仅一次';
    row.innerHTML='<canvas width="400" height="600"></canvas><div class="info"><b></b><div class="days"></div><div class="meta"></div></div><span class="go">›</span>';
    row.querySelector('canvas').dataset.id=a.id;row.querySelector('b').textContent=a.name;row.querySelector('.days').textContent=daysText(a);
    row.querySelector('.meta').textContent=`${a.date} · ${kind} · ${rep}${a.image?' · 图片':''}`;
    row.onclick=()=>{if(!busy)openEditor(a)};
    list.append(row);
  }
  $('annivNew').disabled=items.length>=(r.max||32);
  if(pv.mode==='anniversary'&&!items.some(a=>a.id===pv.id))pv.id=items[0]?.id||'';
  renderChips();loadThumbs();
}
$('annivNew').onclick=()=>openEditor(null);

/* ---------- Editor: geometry, local dither, device-rendered draft ---------- */
const geo={img:null,rot:0,fit:'cover',zoom:1,px:0,py:0,bg:'#ffffff'};
let params={...PRESETS.balanced},hasImage=false,imageDirty=false,storedIndices=null,localIndices=null;
const editor={item:null,id:'',dirty:false,back:null};
function rotated(){const r=geo.rot%2;return [r?geo.img.height:geo.img.width,r?geo.img.width:geo.img.height]}
function scaleFor(W,H){const [rw,rh]=rotated(),f=geo.fit==='cover'?Math.max(W/rw,H/rh):Math.min(W/rw,H/rh);return f*geo.zoom}
function clampPan(){if(!geo.img)return;const [rw,rh]=rotated(),s=scaleFor(1,1.5);const mx=Math.max(0,(rw*s-1)/2),my=Math.max(0,(rh*s-1.5)/3);geo.px=Math.max(-mx,Math.min(mx,geo.px));geo.py=Math.max(-my,Math.min(my,geo.py))}
function compose(W,H,source=geo.img){
  const c=document.createElement('canvas');c.width=W;c.height=H;const ctx=c.getContext('2d',{willReadFrequently:true});
  ctx.fillStyle=geo.bg;ctx.fillRect(0,0,W,H);ctx.imageSmoothingQuality='high';
  ctx.save();ctx.translate(W/2+geo.px*W,H/2+geo.py*H);ctx.rotate(geo.rot*Math.PI/2);const s=scaleFor(W,H);ctx.scale(s,s);ctx.drawImage(source,-source.width/2,-source.height/2);ctx.restore();
  return ctx.getImageData(0,0,W,H);
}
function workingCopy(img,max=1600){const s=Math.min(1,max/Math.max(img.naturalWidth||img.width,img.naturalHeight||img.height));const c=document.createElement('canvas');c.width=Math.round((img.naturalWidth||img.width)*s);c.height=Math.round((img.naturalHeight||img.height)*s);c.getContext('2d').drawImage(img,0,0,c.width,c.height);return c}
function loadImage(src){return new Promise((good,fail)=>{const i=new Image();i.onload=()=>good(i);i.onerror=()=>fail(Error('无法读取图片'));i.src=src})}

const stage=$('stage'),sctx=stage.getContext('2d');let stageTimer=0,stageGen=0,draftTimer=0;
function changed(){editor.dirty=true;updatePush();clearTimeout(draftTimer);draftTimer=setTimeout(sendDraft,600)}
function imageChanged(){imageDirty=true;localIndices=null;editor.dirty=true;updatePush();clearTimeout(stageTimer);stageTimer=setTimeout(renderStage,250)}
// "仅图片" view: the local dither (what will be uploaded) or the source for comparison.
async function renderStage(){
  if(!hasImage||$('editor').hidden)return;
  const g=++stageGen;
  if(!geo.img){if(storedIndices){await e6().catch(()=>{});paint(sctx,storedIndices,palette())}return}
  const source=radio('stageView')==='source';
  if(source)sctx.putImageData(compose(400,600),0,0);
  if(localIndices){if(!source)paint(sctx,localIndices,palette());return}
  $('busy').textContent='处理中…';
  try{const idx=await run(compose(600,900),400,600,params);if(g!==stageGen)return;localIndices=idx;
    if(radio('stageView')!=='source')paint(sctx,idx,palette());
    if(imageDirty){clearTimeout(draftTimer);sendDraft()}}
  catch(e){note($('editorResult'),e.message,'err')}finally{if(g===stageGen)$('busy').textContent=''}
}
function currentItem(){return {id:editor.id,name:$('aName').value.trim(),date:$('aDate').value,mode:radio('mode'),repeat:radio('repeat'),
  lunar:$('aLunar').checked?{month:+$('aLunarMonth').value,day:+$('aLunarDay').value}:null,image:hasImage,overlay:radio('overlay'),band:radio('band')}}
// Body: u16 little-endian JSON length, the item JSON, then the packed image only when it changed;
// otherwise the device draws the image already stored on the card.
function sendDraft(){
  if($('editor').hidden)return;
  const a=currentItem();if(!a.date){pv.error='请选择日期';showTitle();return}
  if(!a.name)a.name='纪念日名称';
  const fresh=hasImage&&imageDirty;
  if(fresh&&!localIndices)return;
  const json=new TextEncoder().encode(JSON.stringify(a)),image=fresh?pack(localIndices):null;
  const body=new Uint8Array(2+json.length+(image?image.length:0));body[0]=json.length&255;body[1]=json.length>>8;body.set(json,2);if(image)body.set(image,2+json.length);
  previewDraft(body,a.name);
}
let drag=null;
stage.onpointerdown=e=>{if(!geo.img)return;drag={x:e.clientX,y:e.clientY,px:geo.px,py:geo.py};stage.setPointerCapture(e.pointerId)};
stage.onpointermove=e=>{if(!drag)return;const r=stage.getBoundingClientRect();geo.px=drag.px+(e.clientX-drag.x)/r.width;geo.py=drag.py+(e.clientY-drag.y)/r.height;clampPan();quick()};
stage.onpointerup=stage.onpointercancel=()=>{drag=null};
function quick(){sctx.putImageData(compose(400,600),0,0);imageChanged()}
$('rotL').onclick=()=>{geo.rot=(geo.rot+3)%4;geo.px=geo.py=0;quick()};
$('rotR').onclick=()=>{geo.rot=(geo.rot+1)%4;geo.px=geo.py=0;quick()};
$$('input[name=fit]').forEach(r=>r.onchange=()=>{geo.fit=radio('fit');clampPan();imageChanged()});
$$('input[name=bg]').forEach(r=>r.onchange=()=>{geo.bg=radio('bg');imageChanged()});
$$('input[name=stageView]').forEach(r=>r.onchange=renderStage);
$('zoom').oninput=()=>{geo.zoom=+$('zoom').value;$('zoomOut').value=geo.zoom.toFixed(2);clampPan();quick()};
$('ditherMode').onchange=imageChanged;
$('preset').onchange=()=>{params={...PRESETS[$('preset').value]};syncSliders();$('autoNote').textContent='';imageChanged()};
$('aName').oninput=changed;$('aDate').onchange=changed;$('aLunarMonth').onchange=changed;$('aLunarDay').onchange=changed;
$$('input[name=mode],input[name=repeat],input[name=overlay],input[name=band]').forEach(r=>r.onchange=changed);
$('aLunar').onchange=()=>{$('lunarRow').hidden=!$('aLunar').checked;if($('aLunar').checked)setRadio('repeat','yearly');changed()};
function buildSliders(){
  for(const [key,label,min,max,step] of SLIDERS){
    const row=document.createElement('div');row.className='slider';row.innerHTML=`<span>${label}</span><input type="range" min="${min}" max="${max}" step="${step}" id="s_${key}"><output id="o_${key}"></output>`;
    $('sliders').append(row);
    row.querySelector('input').oninput=e=>{params[key]=+e.target.value;$('o_'+key).value=(+e.target.value).toFixed(step<1?2:0);imageChanged()};
  }
}
function syncSliders(){for(const [key,,,,step] of SLIDERS){$('s_'+key).value=params[key];$('o_'+key).value=(+params[key]).toFixed(step<1?2:0)}}

/* ---------- Auto: ISP-like AE / AWB / detail, then panel-simulated candidate search ---------- */
const lin=c=>c<=.04045?c/12.92:((c+.055)/1.055)**2.4;
function analyse(im){
  const d=im.data,n=im.width*im.height,lum=new Float32Array(n);let skin=0,sky=0,leaf=0,sat=0,wr=0,wg=0,wb=0,wn=0;
  for(let i=0;i<n;i++){
    const r=d[i*4]/255,g=d[i*4+1]/255,b=d[i*4+2]/255,mx=Math.max(r,g,b),mn=Math.min(r,g,b),s=mx?(mx-mn)/mx:0;lum[i]=.2126*r+.7152*g+.0722*b;sat+=s;
    let h=0;if(mx>mn){h=mx===r?((g-b)/(mx-mn))%6:mx===g?(b-r)/(mx-mn)+2:(r-g)/(mx-mn)+4;h*=60;if(h<0)h+=360}
    const isSkin=h>=8&&h<=50&&s>.15&&s<.6&&mx>.35,isSky=h>=190&&h<=250&&s>.15&&mx>.4,isLeaf=h>=70&&h<=160&&s>.2;
    skin+=isSkin;sky+=isSky;leaf+=isLeaf;
    if(!isSkin&&!isSky&&!isLeaf&&s<.25&&mx>.1&&mx<.97){wr+=r;wg+=g;wb+=b;wn++}
  }
  const sorted=lum.slice().sort();const q=p=>sorted[Math.min(n-1,Math.floor(p*n))];
  return {p02:q(.02),p50:q(.5),p98:q(.98),skin:skin/n,sky:sky/n,leaf:leaf/n,sat:sat/n,gray:wn>n*.05?[wr/wn,wg/wn,wb/wn]:null,protect:(skin+sky+leaf)/n};
}
function autoBase(st){
  const scene=st.skin>.12?'portrait':st.sky+st.leaf>.3?'landscape':st.sat<.08&&st.p98-st.p02>.6?'document':'balanced';
  const p={...PRESETS[scene]};
  // Move the median into a comfortable band only; keep 1/3 EV highlight headroom when highlights are near clipping.
  const m=Math.max(.02,st.p50),goal=Math.max(.38,Math.min(.6,m));
  const headroom=Math.max(0,Math.log2(lin(.96)/lin(Math.max(.05,st.p98))));
  p.exposure=Math.max(-1.5,Math.min(1.5,headroom,Math.log2(lin(goal)/lin(m))))-(st.p98>.92?1/3:0);
  p.contrast*=Math.max(.9,Math.min(1.3,.8/Math.max(.2,st.p98-st.p02)));
  p.gamma=st.p50<.3?1.12:st.p50>.7?.92:1;
  if(st.gray){const [r,g,b]=st.gray,k=1-Math.min(.8,st.protect);p.temperature=Math.max(-.6,Math.min(.6,(b-r)*3*k));p.tint=Math.max(-.5,Math.min(.5,(g-(r+b)/2)*4*k))}
  return {scene,p};
}
const SCENES={balanced:'均衡',landscape:'风景',portrait:'人像',document:'文档'};
async function autoTune(source=geo.img){
  const small=compose(150,225,source),st=analyse(compose(100,150,source)),{scene,p}=autoBase(st);let best=null;
  // Reference = ISP-style AE/AWB result; the search then picks what the panel reproduces best.
  const reference=await adjusted(small,p);
  for(const de of [-.33,0,.33])for(const ds of [-.15,0,.15]){
    const c={...p,exposure:p.exposure+de,saturation:p.saturation+ds};
    const s=await score(reference,await run(small,100,150,c,1),100,150);
    if(!best||s<best.s)best={s,c};
  }
  params=best.c;$('preset').value=scene;syncSliders();
  $('autoNote').textContent=`自动：${SCENES[scene]}场景，曝光 ${params.exposure>=0?'+':''}${params.exposure.toFixed(2)} EV，饱和度 ${params.saturation.toFixed(2)}。可继续手动微调。`;
  imageChanged();clearTimeout(stageTimer);await renderStage();
}
$('auto').onclick=()=>action(async wait=>{if(!geo.img)throw Error('请先选择图片');wait('正在自动调色…');await autoTune();return '自动调色完成'},$('editorResult'));

/* ---------- Optional ONNX (files under /papercolor/web/ort/ on the TF card) ---------- */
let ai=null,aiManifest=null,original=null;
async function aiInit(){try{const r=await fetch('/sdweb/ort/manifest.json',{cache:'no-store'});if(!r.ok)return;aiManifest=await r.json();$('aiAuto').hidden=!(aiManifest.enhance||aiManifest.saliency)}catch(e){}}
function script(src){return new Promise((good,fail)=>{const s=document.createElement('script');s.src=src;s.onload=good;s.onerror=()=>fail(Error('无法加载 '+src));document.head.append(s)})}
async function aiSessions(wait){
  if(ai)return ai;
  wait('首次加载 ONNX Runtime，约需十几秒…');
  await script('/sdweb/ort/'+(aiManifest.runtime||'ort.wasm.min.js'));
  // Plain-HTTP setup page: no WebGPU and no SharedArrayBuffer, so single-threaded WASM only.
  ort.env.wasm.wasmPaths='/sdweb/ort/';ort.env.wasm.numThreads=1;
  ai={};
  for(const k of ['enhance','saliency'])if(aiManifest[k])ai[k]=await ort.InferenceSession.create('/sdweb/ort/'+aiManifest[k].file,{executionProviders:['wasm']});
  return ai;
}
function tensor(img,size,mean=[0,0,0],std=[1,1,1]){
  const c=document.createElement('canvas');c.width=c.height=size;const ctx=c.getContext('2d');ctx.drawImage(img,0,0,size,size);
  const d=ctx.getImageData(0,0,size,size).data,t=new Float32Array(3*size*size);
  for(let i=0;i<size*size;i++)for(let k=0;k<3;k++)t[k*size*size+i]=(d[i*4+k]/255-mean[k])/std[k];
  return {t,d};
}
// Fit per-channel tone curves from the model's low-res output and apply them to the full image.
function curvesFrom(input,output,size){
  const curves=[0,1,2].map(k=>{const sum=new Float64Array(256),cnt=new Float64Array(256);
    for(let i=0;i<size*size;i++){const v=input[i*4+k];sum[v]+=Math.max(0,Math.min(1,output[k*size*size+i]));cnt[v]++}
    const c=new Uint8ClampedArray(256);let last=0;
    for(let v=0;v<256;v++){const y=cnt[v]?sum[v]/cnt[v]*255:-1;last=Math.max(last,y<0?last:y);c[v]=last}
    return c});
  return curves;
}
// The image as currently rotated, so the saliency centroid maps straight onto the pan offsets.
function upright(img){const r=geo.rot%2,c=document.createElement('canvas');c.width=r?img.height:img.width;c.height=r?img.width:img.height;
  const ctx=c.getContext('2d');ctx.translate(c.width/2,c.height/2);ctx.rotate(geo.rot*Math.PI/2);ctx.drawImage(img,-img.width/2,-img.height/2);return c}
$('aiAuto').onclick=()=>action(async wait=>{
  if(!geo.img)throw Error('请先选择图片');const s=await aiSessions(wait),base=original||geo.img;
  if(s.saliency&&geo.fit==='cover'){
    const m=aiManifest.saliency,size=m.size||320,view=upright(base),{t}=tensor(view,size,m.mean,m.std);
    const out=(await s.saliency.run({[s.saliency.inputNames[0]]:new ort.Tensor('float32',t,[1,3,size,size])}))[s.saliency.outputNames[0]].data;
    let lo=Infinity,hi=-Infinity;for(const v of out){lo=Math.min(lo,v);hi=Math.max(hi,v)}
    let sx=0,sy=0,sw=0;const side=Math.round(Math.sqrt(out.length));
    for(let i=0;i<out.length;i++){const w=((out[i]-lo)/(hi-lo||1))**2;sx+=w*(i%side+.5);sy+=w*(Math.floor(i/side)+.5);sw+=w}
    if(sw>0){const k=scaleFor(1,1.5);geo.px=(.5-sx/sw/side)*view.width*k;geo.py=(.5-sy/sw/side)*view.height*k/1.5;clampPan()}
  }
  let source=base;
  if(s.enhance){
    const m=aiManifest.enhance,size=m.size||256,{t,d}=tensor(base,size);
    const out=(await s.enhance.run({[s.enhance.inputNames[0]]:new ort.Tensor('float32',t,[1,3,size,size])}))[s.enhance.outputNames[0]].data;
    const curves=curvesFrom(d,out,size),c=document.createElement('canvas');c.width=base.width;c.height=base.height;const ctx=c.getContext('2d');ctx.drawImage(base,0,0);
    const im=ctx.getImageData(0,0,c.width,c.height);for(let i=0;i<im.data.length;i+=4)for(let k=0;k<3;k++)im.data[i+k]=curves[k][im.data[i+k]];ctx.putImageData(im,0,0);
    source=c;original=base;geo.img=c;
  }
  wait('AI 预处理完成，正在按墨水屏模拟打分…');
  await autoTune(source);return 'AI 自动完成：模型只做预增强和取景，最终参数由墨水屏模拟打分选出';
},$('editorResult'));

/* ---------- Editor: open, save, close ---------- */
function resetImage(){geo.img=null;geo.rot=0;geo.fit='cover';geo.zoom=1;geo.px=geo.py=0;geo.bg='#ffffff';hasImage=false;imageDirty=false;storedIndices=null;localIndices=null;original=null;
  params={...PRESETS.balanced};$('preset').value='balanced';setRadio('fit','cover');setRadio('bg','#ffffff');setRadio('stageView','dither');$('zoom').value=1;$('zoomOut').value='1.00';
  $('aImage').value='';$('autoNote').textContent='';syncSliders();$('imageTools').hidden=true;$('pickLabel').textContent='选择照片'}
function newId(){return (Date.now().toString(36)+Math.random().toString(36).slice(2,6)).slice(-12)}
async function openEditor(a){
  editor.item=a||null;editor.id=a?.id||newId();editor.dirty=false;editor.back={mode:pv.mode,id:pv.id,year:pv.year,month:pv.month};resetImage();
  $('editorTitle').textContent=a?'编辑纪念日':'添加纪念日';$('aName').value=a?.name||'';$('aDate').value=a?.date||todayString();
  setRadio('mode',a?.mode||'countdown');setRadio('repeat',a?.repeat||'yearly');$('aLunar').checked=!!a?.lunar;$('lunarRow').hidden=!a?.lunar;
  if(a?.lunar){$('aLunarMonth').value=a.lunar.month;$('aLunarDay').value=a.lunar.day}
  setRadio('overlay',a?.overlay||'bottom');setRadio('band',a?.band||'white');
  $('aDelete').hidden=!a;$('editorResult').textContent='';
  $('editor').hidden=false;$('editor').scrollTop=0;document.body.classList.add('editing');
  card.classList.remove('compact');$('editorDock').append(card);scroller=$('editor');measureDock();
  if(a?.image){
    hasImage=true;$('imageTools').hidden=false;$('pickLabel').textContent='更换照片';
    try{
      const meta=await fetch(`/api/image?id=${a.id}&kind=json`,{cache:'no-store'}).then(r=>r.ok?r.json():null);
      const jpg=await fetch(`/api/image?id=${a.id}&kind=jpg`,{cache:'no-store'});
      if(jpg.ok){geo.img=workingCopy(await loadImage(URL.createObjectURL(await jpg.blob())))}
      else{const r=await fetch(`/api/image?id=${a.id}&kind=e6`,{cache:'no-store'});if(r.ok)storedIndices=unpack(new Uint8Array(await r.arrayBuffer()))}
      if(meta){Object.assign(geo,meta.geo||{});params={...PRESETS.balanced,...meta.params};$('preset').value=meta.preset||'balanced';$('ditherMode').value=meta.mode??0;
        setRadio('fit',geo.fit);setRadio('bg',geo.bg);$('zoom').value=geo.zoom;$('zoomOut').value=(+geo.zoom).toFixed(2);syncSliders()}
    }catch(e){note($('editorResult'),'图片参数读取失败，可重新选择图片','err')}
  }
  sendDraft();renderStage();
}
function closeEditor(){
  clearTimeout(draftTimer);clearTimeout(stageTimer);stageGen++;
  $('editor').hidden=true;document.body.classList.remove('editing');
  card.classList.remove('compact');$('mainDock').append(card);scroller=window;measureDock();onScroll();
  const saved=editor.item,back=editor.back;editor.item=null;
  if(saved&&items.some(x=>x.id===saved.id))previewAnniversary(saved.id);
  else if(back?.mode==='anniversary')previewAnniversary(back.id);
  else previewCalendar(back?.year||0,back?.month||0);
}
$('aCancel').onclick=()=>{if(editor.dirty&&!confirm('有未保存的修改，确定离开？'))return;closeEditor()};
$('aImage').onchange=()=>action(async wait=>{const f=$('aImage').files[0];if(!f)return;wait('正在读取图片…');
  const url=URL.createObjectURL(f);try{geo.img=workingCopy(await loadImage(url))}finally{URL.revokeObjectURL(url)}
  hasImage=true;storedIndices=null;original=null;geo.rot=0;geo.px=geo.py=0;geo.zoom=1;$('zoom').value=1;$('zoomOut').value='1.00';$('imageTools').hidden=false;$('pickLabel').textContent='更换照片';
  wait('正在自动调色…');await autoTune();return '图片已载入：可旋转、取景、选预设或手动调整，然后保存'},$('editorResult'));
$('removeImage').onclick=()=>{resetImage();imageDirty=true;changed()};
function blob(canvas,type,q){return new Promise(good=>canvas.toBlob(good,type,q))}
$('aSave').onclick=()=>action(async wait=>{
  const a=currentItem();if(!a.name)throw Error('请填写名称');if(!a.date)throw Error('请选择日期');
  if(hasImage&&imageDirty&&geo.img){
    const idx=localIndices||await run(compose(600,900),400,600,params);
    wait('正在写入 TF 卡（约 120 KB）…');await ok(post(`/api/image?id=${a.id}&kind=e6`,pack(idx),true));
    await ok(post(`/api/image?id=${a.id}&kind=jpg`,await blob(geo.img,'image/jpeg',.88),true));
    const meta={geo:{rot:geo.rot,fit:geo.fit,zoom:geo.zoom,px:geo.px,py:geo.py,bg:geo.bg},params,preset:$('preset').value,mode:+$('ditherMode').value};
    await ok(post(`/api/image?id=${a.id}&kind=json`,JSON.stringify(meta),true));
  }
  wait('正在保存…');const r=await ok(post('/api/anniversaries',a));
  editor.item={...a,image:r.image};editor.dirty=false;imageDirty=false;$('aDelete').hidden=false;$('editorTitle').textContent='编辑纪念日';
  await loadList();refreshStatus().catch(()=>{});clearTimeout(draftTimer);sendDraft();
  return r.message+(r.missing?`：${r.missing}`:'');
},$('editorResult'));
$('aDelete').onclick=()=>action(async()=>{
  const a=editor.item;if(!a||!confirm(`删除“${a.name}”及其图片？`))return;
  const r=await ok(post('/api/anniversaries/delete',{id:a.id}));editor.item=null;editor.dirty=false;
  await loadList();refreshStatus().catch(()=>{});closeEditor();toast(r.message);
},$('editorResult'));

/* ---------- Start ---------- */
buildSliders();syncSliders();
(async()=>{
  showTab(['screen','anniv','settings'].includes(location.hash.slice(1))?location.hash.slice(1):'screen');
  try{await refreshStatus()}catch(e){pv.error=e.message||'无法连接设备，请重新连接热点并打开 192.168.4.1';showTitle();return}
  e6().then(()=>{paintFrame();paintThumbs()}).catch(()=>{});
  previewCalendar();renderChips();
  if(device.card!==false){await loadList().catch(e=>toast(e.message,false));aiInit()}
})();
