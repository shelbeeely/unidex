#pragma once
// The House's phone page, served by the X4's own hotspot (house/phone.cpp): each roommate's Cash App link with Open,
// Share, Copy and Text, and a box to paste a payment request made in Cash App. "/*STATE*/null" is replaced by the
// household's pay info (phone.cpp's stateJson), so the page draws at once.
//
// Phones only open their share sheet (navigator.share) from secure (https) pages, and this one is plain http from
// the hotspot, so Share uses it where the phone allows and otherwise offers Copy, Text and Email, which work on iOS
// and Android alike. Copy uses the old execCommand way for the same reason (the clipboard API needs https too).
static const char HOUSE_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>House: pay</title>
<style>
:root{--ink:#111;--paper:#fff;--soft:#f2f2f2;--line:#ccc;--green:#00a83b}
@media (prefers-color-scheme:dark){:root{--ink:#eee;--paper:#141414;--soft:#222;--line:#444}}
*{box-sizing:border-box}body{margin:0;font:16px/1.4 -apple-system,system-ui,sans-serif;background:var(--paper);color:var(--ink)}
main{max-width:520px;margin:0 auto;padding:16px}h1{font-size:22px;margin:4px 0 2px}h2{font-size:17px;margin:22px 0 8px}
p.note{margin:0 0 12px;color:#888;font-size:14px}
.card{border:1px solid var(--line);border-radius:14px;padding:12px 14px;margin:10px 0;background:var(--soft)}
.row{display:flex;justify-content:space-between;align-items:baseline;gap:8px}.name{font-weight:600}
.owe{font-size:14px}.btns{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
a.btn,button{font:inherit;font-size:15px;border:0;border-radius:999px;padding:9px 14px;text-decoration:none;cursor:pointer}
.go{background:var(--green);color:#fff}.alt{background:var(--paper);color:var(--ink);border:1px solid var(--line)!important}
input,select{font:inherit;width:100%;padding:10px;border:1px solid var(--line);border-radius:10px;background:var(--paper);color:var(--ink);margin:4px 0 10px}
label{font-size:14px;color:#888}.link{font-size:13px;word-break:break-all;color:#888;margin-top:6px}
#sheet{position:fixed;left:0;right:0;bottom:0;background:var(--paper);border-top:1px solid var(--line);padding:16px;display:none;box-shadow:0 -8px 24px #0004}
#sheet input{font-size:14px}#msg{min-height:20px;color:var(--green);font-size:14px}
</style></head><body><main>
<h1>Pay your roommates</h1>
<p class="note">Open goes to Cash App (it needs the internet: your phone switches back to your own WiFi or data when
you leave this page). Nothing is paid through the unidex.</p>
<div id="people"></div>
<h2>Paste a Cash App request</h2>
<p class="note">In Cash App: Request, the amount, then Share link. Copy it and paste it here. Your roommates' unidex
show it as the code to pay you, after the next Sync.</p>
<label for="text">Request link (or everything Cash App shared)</label>
<input id="text" placeholder="https://cash.app/pay/link/..." autocomplete="off">
<label for="amt">Amount (optional)</label>
<input id="amt" inputmode="decimal" placeholder="15.00">
<label for="for">For</label>
<select id="for"></select>
<button class="go" onclick="addLink()">Save request</button>
<div id="msg"></div>
<div id="mine"></div>
</main>
<div id="sheet"><div class="row"><b id="shTitle"></b><button class="alt" onclick="closeSheet()">Close</button></div>
<input id="shLink" readonly onclick="this.select()">
<div class="btns"><button class="go" onclick="copyLink()">Copy link</button><a class="btn alt" id="shText">Text it</a>
<a class="btn alt" id="shMail">Email</a></div><div id="shMsg" class="note"></div></div>
<script>
var S=/*STATE*/null;
function esc(s){return String(s).replace(/[&<>"']/g,function(c){return'&#'+c.charCodeAt(0)+';'})}
function money(p){return '$'+(p/100).toFixed(2)}
function payUrl(p){return p.req?p.req:p.tag?'https://cash.app/$'+p.tag+(p.owe?'/'+(p.owe/100).toFixed(2):''):''}
function draw(){
 var h='';S.people.forEach(function(p,i){
  var u=payUrl(p),line=p.owe?'You owe '+money(p.owe):p.owed?'Owes you '+money(p.owed):'Even';
  h+='<div class="card"><div class="row"><span class="name">'+esc(p.name)+'</span><span class="owe">'+line+'</span></div>';
  h+='<div class="link">'+(p.tag?'$'+esc(p.tag):'No $cashtag yet')+(p.req?' &middot; request '+(p.reqAmt?money(p.reqAmt):''):'')+'</div>';
  if(u)h+='<div class="btns"><a class="btn go" href="'+esc(u)+'">Open in Cash App</a><button class="alt" onclick="share('+i+')">Share</button></div>';
  h+='</div>'});
 if(!S.people.length)h='<p class="note">No roommates yet: join or start a household on the unidex, then Sync.</p>';
 document.getElementById('people').innerHTML=h;
 var f='<option value="0">Anyone</option>';S.people.forEach(function(p){f+='<option value="'+p.id+'">'+esc(p.name)+'</option>'});
 document.getElementById('for').innerHTML=f;
 var m='';S.mine.forEach(function(r){m+='<div class="card"><div class="row"><span>'+(r.amt?money(r.amt):'Request')+' for '+esc(r.for)+'</span><button class="alt" onclick="removeLink('+r.seq+')">Remove</button></div><div class="link">'+esc(r.url)+'</div></div>'});
 document.getElementById('mine').innerHTML=m?'<h2>Your requests</h2>'+m:'';
}
var cur='';
function share(i){var p=S.people[i],u=payUrl(p),t='Pay '+p.name+(p.owe?' '+money(p.owe):'')+' on Cash App';
 if(navigator.share){navigator.share({title:t,text:t,url:u}).catch(function(){});return}
 cur=u;document.getElementById('shTitle').textContent=t;document.getElementById('shLink').value=u;
 var ios=/iPhone|iPad|iPod/.test(navigator.userAgent),b=encodeURIComponent(t+': '+u);
 document.getElementById('shText').href=(ios?'sms:&body=':'sms:?body=')+b;
 document.getElementById('shMail').href='mailto:?subject='+encodeURIComponent(t)+'&body='+b;
 document.getElementById('shMsg').textContent='Or press and hold the link to share it.';
 document.getElementById('sheet').style.display='block'}
function closeSheet(){document.getElementById('sheet').style.display='none'}
function copyLink(){var e=document.getElementById('shLink');e.focus();e.select();e.setSelectionRange(0,999);
 var ok=false;try{ok=document.execCommand('copy')}catch(x){}
 document.getElementById('shMsg').textContent=ok?'Copied':'Select the link and copy it'}
function get(u,done){var x=new XMLHttpRequest();x.open('GET',u);x.onload=function(){try{done(JSON.parse(x.responseText))}catch(e){done({ok:0,msg:'No answer from the unidex'})}};x.onerror=function(){done({ok:0,msg:'Not connected to the unidex'})};x.send()}
function say(t){document.getElementById('msg').textContent=t}
function addLink(){var t=document.getElementById('text').value,a=document.getElementById('amt').value,f=document.getElementById('for').value;
 get('/add?text='+encodeURIComponent(t)+'&amt='+encodeURIComponent(a)+'&for='+f,function(r){say(r.msg);if(r.ok){document.getElementById('text').value='';document.getElementById('amt').value='';S=r.state;draw()}})}
function removeLink(s){get('/remove?seq='+s,function(r){say(r.msg);if(r.state){S=r.state;draw()}})}
if(S)draw();else get('/state.json',function(r){S=r;draw()});
</script></body></html>)HTML";
