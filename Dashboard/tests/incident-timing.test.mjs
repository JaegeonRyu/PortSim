import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createIncidentBaseline,compareIncidentTiming,stepTiming,renderIncidentTiming,renderStepTiming} from '../public/incident-timing.mjs';
import {IncidentUI,renderIncidentRuns} from '../public/incident-view.mjs';
const state=(times,run='fault')=>({run_id:run,incident_timing:{schema_version:1,comparison_key:'same-initial-state',clock_seconds:400,halt_seconds:20,reference_eligible:run==='normal',cargo:times.map((completed_at,i)=>({id:`C${i}`,actor_id:`actor${i}`,completed_at}))}});
test('baseline accepts measured normal completions and rejects injected or absent records',()=>{
 assert.throws(()=>createIncidentBaseline(state([100])),/정상/);
 assert.throws(()=>createIncidentBaseline(state([-1],'normal')),/완료/);
 assert.equal(createIncidentBaseline(state([100,-1],'normal')).cargo.length,1);
});
test('fixed cargo cohort comparison includes fleet delays without summing parallel waits',()=>{
 const reference=createIncidentBaseline(state([100,200],'normal'));
 const result=compareIncidentTiming(state([160,230]),reference);
 assert.equal(result.delay,30);assert.equal(result.percent,15);assert.equal(result.count,2);
 assert.deepEqual(result.rows.map(c=>c.delay),[60,30]); // 90 vehicle-seconds is not 30 elapsed seconds.
 assert.equal(compareIncidentTiming(state([80,180]),reference).delay,-20);
});
test('incomplete, mismatched and same-run results never fabricate final delay',()=>{
 const reference=createIncidentBaseline(state([100,200],'normal'));
 assert.equal(compareIncidentTiming(state([100],'normal'),reference).status,'same_run');
 assert.equal(compareIncidentTiming(state([100,200]),null).status,'missing');
 assert.equal(compareIncidentTiming({...state([100,200]),incident_timing:{...state([100,200]).incident_timing,comparison_key:'different'}},reference).status,'mismatch');
 const pending=compareIncidentTiming(state([160,-1]),reference);
 assert.equal(pending.delay,null);assert.equal(pending.lowerBound,200);assert.equal(pending.percent,null);
 assert.equal(compareIncidentTiming(state([160,-1]),{...reference,cargo:[...reference.cargo,{id:'C99',completed_at:100}]}).status,'mismatch');
});
test('partial baseline keeps the same IDs even when actual completion order changes',()=>{
 const reference=createIncidentBaseline(state([100,-1,300],'normal'));
 const result=compareIncidentTiming(state([160,150,320]),reference);
 assert.equal(result.count,2);assert.equal(result.delay,20);assert.equal(result.fullScope,false);
 assert.match(renderIncidentTiming(state([160,150,320]),reference),/기준 물량 2개/);
});
test('fault clearing, resumed work and delivery remain separate; timeout still grows',()=>{
 const step={measurement_applied_at:10,fault_cleared_at:30,work_resumed_at:40,work_completed_at:90};
 assert.deepEqual(stepTiming(step,100),{applied:true,fault:20,faultFinal:true,resume:30,completion:80});
 assert.equal(stepTiming({...step,state:'timeout',fault_cleared_at:-1},100).fault,90);
 assert.equal(stepTiming({applied_at:5},100).applied,false);
});
test('timing records distinguish missing references, escape labels and keep cumulative counts unique',()=>{
 const s=state([160,230]);s.incident_scenarios=[{steps:[{measurement_applied_at:10,affected_actors:['a','a','b'],affected_cargo:['actor0']},{measurement_applied_at:10,affected_actors:['b'],affected_cargo:['actor0']}]}];
 assert.match(renderIncidentTiming(s,null),/2대 \/ 1개/);assert.match(renderIncidentTiming(s,null),/비교 기준 없음/);
 const r=createIncidentBaseline(state([100,200],'normal'));r.run_id='<script>';
 assert.match(renderIncidentTiming(s,r),/&lt;script&gt;/);assert.doesNotMatch(renderIncidentTiming(s,r),/<script>/);
 assert.match(renderStepTiming({action:'fail',measurement_applied_at:10,fault_cleared_at:-1,affected_cargo:['actor0'],affected_actors:['a']},s,r),/고장 지속 · 진행 중/);
});
test('old savings page and vessel shortcut are removed; records appear after overview',async()=>{
 const [app,index,server]=await Promise.all(['../public/app.mjs','../public/index.html','../server.mjs'].map(p=>readFile(new URL(p,import.meta.url),'utf8')));
 assert.doesNotMatch(app,/performancePage|data-performance|시간 절감 분석/);assert.doesNotMatch(index,/data-view="performance"/);
 assert.match(server,/\/incident-timing\.mjs/);
 assert(app.indexOf('data-scenario-map>${map()+impactFeed(state,null,live)}</div>`+incidentUI.records')>=0);
 const ui=new IncidentUI({snapshot:()=>({}),render:()=>{},notify:()=>{},send:()=>{}});
 assert.match(ui.records(state([100]),false),/data-incident-records/);
 const s={...state([100]),incident_scenarios:[{id:'done',name:'finished',steps:[{action:'fail',state:'verified'}]}]};
 assert.doesNotMatch(renderIncidentRuns(s,true,false),/finished/);assert.match(renderIncidentRuns(s,true),/finished/);
});
