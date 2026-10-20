// Run beside TestSite.ps1 in the FINAL validation phase; this controls that live engine.
import {writeFile} from 'node:fs/promises';
import {setTimeout as wait} from 'node:timers/promises';
const origin='http://127.0.0.1:4173';
const start=Date.now();let run,session,failed=false,repaired=false,agvDone=false,agv,repairedAt=0;
const summary={commands:[],reassignments:0,otherMovementObserved:false};
async function snapshot(){const r=await fetch(origin+'/api/state');if(!r.ok)throw Error('State HTTP failure');return r.json();}
async function command(state,actor,action){
 const r=await fetch(origin+'/api/equipment-command',{method:'POST',headers:{Origin:origin,'Content-Type':'application/json'},body:JSON.stringify({run_id:state.run_id,control_session:state.control_session,actor_id:actor.id,action})});
 const result=await r.json();if(r.status!==202)throw Error(`${actor.name}: ${result.message}`);
 for(let i=0;i<12;i++){
  await wait(1000);const {state:s,live}=await snapshot();if(!live)continue;
  if(s.control_session!==session)throw Error('Engine session changed during command');
  const ack=s.command_results?.find(c=>c.id===result.id);
  if(ack){if(ack.status!=='applied')throw Error(ack.message);summary.commands.push({actor:actor.name,action,id:result.id});return s;}
 }
 throw Error('Engine command acknowledgement timed out');
}
while(Date.now()-start<30*60*1000){
 const {state:s,live}=await snapshot();
 if(!live||!s?.control_session||Date.parse(s.generated_at)<start){await wait(500);continue;}
 run??=s.run_id;session??=s.control_session;
 if(s.run_id!==run||s.control_session!==session){if(failed)throw Error('Controlled run reset unexpectedly');run=s.run_id;session=s.control_session;}
 if(s.fault)throw Error(s.fault);
 const find=name=>s.actors.find(a=>a.name===name);
 if(!failed&&s.simulation_seconds>=60){
  await command(s,find('STS-02'),'fail');await command(s,find('RMG-12'),'fail');failed=true;
 }
 if(failed&&!agvDone&&s.simulation_seconds>=300){
  const v=s.actors.find(a=>a.type==='agv'&&a.speed_mps>0&&a.on_deck_count>0&&a.stage===3);
  if(v){
   const stopped=await command(s,v,'fail'),fixed=stopped.actors.find(a=>a.id===v.id),others=stopped.actors.filter(a=>a.type==='agv'&&a.id!==v.id&&a.speed_mps>0);
   if(!fixed.drive_failure||fixed.speed_mps!==0)throw Error('AGV did not stop');
   await wait(2200);const {state:later}=await snapshot(),still=later.actors.find(a=>a.id===v.id);
   if(JSON.stringify(still.position_m)!==JSON.stringify(fixed.position_m)||still.cargo_actor!==fixed.cargo_actor)throw Error('Failed AGV moved or lost its load');
   summary.otherMovementObserved=others.some(other=>JSON.stringify(later.actors.find(a=>a.id===other.id).position_m)!==JSON.stringify(other.position_m));
   await command(later,still,'repair');agvDone=true;agv=v.name;
  }
 }
 if(failed&&!repaired&&s.simulation_seconds>=960){
  summary.reassignments=s.yard_reassignments;
  if(summary.reassignments<1||!s.recovery_events.some(e=>e.kind==='yard_reassigned'))throw Error('Reassignment events not exported');
  await command(s,find('STS-02'),'repair');await command(s,find('RMG-12'),'repair');repaired=true;repairedAt=s.simulation_seconds;
 }
 if(repaired&&agvDone&&s.simulation_seconds>repairedAt+60){
  if(!summary.otherMovementObserved)throw Error('No unrelated AGV movement observed');
  summary.run=run;summary.agv=agv;summary.acknowledged=true;
  await writeFile(new URL('../../PortSim/Saved/Tests/WebControlIntegration.json',import.meta.url),JSON.stringify(summary,null,2));
  console.log('PASS live web → engine fault/repair acknowledgements; loaded AGV held; other AGV moved; yard reassignment events exported');break;
 }
 await wait(500);
}
if(!summary.acknowledged)throw Error('Live control scenario incomplete');
