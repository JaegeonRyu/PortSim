const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
export const mapPosition=a=>a?.gantry_position_m||a?.position_m;
export function controlPanel(a,state,live,pending,message=''){
 if(!['sts','rmg','agv'].includes(a?.type))return '<div class="note">STS·RMG·AGV를 선택하면 고장을 제어할 수 있습니다.</div>';
 const failed=a.fault_scope==='equipment',disabled=!live||!state?.control_session||!!state.fault||!!pending;
 return `<section class="fault-controls"><h3>장비 고장 · 복구</h3><p>${esc(a.name)} · ${a.type==='agv'?'주행 고장':'필수 센서 고장'}</p><div class="control-actions"><button class="button danger" data-command="fail" ${disabled||failed?'disabled':''}>오류 발생</button><button class="button" data-command="repair" ${disabled||!failed?'disabled':''}>수리 · 복구</button></div><p class="control-message" role="status">${esc(message||(!live?'최근 기록은 제어할 수 없습니다. 언리얼 시뮬레이션을 실행하세요.':state?.fault?'전체 안전 정지 원인을 먼저 해결하세요.':!state?.control_session?'제어 기능이 포함된 엔진으로 다시 실행하세요.':failed?'고장 적용 상태 · 관련 작업 격리':'실시간 엔진에 선택 장비의 고장을 적용합니다.'))}</p><div class="note">${a.type==='agv'?'AGV는 현재 위치·화물·예약을 유지하고 수리를 기다립니다. 다른 AGV는 기존 도로 예약·LiDAR로 대기하거나 우회합니다.':'이미 잠기거나 매달린 화물은 유지합니다. 미인양 STS 작업과 RMG 목적지는 도달 범위·적재 조건을 만족할 때 재배정합니다.'}</div></section>`;
}

export function impactModel(state,focus){
 const actors=state?.actors||[],events=(state?.recovery_events||[]).filter(e=>!focus||e.cause_actor===focus.cause&&e.sequence>focus.after);
 const related=new Set(),cargos=new Set();
 for(const e of events){related.add(e.cause_actor);related.add(e.from_actor);related.add(e.to_actor);cargos.add(e.cargo);}
 if(focus)related.add(focus.cause);
 for(const a of actors)if(a.fault_scope==='equipment')related.add(a.id);
 for(const a of actors)if(a.type==='agv'&&(related.has(a.sts)||related.has(a.rmg)||cargos.has(a.cargo_actor)||a.equipment_waiting||a.recovery_route_pending))related.add(a.id);
 // Include queues caused by a failed/affected vehicle, including indirect blockers.
 for(let n=0;n<actors.length;n++){
  let changed=false;
  for(const a of actors)if(a.type==='agv'&&!related.has(a.id)&&actors.some(b=>b.type==='agv'&&related.has(b.id)&&Number.isInteger(b.vehicle_id)&&
    [a.fms_blocking_agv,a.lidar_blocking_agv].includes(b.vehicle_id))){related.add(a.id);changed=true;}
  if(!changed)break;
 }
 return {events,related};
}

export function impactFeed(state,focus,live){
 const {events,related}=impactModel(state,focus),actors=state?.actors||[],name=id=>actors.find(a=>a.id===id)?.name||id;
 const groups=new Map();
 for(const e of events){const key=`${e.kind}|${e.from_actor}|${e.to_actor}`;const old=groups.get(key);groups.set(key,{...e,count:(old?.count||0)+1});}
 const types={fault:'고장 격리',repair:'수리',sts_reassigned:'STS 작업 재배정',yard_reassigned:'야드 목적지 재배정'};
 return `<section class="impact-feed"><div class="impact-heading"><strong>고장 영향 · 재배정 기록</strong><span>${live?'엔진 실시간':'최근 실행 기록'} · 영향 대상 ${actors.filter(a=>related.has(a.id)&&['sts','rmg','agv'].includes(a.type)).length}대</span><button class="link" data-impact="all">전체 기록</button></div><div class="impact-events">${[...groups.values()].slice(-8).reverse().map(e=>`<button class="impact-event" data-actor="${esc(e.to_actor)}"><b>${esc(types[e.kind]||e.kind)}${e.count>1?` ${e.count}건`:''}</b><span>${esc(name(e.from_actor))}${e.to_actor!==e.from_actor?` → ${esc(name(e.to_actor))}`:''} · ${Math.floor(e.simulation_seconds)}초</span></button>`).join('')||'<p>고장을 실행하면 실제 엔진의 격리·재배정 결과가 여기에 표시됩니다.</p>'}</div><small>빨강: 고장 · 주황: 영향 대상 · 파랑: AGV 잔여 경로 · 주황 점선: 계획 재배정 연결(실제 이동 경로와 구분)</small></section>`;
}

export function impactOverlay(state,focus,X,Y,selected){
 const {events,related}=impactModel(state,focus),actors=state?.actors||[];
 const points=ps=>ps.filter(p=>Array.isArray(p)&&p.slice(0,2).every(Number.isFinite)).map(p=>`${X(p[0])},${Y(p[1])}`).join(' ');
 let html='';
 for(const e of events.filter(e=>['sts_reassigned','yard_reassigned'].includes(e.kind)).slice(-40))html+=`<polyline class="plan-connection" points="${points([e.from_position_m,e.to_position_m])}"/>`;
 for(const a of actors.filter(a=>['sts','rmg','agv'].includes(a.type))){
  const p=mapPosition(a);if(!p)continue;
  if(a.fault)html+=`<circle class="fault-halo" cx="${X(p[0])}" cy="${Y(p[1])}" r="9"/>`;
  else if(related.has(a.id))html+=`<circle class="impact-halo" cx="${X(p[0])}" cy="${Y(p[1])}" r="7"/>`;
  if(related.has(a.id)||a.id===selected){
   if(a.type==='agv'&&a.route?.length)html+=`<polyline class="impact-route" points="${points([p,...a.route.map(n=>n.position_m)])}"/>`;
  }
 }
 return `<g class="impact-overlay" pointer-events="none">${html}</g>`;
}
