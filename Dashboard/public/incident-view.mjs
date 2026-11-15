import {incidentActions,weatherFields,triggers,cleanScenario} from './incident-model.mjs';
const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const num=v=>Number.isFinite(v)?v.toLocaleString('ko-KR',{maximumFractionDigits:1}):'—';
export const stepLabels={pending:'발생 조건 대기',active:'상황 대응·복구 중',recovering:'복구 진행·재인식 확인',awaiting_completion:'복구 후 작업 완료 대기',verified:'검증 통과',rejected:'적용 거절',timeout:'시간 초과',cancelled:'미발생 단계 취소',not_exercised:'대응 미관측'};
const terminal=s=>['verified','rejected','timeout','cancelled','not_exercised'].includes(s);
const actorName=(state,id)=>id==='environment'?'날씨':state?.actors?.find(a=>a.id===id)?.name||id;
const actionName=a=>Object.values(incidentActions).flat().find(([k])=>k===a)?.[1]||a;
export function incidentRunStatus(run){
 const steps=run.steps||[];if(steps.some(s=>['rejected','timeout','not_exercised'].includes(s.state)))return '확인 필요';
 if(steps.length&&steps.every(s=>s.state==='verified'))return '검증 통과';
 if(steps.length&&steps.every(s=>terminal(s.state)))return '종료 · 취소 포함';
 return run.cancelled?'미발생 단계 취소 · 기존 상황 추적 중':'실행 중';
}
function renderIncidentIssue(actor, live) {
 const kinds = new Set(actor.incident_faults || []);
 if (actor.incident_needs_position_fix) kinds.add('localization_loss');
 if (actor.drive_failure) kinds.add('fail');
 if (actor.incident_sensor_failure || actor.sensor_failure) kinds.add('fail_reboot');
 if (actor.incident_destination_mismatch) kinds.add('misdelivery');
 if (actor.incident_pickup_failure || actor.pickup_failure) kinds.add('fail_pickup');
 if (actor.incident_placement_failure) kinds.add('fail_placement');
 if (actor.gantry_failure) kinds.add('fail_gantry');
 if (actor.emergency_test?.active) kinds.add('worker_enter');
 const phase = {failed:'재부팅 요청 대기',rebooting:'재부팅 중',confirming:'위치·관측 재확인 중',self_check:'센서 자체 점검 중'}[actor.incident_sensor_phase];
 const disabled = live ? '' : 'disabled';
 const id = esc(actor.id);
 return `<div class="incident-issue"><b>${esc(actor.name)}</b>
  <span>${esc(actor.fault || [...kinds].map(actionName).join(' / '))}${phase ? ` · ${phase}` : ''}</span>
  ${actor.can_replace_loaded_agv ? `<button class="button secondary" data-incident-action="replace" data-actor-id="${id}" ${disabled}>대체 AGV · R/S 출동</button>` : ''}
  ${[...kinds].map(kind => `<button class="button secondary" data-incident-action="recover" data-actor-id="${id}" data-fault-action="${esc(kind)}" ${disabled}>${kind === 'fail_reboot' ? '센서 재부팅 요청' : `${esc(actionName(kind))} 해제`}</button>`).join('')}
 </div>`;
}

function renderIncidentRun(run, state, live, status, tone) {
 const steps = run.steps || [];
 const verified = steps.filter(step => step.state === 'verified').length;
 const disabled = live ? '' : 'disabled';
 const id = esc(run.id);
 return `<details class="incident-run" name="incident-runs" data-incident-disclosure="run-${id}">
  <summary class="incident-run-summary">
   <span class="incident-run-title" title="${esc(run.name)}">${esc(run.name)}</span>
   <span class="incident-run-count" aria-label="${steps.length}단계 중 ${verified}단계 검증 통과">${verified}/${steps.length} 통과</span>
   <span class="badge ${tone}">${status}</span>
  </summary>
  <div class="incident-run-detail" data-incident-scroll="run-${id}" tabindex="0" role="region" aria-label="${esc(run.name)} 단계 기록">
   <div class="incident-run-meta"><span>시작 ${num(run.started_at)}초</span>
    ${steps.some(step => step.state === 'pending') ? `<button class="button secondary" data-incident-action="cancel" data-run="${id}" ${disabled}>미발생 단계 취소</button>` : ''}
   </div>
   <ol>${steps.map((step, index) => `<li class="incident-step state-${esc(step.state)}">
    <div><span class="incident-sequence">${index + 1}</span><div><b>${esc(actorName(state, step.actor_id))} · ${esc(actionName(step.action))}</b>
     <p>${esc(triggers[step.trigger] || triggers.immediate)} · 지연 ${num(step.delay || 0)}초 · ${step.duration ? `${num(step.duration)}초 후 복구 요청` : '수동·장비 자체 복구'}</p></div>
     <strong>${esc(stepLabels[step.state] || step.state)}</strong></div>
    <p class="incident-message">${esc(step.message || '엔진에서 발생 조건을 확인합니다.')}${step.cargo && step.cargo !== 'None' ? ` · 화물 ${esc(step.cargo)}` : ''}</p>
    ${step.state === 'active' && step.actor_id !== 'environment' ? `<button class="button" data-incident-action="recover" data-actor-id="${esc(step.actor_id)}" data-fault-action="${esc(step.action)}" ${disabled}>${step.action === 'worker_enter' ? '작업자 제거' : step.action === 'fail_reboot' ? '센서 재부팅 요청' : '수리·복구 요청'}</button>` : ''}
   </li>`).join('')}</ol>
  </div>
 </details>`;
}

export function renderIncidentRuns(state, live) {
 const runs = [...(state?.incident_scenarios || [])].reverse();
 const affected = (state?.actors || []).filter(actor => actor.fault || actor.incident_pickup_failure ||
  actor.incident_placement_failure || actor.incident_faults?.length || actor.incident_needs_position_fix || actor.emergency_test?.active);
 const groups = { working: [], fault: [], verified: [], recorded: [] };
 for (const run of runs) {
  const status = incidentRunStatus(run);
  const tone = status === '확인 필요' ? 'fault' : status === '검증 통과' ? 'verified' :
   status === '종료 · 취소 포함' ? 'recorded' : 'working';
  groups[tone].push(renderIncidentRun(run, state, live, status, tone));
 }
 const operation = !live ? '최근 저장 기록 · 명령 실행 불가' : state?.manual_paused ? '수동 일시정지' :
  state?.fault ? '안전 정지' : '운송 진행 중';
 const weather = state?.environment;
 return `<div class="incident-summary">
   <div><small>진행 중</small><strong>${groups.working.length}</strong></div>
   <div><small>검증 통과</small><strong>${groups.verified.length}</strong></div>
   <div><small>확인 필요</small><strong>${groups.fault.length}</strong></div>
  </div>
  <div class="incident-context"><span>${operation} · ${num(state?.incident_clock_seconds)}초</span>
   <span title="${esc(state?.weather_hold?.reason || '')}">${state?.weather_hold?.active ? '기상 회복 대기' : '기상 운용 정상'}</span>
   <span>비 ${num(weather?.rain_mm_h)} · 눈 ${num(weather?.snow_mm_h)} mm/h</span>
   <span>풍속 ${num(weather?.effective_wind_mps)} m/s · 시정 ${weather?.fog_m ? `${num(weather.fog_m)} m` : '제한 없음'}</span>
  </div>
  ${affected.length ? `<details class="incident-equipment" data-incident-disclosure="equipment">
   <summary>현재 문제 장비 ${affected.length}대</summary>
   <div class="incident-issues" data-incident-scroll="equipment" tabindex="0" role="region" aria-label="문제 장비와 복구 명령">${affected.map(actor => renderIncidentIssue(actor, live)).join('')}</div>
  </details>` : ''}
  ${runs.length ? `<div class="incident-history-label"><span>실행 기록 ${runs.length}건</span><span>선택해서 단계 보기</span></div>
   <div class="incident-history" data-incident-scroll="history" tabindex="0" role="region" aria-label="시나리오 실행 기록">${Object.values(groups).flat().join('')}</div>` :
   '<div class="incident-empty"><strong>등록된 시나리오가 없습니다.</strong><p>상황을 조합하고 실행하면 여기에 표시됩니다.</p></div>'}`;
}

export function refreshIncidentRuns(panel, state, live) {
 const html = renderIncidentRuns(state, live);
 if (panel.incidentMarkup === html) return;
 const open = new Set([...panel.querySelectorAll('[data-incident-disclosure][open]')].map(el => el.dataset.incidentDisclosure));
 const scroll = new Map([...panel.querySelectorAll('[data-incident-scroll]')].map(el => [el.dataset.incidentScroll, el.scrollTop]));
 const active = panel.ownerDocument.activeElement;
 const disclosure = active?.closest('[data-incident-disclosure]');
 const focusKey = disclosure && panel.contains(disclosure) ? disclosure.dataset.incidentDisclosure : null;
 const focusArea = panel.contains(active) ? active.dataset.incidentScroll : null;
 panel.innerHTML = html;
 panel.incidentMarkup = html;
 for (const detail of panel.querySelectorAll('[data-incident-disclosure]')) {
  detail.open = open.has(detail.dataset.incidentDisclosure);
  if (focusArea || detail.dataset.incidentDisclosure !== focusKey) continue;
  const button = [...detail.querySelectorAll('button:not(:disabled)')].find(el =>
   el.dataset.incidentAction === active.dataset.incidentAction && el.dataset.actorId === active.dataset.actorId &&
   el.dataset.faultAction === active.dataset.faultAction);
  (button || detail.querySelector('summary'))?.focus({ preventScroll: true });
 }
 for (const area of panel.querySelectorAll('[data-incident-scroll]')) {
  area.scrollTop = scroll.get(area.dataset.incidentScroll) || 0;
  if (area.dataset.incidentScroll === focusArea) area.focus({ preventScroll: true });
 }
}
export class IncidentUI{
 constructor({snapshot,send,render,notify}){this.snapshot=snapshot;this.send=send;this.render=render;this.notify=notify;this.busy=false;this.message='';this.draft={name:'실시간 복합 시나리오',target:'',action:'',trigger:'immediate',delay:0,duration:60,timeout:1800,worker_distance_m:20,weather:{rain_mm_h:25,snow_mm_h:0,wind_mps:3.5,fog_m:0,ice:0},steps:[]};try{Object.assign(this.draft,JSON.parse(localStorage.getItem('portsim-incident-draft-v1')||'{}'));}catch{}}
 persist(){try{localStorage.setItem('portsim-incident-draft-v1',JSON.stringify(this.draft));}catch{}}
 view(state,live){
  const actors=(state?.actors||[]).filter(a=>['agv','sts','rmg'].includes(a.type)),d=this.draft;
  const target=actors.find(a=>a.id===d.target)||actors[0],type=target?.type||'agv',actions=incidentActions[type];
  if(target)d.target=target.id;if(!actions.some(([a])=>a===d.action))d.action=actions[0][0];
  const disabled=!live||!state?.incident_control_supported||!!state?.fault||this.busy;
  const input=(key,label,min,max,step=1)=>`<label>${label}<input type="number" data-incident-field="${key}" value="${esc(d[key])}" min="${min}" max="${max}" step="${step}" required></label>`;
  return `<div class="incident-intro"><div><h2>상황을 만들고, 대응부터 해결까지 확인하세요.</h2><p>운송 중 날씨·장비 고장·작업자 진입을 조합합니다. 판정은 실제 엔진 상태로 갱신됩니다.</p></div><span class="live-label">${live?'● LIVE':'○ 최근 기록'}</span></div>${!state?.incident_control_supported?'<p class="note amber">최신 엔진 빌드가 필요합니다. 실행 중인 엔진이 시나리오 제어를 지원하면 입력이 활성화됩니다.</p>':''}${this.weatherPanel(disabled)}<div class="incident-grid"><div class="incident-composer"><section class="panel"><div class="panel-head"><h2>상황 구성</h2><span>최대 12개</span></div><div class="form-body"><fieldset ${disabled?'disabled':''}><label>시나리오 이름<input data-incident-field="name" maxlength="80" value="${esc(d.name)}"></label><div class="incident-presets"><button class="button secondary" data-incident-action="preset">예시: 비 + 작업자 + RMG 고장</button><button class="button secondary" data-incident-action="load">저장 구성 불러오기</button></div><label>대상 장비<select data-incident-field="target">${actors.map(a=>`<option value="${esc(a.id)}" ${a.id===d.target?'selected':''}>${esc(a.name)}</option>`).join('')}</select></label><label>문제 상황<select data-incident-field="action">${actions.map(([a,n])=>`<option value="${a}" ${a===d.action?'selected':''}>${n}</option>`).join('')}</select></label><div class="incident-fields"><label>발생 조건<select data-incident-field="trigger">${Object.entries(triggers).filter(([k])=>k==='immediate'||(type==='agv'?['moving','loaded']:['pickup','placing','release','handover']).includes(k)).map(([k,n])=>`<option value="${k}" ${d.trigger===k?'selected':''}>${n}</option>`).join('')}</select></label>${input('delay','시작 후 지연 (초)',0,3600)}${input('duration','복구 요청까지 (초, 0은 수동)',0,3600)}${input('timeout','단계 제한 시간 (초)',30,14400)}</div>${d.action==='worker_enter'?input('worker_distance_m','작업자 진입 거리 (m)',5,80):''}<p class="note">조건이 충족되지 않으면 대기합니다. 오류 해제 후에도 실제 후속 작업이 완료돼야 검증 통과입니다.${d.action==='misdelivery'?' 오배송은 목적지 지시 불일치를 주입해 인수를 차단하고, 수정 후 기존 예약 목적지로 인계합니다.':d.action==='localization_loss'?' 절대 위치 보정을 끊고 불확실성에 따라 감속·정지합니다. 해제 후 승인된 도로 노드에서 재인식해야 합니다.':d.action==='fms_delay'?' FMS 명령 전달을 2초 지연합니다. 새 명령 도착 전에는 이동을 보류합니다.':d.action==='fms_disconnect'?' FMS 새 명령 수신이 끊기며 기존 예약을 유지하고 제동합니다.':d.action==='road_block'?' 전방 직선 도로에 차단 구역을 만듭니다. 모든 AGV가 해당 구간을 피하거나 대기합니다.':d.action==='fail_reboot'?' 센서 오류를 발생시킵니다. 복구 요청 시 재부팅을 시작하고 관측을 재확인합니다. 복구 요청까지 0초이면 수동으로 재부팅을 요청해야 합니다.':''}</p><button class="button" data-incident-action="add">장비 상황 추가</button><div class="incident-draft"><h3>실행할 상황 · ${d.steps.length}개</h3><ol>${d.steps.map((s,i)=>`<li><span>${esc(actorName(state,s.actor_id))} · ${esc(actionName(s.action))}<small>${esc(triggers[s.trigger])} · 지연 ${num(s.delay)}초${s.weather?` · ${esc(Object.entries(s.weather).map(([k,v])=>weatherFields[k][2]+': '+v).join(' / '))}`:''}</small></span><button class="link" data-incident-action="remove" data-index="${i}" aria-label="${i+1}번째 상황 삭제">삭제</button></li>`).join('')||'<li>상황을 추가하세요. 같은 지연 시간의 단계들은 함께 발생합니다.</li>'}</ol></div><div class="control-actions"><button class="button" data-incident-action="start" ${!d.steps.length?'disabled':''}>조합 실행</button><button class="button secondary" data-incident-action="save">구성 저장</button><button class="link" data-incident-action="clear">구성 비우기</button></div></fieldset><p class="control-message" role="status">${esc(this.message)}</p></div></section></div><section class="panel incident-live"><div class="panel-head"><h2>실시간 대응·검증</h2></div><div class="form-body" data-incident-live>${renderIncidentRuns(state,live)}</div></section></div>`;
 }
 weatherPanel(disabled){const d=this.draft;return `<section class="panel incident-weather-panel"><div class="panel-head"><div><h2>날씨 먼저 설정</h2><p>운송 중에도 적용됩니다. 날씨를 설정한 뒤 아래에서 돌발상황을 구성하세요.</p></div></div><div class="form-body"><fieldset ${disabled?'disabled':''}><div class="incident-fields">${Object.entries(weatherFields).map(([k,[min,max,label]])=>`<label>${label}<input type="number" data-incident-weather="${k}" value="${esc(d.weather[k]??0)}" min="${min}" max="${max}" step="${k==='ice'?.05:k==='wind_mps'?.5:1}" required></label>`).join('')}</div><p class="note">0은 해당 날씨 해제입니다. 강수가 멈춰도 젖음·적설·결빙은 기존 환경 계산에 따라 남고 서서히 변합니다. 날씨 해제는 별도 단계로 추가하세요.</p><div class="control-actions"><button class="button" data-incident-action="weather-add">날씨를 조합에 추가</button><button class="button secondary" data-incident-action="weather-now">날씨 즉시 적용</button></div></fieldset></div></section>`;}
 input(el){const field=el.dataset.incidentField,key=el.dataset.incidentWeather;if(key)this.draft.weather[key]=el.value===''?null:Number(el.value);if(field)this.draft[field]=['delay','duration','timeout','worker_distance_m'].includes(field)?el.value===''?null:Number(el.value):el.value;this.persist();}
 async handle(button){
  const {state,live}=this.snapshot(),d=this.draft,a=button.dataset.incidentAction;
  try{
   if(this.busy)return;
   if(['start','weather-now','add','weather-add'].includes(a)&&!Array.from(document.querySelectorAll('.incident-composer input,.incident-weather-panel input')).every(i=>i.reportValidity()))return;
   const step=weather=>({actor_id:weather?'environment':d.target,action:weather?'set_environment':d.action,trigger:weather?'immediate':d.trigger,delay:Number(d.delay),duration:weather?0:Number(d.duration),timeout:Number(d.timeout),...(weather?{weather:{...d.weather}}:d.action==='worker_enter'?{worker_distance_m:Number(d.worker_distance_m)}:{})});
   if(a==='remove')d.steps.splice(Number(button.dataset.index),1);
   if(a==='clear')d.steps=[];
   if(a==='add'||a==='weather-add'){const next=[...d.steps,step(a==='weather-add')];d.steps=cleanScenario({steps:next},state).steps;}
   if(a==='save'){localStorage.setItem('portsim-incident-saved-v1',JSON.stringify(d));this.message='구성을 브라우저에 저장했습니다.';}
   if(a==='load'){const saved=JSON.parse(localStorage.getItem('portsim-incident-saved-v1')||'null');if(!saved)throw Error('저장된 구성이 없습니다.');Object.assign(d,saved);this.message='저장 구성을 불러왔습니다. 현재 실행의 대상 장비를 확인하세요.';}
   if(a==='preset'){
    const agv=state?.actors?.find(a=>a.type==='agv'&&a.stage===3)||state?.actors?.find(a=>a.type==='agv'),rmg=state?.actors?.find(a=>a.type==='rmg');
    if(!agv||!rmg)throw Error('AGV와 RMG가 있는 실행이 필요합니다.');
    d.name='비 속 작업자 진입 + RMG 고장';d.steps=[{actor_id:'environment',action:'set_environment',trigger:'immediate',delay:0,duration:0,timeout:1800,weather:{rain_mm_h:25}}, {actor_id:agv.id,action:'worker_enter',trigger:'loaded',delay:5,duration:30,timeout:1800,worker_distance_m:20},{actor_id:rmg.id,action:'fail_gantry',trigger:'immediate',delay:20,duration:90,timeout:1800},{actor_id:'environment',action:'set_environment',trigger:'immediate',delay:180,duration:0,timeout:1800,weather:{rain_mm_h:0}}];
   }
   if(['start','weather-now','cancel','recover','replace'].includes(a)){
    if(!live||!state?.incident_control_supported||state?.fault)throw Error('제어 가능한 실시간 엔진 연결이 필요합니다.');
    let command;
    if(a==='start'||a==='weather-now')command={actor_id:'scenario',action:'scenario_start',...cleanScenario({name:a==='weather-now'?'날씨 즉시 적용':d.name,steps:a==='weather-now'?[{...step(true),delay:0}]:d.steps},state)};
    if(a==='cancel')command={actor_id:'scenario',action:'scenario_cancel',scenario_id:button.dataset.run};
    if(a==='replace')command={actor_id:button.dataset.actorId,action:'replace_loaded_agv'};
    if(a==='recover')command={actor_id:button.dataset.actorId,action:'incident_clear',fault_action:button.dataset.faultAction||button.dataset.recovery};
    this.busy=true;this.message='명령 전송 중…';this.render();
    const response=await this.send(command);this.message='엔진 접수 대기 · '+response.id;this.pending={id:response.id,session:state.control_session,started:Date.now()};
   }
  }catch(error){this.message=error.message;this.notify(error.message);}finally{this.busy=false;this.persist();this.render();}
 }
 observe(state){if(!this.pending)return;const p=this.pending,result=state?.command_results?.find(r=>r.id===p.id);if(result||p.session!==state?.control_session||Date.now()-p.started>12000){this.message=result?(result.status==='applied'?'엔진 적용 완료 · 실행 목록에서 진행 상태를 확인하세요.':result.message):'실행 변경 또는 응답 시간 초과 · 실제 상태를 확인하세요.';this.pending=null;}}
}
