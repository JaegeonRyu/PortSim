// Run against an actual test engine, after the implementation is complete.
import {writeFile} from 'node:fs/promises';
import {setTimeout as wait} from 'node:timers/promises';
const base=process.env.PORTSIM_DASHBOARD_URL||'http://127.0.0.1:4173',started=Date.now();
const output=new URL('../../PortSim/Saved/Tests/WorkerIntrusion_WebIntegration.json',import.meta.url);
const summary={commands:[],rejections:[],affected:[]};
let session,vehicle,lastTry=0,stoppedAt=0,heldPosition,clearedAt=0;
async function snapshot(){return (await fetch(base+'/api/state')).json();}
async function command(s,a,action){
 const response=await fetch(base+'/api/equipment-command',{method:'POST',headers:{Origin:base,'Content-Type':'application/json'},
  body:JSON.stringify({run_id:s.run_id,control_session:s.control_session,actor_id:a.id,action,...(action==='worker_enter'?{worker_distance_m:20}:{})})});
 const result=await response.json();if(response.status!==202)throw Error(result.message);
 for(let j=0;j<50;j++){
  await wait(250);const {state:p}=await snapshot();if(p.control_session!==session)throw Error('Engine session changed');
  const ack=p.command_results?.find(c=>c.id===result.id);if(ack){summary.commands.push({actor:a.name,action,...ack});return ack;}
 }
 throw Error('Native command acknowledgement missing');
}
try{
 while(Date.now()-started<10*60*1000){
  const {state:s,live}=await snapshot();if(!live||Date.parse(s?.generated_at)<started){await wait(250);continue;}
  if(session!==s.control_session){if(vehicle)throw Error('Controlled run changed');session=s.control_session;}
  if(s.fault)throw Error(s.fault);
  if(!vehicle&&Date.now()-lastTry>800){
   // Empty STS-bound AGVs exercise the new capability and a real crane dependency.
   const a=s.actors.find(v=>v.type==='agv'&&v.stage===6&&v.payload_kg===0&&v.speed_mps>=2&&!v.fms_hold&&!v.controlled_stop&&
    !v.emergency_test?.active&&Math.abs(v.agv_physics?.yaw_rate_rad_s||0)<.01&&v.route_remaining_m>60);
   if(a){lastTry=Date.now();const ack=await command(s,a,'worker_enter');if(ack.status==='applied'){vehicle=a.id;summary.vehicle=a.name;summary.run=s.run_id;console.log(`WORKER_ENTERED ${a.name}`);}
    else summary.rejections.push(ack.message);}
  }
  if(vehicle){
   const a=s.actors.find(v=>v.id===vehicle),e=a.emergency_test;
   if(e.uses_test_overrides||e.scenario_kind!=='worker_intrusion'||a.payload_kg!==0)throw Error('Manual entry changed scenario conditions');
   if(e.finished&&!e.stopped)throw Error(`Expected safe stop: ${e.outcome}`);
   if(e.stopped&&!stoppedAt){stoppedAt=Date.now();heldPosition=a.position_m;summary.result=e;}
   if(stoppedAt&&!clearedAt){
    const impacts=(s.worker_impacts||[]).filter(i=>i.cause_actor===vehicle&&i.to_actor!==vehicle);
    for(const i of impacts)if(!summary.affected.some(old=>old.to_actor===i.to_actor&&old.reason===i.reason))summary.affected.push(i);
    if(impacts.length){
     await writeFile(new URL('../../PortSim/Saved/Tests/WorkerIntrusion_RuntimeActive.json',import.meta.url),JSON.stringify(s,null,2));
     console.log(`WORKER_STOPPED ${summary.vehicle}; ${impacts.length} actual downstream dependencies`);
     const hold=Number(process.env.PORTSIM_WORKER_PROOF_HOLD_MS||0);
     if(Number.isFinite(hold)&&hold>0)await wait(Math.min(hold,60000));
     const ack=await command(s,a,'clear_worker');if(ack.status!=='applied')throw Error(ack.message);clearedAt=Date.now();
    }else if(Date.now()-stoppedAt>20000)throw Error('No actual downstream dependency observed');
   }
   if(clearedAt&&!e.active&&a.speed_mps>.5&&Math.hypot(a.position_m[0]-heldPosition[0],a.position_m[1]-heldPosition[1])>1){
    if((s.worker_impacts||[]).some(i=>i.cause_actor===vehicle))throw Error('Current impacts survived worker removal');
    if(!s.recovery_events?.some(i=>i.kind==='worker_impact_cleared'&&i.cause_actor===vehicle))throw Error('Clearance history missing');
    summary.valid=true;await writeFile(output,JSON.stringify(summary,null,2));
    await writeFile(new URL('../../PortSim/Saved/Tests/WorkerIntrusion_RuntimeReleased.json',import.meta.url),JSON.stringify(s,null,2));
    console.log(`PASS manual worker → empty ${summary.vehicle}: physical stop, ${summary.affected.length} real dependencies, clearance history, resumed motion`);process.exit(0);
   }
  }
  await wait(250);
 }
 throw Error('Manual worker integration timed out');
}catch(error){summary.valid=false;summary.error=error.message;await writeFile(output,JSON.stringify(summary,null,2));throw error;}
