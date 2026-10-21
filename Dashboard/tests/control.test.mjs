import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,readdir,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {createDashboardServer} from '../server.mjs';
import {controlPanel,impactModel,impactOverlay,mapPosition,impactFeed} from '../public/control-view.mjs';

async function fixture(t,stale=false){
 const repo=await mkdtemp(path.join(tmpdir(),'portsim-control-')),directory=path.join(repo,'PortSim/Saved/Dashboard');
 await mkdir(directory,{recursive:true});
 const state={schema_version:1,run_id:'test-run',control_session:'a'.repeat(32),generated_at:new Date(Date.now()-(stale?60000:0)).toISOString(),actors:[{id:'sts',type:'sts'},{id:'rmg',type:'rmg'},{id:'agv',type:'agv'},{id:'cargo',type:'container'}]};
 await writeFile(path.join(directory,'state.json'),JSON.stringify(state));
 const server=createDashboardServer({repo});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const url=`http://127.0.0.1:${server.address().port}`;
 t.after(async()=>{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));if(!path.resolve(repo).startsWith(path.join(tmpdir(),'portsim-control-')))throw Error('Unexpected fixture path');await rm(repo,{recursive:true,force:true});});
 const post=(body={},origin=url,contentType='application/json')=>fetch(url+'/api/equipment-command',{method:'POST',headers:{Origin:origin,'Content-Type':contentType},body:JSON.stringify({run_id:state.run_id,control_session:state.control_session,actor_id:'sts',action:'fail',...body})});
 return {state,directory,post,url};
}
test('HTTP publishes only allowed commands atomically, serializes duplicate submissions',async t=>{
 const f=await fixture(t);
 assert.equal((await f.post({},'http://untrusted.invalid')).status,403);
 assert.equal((await f.post({},f.url,'text/plain')).status,415);
 assert.equal((await f.post({actor_id:'cargo'})).status,400);
 assert.equal((await f.post({run_id:'old-run'})).status,409);
 assert.equal((await f.post({action:'console'})).status,400);
 const responses=await Promise.all([f.post({ignored:'not published'}),f.post()]);
 assert.deepEqual(responses.map(r=>r.status).sort(),[202,409]);
 const files=await readdir(path.join(f.directory,'commands'));assert.equal(files.length,1);assert.match(files[0],/^[a-f0-9]{32}\.json$/);
 const command=JSON.parse(await readFile(path.join(f.directory,'commands',files[0]),'utf8'));
 assert.equal(command.actor_id,'sts');assert.equal(command.action,'fail');assert.equal(command.ignored,undefined);
 assert.equal((await f.post({actor_id:'rmg'})).status,202);assert.equal((await f.post({actor_id:'agv'})).status,202);
});
test('recorded snapshots cannot receive queued controls',async t=>{
 const f=await fixture(t,true);assert.equal((await f.post()).status,409);
});
test('controls require live engine, distinguish queue acknowledgement from applied state',()=>{
 const actor={id:'sts',type:'sts',name:'<STS>'},state={control_session:'a'.repeat(32)};
 assert.match(controlPanel(actor,state,false,null),/data-command="fail" disabled/);
 assert.match(controlPanel(actor,state,true,{id:'pending'},'엔진 응답 대기'),/data-command="fail" disabled/);
 assert.match(controlPanel({...actor,fault_scope:'equipment'},state,true,null),/data-command="repair" >/);
 assert.match(controlPanel(actor,state,true,null),/&lt;STS&gt;/);
 assert.match(controlPanel(actor,{...state,fault:'safety'},true,null),/전체 안전 정지/);
});
test('overview follows actual gantry position and traces real affected routes and indirect queues',()=>{
 const state={actors:[{id:'rmg',type:'rmg',name:'RMG',position_m:[1,2,0],gantry_position_m:[30,40,0]},
  {id:'agv',type:'agv',vehicle_id:100,rmg:'rmg',position_m:[2,3,0],route:[{position_m:[5,6,0]}]},
  {id:'queue',type:'agv',vehicle_id:101,fms_blocking_agv:100,position_m:[8,9,0]},
  {id:'unrelated',type:'agv',position_m:[20,30,0]}],recovery_events:[{sequence:2,kind:'yard_reassigned',cause_actor:'failed',from_actor:'failed',to_actor:'rmg',cargo:'cargo',from_position_m:[1,2,0],to_position_m:[3,4,0],simulation_seconds:50}]};
 assert.deepEqual(mapPosition(state.actors[0]),[30,40,0]);
 const focus={cause:'failed',after:1},model=impactModel(state,focus);
 assert(model.related.has('queue'));assert(!model.related.has('unrelated'));
 const svg=impactOverlay(state,focus,v=>v,v=>v,null);
 assert.match(svg,/plan-connection/);assert.match(svg,/impact-route/);assert.doesNotMatch(svg,/motion-trail/);
 assert.match(impactFeed(state,focus,true),/야드 목적지 재배정/);
 assert.equal(impactModel(state,{cause:'failed',after:3}).events.length,0);
});
