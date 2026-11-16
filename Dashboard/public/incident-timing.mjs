import {formatDuration} from './metrics.mjs';
const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const stamp=n=>Number.isFinite(n)&&n>=0;
export const baselineStorageKey='portsim-incident-baselines-v1';
export function createIncidentBaseline(state){
 const t=state?.incident_timing;
 if(t?.schema_version!==1||!t.comparison_key||!t.reference_eligible)throw Error('오류·환경 변경이 없는 정상 실행 기록만 저장할 수 있습니다.');
 const cargo=(t.cargo||[]).filter(c=>stamp(c.completed_at)).map(({id,completed_at})=>({id,completed_at}));
 if(!cargo.length)throw Error('야드 적재가 완료된 컨테이너가 있어야 기준을 저장할 수 있습니다.');
 return {schema_version:1,comparison_key:t.comparison_key,run_id:state.run_id,saved_at:new Date().toISOString(),total_count:t.cargo.length,environment_seed:t.environment_seed,cargo};
}
export function compareIncidentTiming(state,reference){
 const t=state?.incident_timing;
 if(!reference)return {status:'missing',reason:'비교 기준 없음 · 정상 실행에서 기준을 저장하세요.'};
 if(t?.schema_version!==1||reference.schema_version!==1||!t.comparison_key||t.comparison_key!==reference.comparison_key)
  return {status:'mismatch',reason:'비교 조건 불일치 · 같은 코드·설정·초기 배치·환경 시드가 필요합니다.'};
 if(state.run_id===reference.run_id)return {status:'same_run',reason:'정상 기준을 저장했습니다. 같은 조건의 다음 실행에서 비교합니다.'};
 const actual=new Map((t.cargo||[]).map(c=>[c.id,c]));
 const unique=new Set((reference.cargo||[]).map(c=>c.id));
 if(!reference.cargo?.length||unique.size!==reference.cargo.length||reference.cargo.some(c=>!stamp(c.completed_at)||!actual.has(c.id)))
  return {status:'mismatch',reason:'기준 컨테이너 집합이 현재 실행과 일치하지 않습니다.'};
 const rows=reference.cargo.map(c=>({id:c.id,baseline:c.completed_at,actual:actual.get(c.id).completed_at}));
 const completed=rows.filter(c=>stamp(c.actual));
 const deltas=completed.map(c=>({...c,delay:c.actual-c.baseline}));
 const baseEnd=Math.max(...rows.map(c=>c.baseline)),done=completed.length===rows.length;
 const actualEnd=done?Math.max(...completed.map(c=>c.actual)):null;
 return {status:done?'complete':'pending',rows:deltas,count:rows.length,completed:completed.length,
  baselineEnd:baseEnd,actualEnd,delay:done?actualEnd-baseEnd:null,
  lowerBound:!done&&stamp(t.clock_seconds)&&t.clock_seconds>baseEnd?t.clock_seconds-baseEnd:null,
  percent:done&&baseEnd>0?(actualEnd-baseEnd)/baseEnd*100:null,
  fullScope:rows.length===t.cargo.length};
}
export function stepTiming(step,clock){
 const start=step.measurement_applied_at;
 if(!stamp(start)||!stamp(clock))return {applied:false};
 const span=end=>stamp(end)&&end>=start?end-start:null;
 return {applied:true,fault:span(step.fault_cleared_at)??Math.max(0,clock-start),
  faultFinal:span(step.fault_cleared_at)!==null,resume:span(step.work_resumed_at),completion:span(step.work_completed_at)};
}
const signed=n=>Number.isFinite(n)?`${n<0?'−':n>0?'+':''}${formatDuration(Math.abs(n))}`:'—';
const duration=n=>Number.isFinite(n)?formatDuration(n):'—';
export function renderStepTiming(step,state,reference){
 const timing=stepTiming(step,state?.incident_timing?.clock_seconds);
 if(!timing.applied||step.action==='set_environment')return '';
 const comparison=compareIncidentTiming(state,reference);
 const ids=new Set(step.affected_cargo||[]);
 const matched=new Set((state.incident_timing?.cargo||[]).filter(c=>ids.has(c.actor_id)).map(c=>c.id));
 const delays=(comparison.rows||[]).filter(c=>matched.has(c.id));
 const max=delays.length?Math.max(...delays.map(c=>c.delay)):null;
 const actors=new Set(step.affected_actors||[]);
 return `<dl class="incident-step-times"><div><dt>고장 지속${timing.faultFinal?'':' · 진행 중'}</dt><dd>${duration(timing.fault)}</dd></div><div><dt>작업 재개까지</dt><dd>${timing.resume===null?'재개 미확인':duration(timing.resume)}</dd></div><div><dt>후속 작업 완료까지</dt><dd>${timing.completion===null?'완료 미확인':duration(timing.completion)}</dd></div><div><dt>영향 장비 / 화물</dt><dd>${actors.size}대 / ${ids.size}개</dd></div><div><dt>영향 화물 최대 추가 지연</dt><dd>${max===null?'비교 가능한 완료 기록 없음':signed(max)}</dd></div></dl>`;
}
export function renderIncidentTiming(state,reference){
 const t=state?.incident_timing,comparison=compareIncidentTiming(state,reference);
 const measured=!!t&&t.schema_version===1;
 const canSave=measured&&t.reference_eligible&&(t.cargo||[]).some(c=>stamp(c.completed_at));
 const steps=(state?.incident_scenarios||[]).flatMap(r=>r.steps||[]).filter(s=>stamp(s.measurement_applied_at)&&s.action!=='set_environment');
 const affectedActors=new Set(steps.flatMap(s=>s.affected_actors||[])),affectedCargo=new Set(steps.flatMap(s=>s.affected_cargo||[]));
 const valid=['complete','pending'].includes(comparison.status);
 const label=valid?(comparison.fullScope?'전체 물량 추가시간':`기준 물량 ${comparison.count}개 추가시간`):'정상 대비 추가시간';
 const result=comparison.status==='complete'?signed(comparison.delay):comparison.lowerBound!==null&&comparison.lowerBound!==undefined?`${signed(comparison.lowerBound)} 이상`:'산정 대기';
 return `<div class="incident-timing" data-timing-schema="${measured?1:0}"><div class="incident-timing-head"><div><h3>돌발상황 시간 기록</h3><p>오류·기상 정지는 포함하고 수동 일시정지는 제외한 시뮬레이션 시간입니다.</p></div><div class="control-actions"><button class="button secondary" data-incident-action="save-reference" ${canSave?'':'disabled'}>현재 정상 기록을 기준으로 저장</button><button class="link" data-incident-action="clear-reference" ${reference?'':'disabled'}>비교 기준 삭제</button><button class="button secondary" data-incident-action="export-timing" ${measured?'':'disabled'}>시간 기록 내려받기</button></div></div>
 <div class="incident-timing-cards"><div><small>측정 경과 / 전체 정지</small><strong>${duration(t?.clock_seconds)} / ${duration(t?.halt_seconds)}</strong></div><div><small>누적 영향 장비 / 화물</small><strong>${affectedActors.size}대 / ${affectedCargo.size}개</strong></div><div><small>${esc(label)}</small><strong>${result}</strong></div><div><small>작업시간 증가율</small><strong>${Number.isFinite(comparison.percent)?`${comparison.percent>0?'+':''}${comparison.percent.toFixed(1)}%`:'산정 대기'}</strong></div></div>
 <p class="note">${!measured?'시간 기록을 지원하는 엔진으로 다시 실행하세요.':valid?`같은 컨테이너 ${comparison.completed}/${comparison.count}개 완료 비교 · ${comparison.fullScope?'전체 물량':'저장한 부분 물량'} 기준 · ${comparison.status==='pending'?'미완료 작업의 최종 지연은 산정하지 않습니다.':'음수는 정상 기준보다 빨라진 결과입니다.'}`:esc(comparison.reason)}${reference?`<br>기준 실행 ${esc(reference.run_id)} · ${reference.cargo?.length||0}개 · 환경 시드 ${esc(reference.environment_seed??'—')}`:''}</p>
 <p class="note">정상 운반을 먼저 실행해 기준을 저장한 후 같은 초기 조건으로 다시 실행하세요. 기준은 이 브라우저에 저장됩니다. 오류 조합은 하나의 실행 전체로 비교하며 단계별 지연을 더하지 않습니다. 영향 수는 재배정·실제 대기 연결에서 확인한 누적 범위이며, 단계별 차이는 해당 오류만의 인과 효과를 분리한 값이 아닙니다.</p></div>`;
}
