import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,readdir,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {createDashboardServer} from '../server.mjs';
import {emergencyControls,emergencyDetails,renderEmergencyReport,workerIntrusionControls} from '../public/agv-emergency-view.mjs';
import {impactModel,impactFeed,workerImpactDetails} from '../public/control-view.mjs';

test('manual worker entry uses current conditions and live engine impacts clear independently of history',()=>{
 const a={id:'agv',name:'AGV-100',type:'agv',stage:6,speed_mps:5,emergency_test:{active:false,outcome:'idle'}};
 assert.match(workerIntrusionControls(a,{worker_intrusion_supported:true},false,35),/value="35"/);
 assert.match(workerIntrusionControls(a,{worker_intrusion_supported:true},false),/data-command="worker_enter" >/);
 assert.match(workerIntrusionControls(a,{},false),/data-command="worker_enter" disabled/);
 a.vehicle_id=100;
 const s={actors:[a,{id:'queue',name:'<AGV>',type:'agv',speed_mps:0},{id:'sts',name:'STS-01',type:'sts'},
  {id:'stale',type:'agv',speed_mps:5,fms_blocking_agv:100,fms_hold:false}],worker_impacts:[
  {cause_actor:'agv',to_actor:'queue',from_actor:'agv',reason:'fms_queue',duration_seconds:12},
  {cause_actor:'agv',to_actor:'sts',from_actor:'queue',reason:'sts_handover_wait',duration_seconds:3}],
  recovery_events:[{kind:'worker_entered',cause_actor:'agv',to_actor:'agv',from_actor:'agv',sequence:1,simulation_seconds:1}]};
 a.emergency_test.active=true;
 assert(impactModel(s).related.has('sts'));assert(impactModel(s).related.has('queue'));
 assert(!impactModel(s).related.has('stale'));
 assert.match(impactFeed(s,null,true),/FMS 도로 예약 대기/);
 assert.match(impactFeed(s,null,true),/&lt;AGV&gt;/);
 assert.match(workerImpactDetails(s,s.actors[2]),/직접 선행 장비 &lt;AGV&gt;/);
 a.emergency_test.active=false;
 assert(!impactModel(s).related.has('sts'));assert(!impactModel(s).related.has('queue'));
 assert.equal(workerImpactDetails(s,s.actors[2]),'');assert.equal(s.recovery_events.length,1);
});

test('live controls require supported telemetry and moving cargo; unsafe outcomes cannot be released',()=>{
 const a={id:'agv',type:'agv',stage:3,speed_mps:6,emergency_test:{active:false,outcome:'idle'}};
 assert.match(emergencyControls(a,false),/data-command="test_wet_worker" >/);
 assert.match(emergencyControls({...a,emergency_test:null},false),/data-command="test_wet_worker" disabled/);
 for(const b of [{...a,speed_mps:0},{...a,fms_hold:true},{...a,controlled_stop:true},{...a,stage:1}])
  assert.match(emergencyControls(b,false),/data-command="test_wet_worker" disabled/);
 assert.match(emergencyControls(a,true),/data-command="test_wet_worker" disabled/);
 a.emergency_test={active:true,stopped:true,peak_cargo_shift_m:0};
 assert.match(emergencyControls(a,false),/data-command="clear_worker" >/);
 a.emergency_test.peak_cargo_shift_m=.3;
 assert.match(emergencyControls(a,false),/data-command="clear_worker" disabled/);
 const model=impactModel({actors:[a,{id:'queue',type:'agv',fms_blocking_agv:100}],recovery_events:[]});
 assert(model.related.has('agv'));a.emergency_test.active=false;
 assert(!impactModel({actors:[a]}).related.has('agv'));
});

test('reports distinguish expected hazards from safety, retain diagnostics and escape external input',()=>{
 assert.match(renderEmergencyReport(null),/보고서가 없습니다/);
 const report={valid:true,generated_at:'now',cases:[{valid:true,input:{id:'<script>',label:'<img>'},cargo_secured:false,
  outcome:'cargo_shift_limit',braking_distance_m:.5,minimum_clearance_m:5,peak_cargo_shift_m:.301,
  samples:[{time_s:1,speed_mps:5,gap_m:5,detected:true,brake:true,cargo_shift_m:.301}]}]};
 const html=renderEmergencyReport(report);
 assert.match(html,/화물 이동 한계 초과/);assert.match(html,/안전 정지를 의미하지 않습니다/);
 assert.match(html,/&lt;img&gt;/);assert.doesNotMatch(html,/<img>|<script>|undefined|NaN|Infinity/);
 assert.match(emergencyDetails({emergency_test:{outcome:'physical_braking',active:true,cargo_secured:true}}),/타이어 물리 급제동 중/);
 assert.doesNotMatch(emergencyDetails({emergency_test:{outcome:'worker_contact'}}),/NaN|undefined/);
});

test('HTTP publishes only whitelisted emergency commands and serves the separate integrated report',async t=>{
 const repo=await mkdtemp(path.join(tmpdir(),'portsim-emergency-')),dir=path.join(repo,'PortSim/Saved/Dashboard');
 await mkdir(dir,{recursive:true});await mkdir(path.join(repo,'PortSim/Saved/Tests'),{recursive:true});
 const state={schema_version:1,run_id:'test',control_session:'a'.repeat(32),generated_at:new Date().toISOString(),actors:[{id:'agv',type:'agv'},{id:'sts',type:'sts'}]};
 await writeFile(path.join(dir,'state.json'),JSON.stringify(state));
 const server=createDashboardServer({repo});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 t.after(async()=>{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
  if(!path.resolve(repo).startsWith(path.join(tmpdir(),'portsim-emergency-')))throw Error('Unexpected fixture path');await rm(repo,{recursive:true,force:true});});
 const base=`http://127.0.0.1:${server.address().port}`;
 const post=(actor_id,action,extra={})=>fetch(base+'/api/equipment-command',{method:'POST',headers:{Origin:base,'Content-Type':'application/json'},
  body:JSON.stringify({run_id:state.run_id,control_session:state.control_session,actor_id,action,brake_demand:999,...extra})});
 assert.equal((await post('sts','test_wet_worker')).status,400);
 assert.equal((await post('agv','set_any_parameter')).status,400);
 // Web control always preserves current weather and cargo conditions.
 assert.equal((await post('agv','test_wet_worker')).status,400);
 assert.equal((await post('agv','test_wet_unsecured')).status,400);
 for(const distance of [undefined,null,'20',4,81])assert.equal((await post('agv','worker_enter',{worker_distance_m:distance})).status,400);
 assert.equal((await post('sts','worker_enter',{worker_distance_m:20})).status,400);
 assert.equal((await post('agv','worker_enter',{worker_distance_m:25,wetness:1})).status,202);
 const [entryFile]=await readdir(path.join(dir,'commands'));
 const entry=JSON.parse(await readFile(path.join(dir,'commands',entryFile),'utf8'));
 assert.equal(entry.action,'worker_enter');assert.equal(entry.worker_distance_m,25);assert.equal(entry.wetness,undefined);assert.equal(entry.brake_demand,undefined);
 assert.deepEqual(await (await fetch(base+'/api/agv-emergencies')).json(),{report:null});
 await writeFile(path.join(repo,'PortSim/Saved/Tests/AGVEmergency_Report.json'),JSON.stringify({valid:true,cases:[]}));
 assert.equal((await (await fetch(base+'/api/agv-emergencies')).json()).report.valid,true);
 assert.equal((await fetch(base+'/agv-emergency-view.mjs')).status,200);
});
