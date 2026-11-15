// Explicit live integration runner. Start a disposable PortSim site test first.
import {writeFile} from 'node:fs/promises';
const extended=process.argv.includes('--extended');
const base=process.argv[2]||'http://127.0.0.1:4184',started=Date.now(),deadline=started+(extended?600000:240000);
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let run,scenario,auxScenario,auxSteps,vehicle,cargo,initialOthers,lastLive=0,otherMovement=false,responseObserved=false;
while(Date.now()<deadline){
 const {state:s,live}=await (await fetch(base+'/api/state')).json();
 if(!live||!s?.incident_control_supported||Date.parse(s.generated_at)<started){if(lastLive&&Date.now()-lastLive>15000)throw Error('Controlled engine stopped publishing; inspect its latest log');await sleep(150);continue;}
 lastLive=Date.now();
 if(!scenario)run=s.run_id;else if(run!==s.run_id)throw Error('Run changed during integration');if(s.fault)throw Error(s.fault);
 if(!scenario){
  const v=s.actors.find(a=>a.type==='agv'&&a.stage===3&&a.on_deck_count>0&&a.rmg&&a.speed_mps>.1);
  if(!v){await sleep(150);continue;}
  const other=s.actors.find(a=>a.type==='agv'&&a.id!==v.id&&a.stage===3&&a.on_deck_count>0);
  if(extended&&!other){await sleep(150);continue;}
  vehicle=v.id;cargo=v.cargo_actor;initialOthers=new Map(s.actors.filter(a=>a.type==='agv'&&a.id!==vehicle).map(a=>[a.id,JSON.stringify(a.position_m)]));
  const steps=extended?[
   {actor_id:'environment',action:'set_environment',weather:{rain_mm_h:10},duration:0},
   {actor_id:vehicle,action:'localization_loss',duration:12},
   {actor_id:vehicle,action:'fms_disconnect',duration:5},
   {actor_id:vehicle,action:'fms_delay',duration:25},
   {actor_id:vehicle,action:'road_block',trigger:'moving',duration:35},
   {actor_id:v.rmg,action:'slot_occupied',trigger:'placing',duration:8},
   {actor_id:v.rmg,action:'height_mismatch',trigger:'placing',duration:8},
   {actor_id:v.rmg,action:'fail_release',trigger:'release',duration:12},
   {actor_id:v.sts,action:'agv_unavailable',trigger:'handover',duration:8},
   {actor_id:v.sts,action:'fail_hoist',trigger:'placing',duration:12},
   {actor_id:v.sts,action:'fail_release',trigger:'release',duration:15}
  ]:[
   {actor_id:'environment',action:'set_environment',weather:{rain_mm_h:10},delay:0,duration:0,timeout:1800},
   {actor_id:vehicle,action:'fail_reboot',delay:0,duration:5,timeout:1800},
   {actor_id:v.rmg,action:'fail_placement',trigger:'placing',delay:0,duration:12,timeout:1800}
  ];
  const response=await fetch(base+'/api/equipment-command',{method:'POST',headers:{Origin:base,'Content-Type':'application/json'},body:JSON.stringify({run_id:s.run_id,control_session:s.control_session,actor_id:'scenario',action:'scenario_start',name:extended?'Live extended incidents':'Live integration · rain + AGV sensor + RMG placement',steps})});
  const result=await response.json();if(!response.ok)throw Error(result.message);scenario=result.id;console.log('QUEUED',scenario,vehicle,cargo);
  if(extended)auxSteps=[
   {actor_id:v.sts,action:'equipment_failure',trigger:'placing',duration:2},
   {actor_id:v.rmg,action:'equipment_failure',trigger:'placing',duration:2},
   {actor_id:other.id,action:'fail_reboot',trigger:'loaded',duration:5}
  ];
 }
 const r=s.incident_scenarios?.find(r=>r.id===scenario);
 if(r){
  if(extended&&!auxScenario){
   const auxResponse=await fetch(base+'/api/equipment-command',{method:'POST',headers:{Origin:base,'Content-Type':'application/json'},body:JSON.stringify({run_id:s.run_id,control_session:s.control_session,actor_id:'scenario',action:'scenario_start',name:'Equipment failure and sensor fault → reboot',steps:auxSteps})});
   const data=await auxResponse.json();if(!auxResponse.ok)throw Error(data.message);auxScenario=data.id;
  }
  const aux=s.incident_scenarios?.find(r=>r.id===auxScenario),runs=aux?[r,aux]:[r];
  for(const item of runs)if(item.steps.some(x=>['rejected','timeout','not_exercised'].includes(x.state)))throw Error(JSON.stringify(item));
  responseObserved||=r.steps.some(x=>x.response_observed&&x.actor_id===vehicle);
  otherMovement||=s.actors.some(a=>initialOthers.has(a.id)&&JSON.stringify(a.position_m)!==initialOthers.get(a.id));
  if((!extended||aux)&&runs.every(item=>item.steps.every(x=>x.state==='verified'))){
   const box=s.actors.find(a=>a.id===cargo);if(!responseObserved||!otherMovement||box?.state!=='Yard')throw Error('Missing physical response, unrelated movement, or final yard ownership');
   const summary={run,scenario,vehicle,cargo,otherMovement,responseObserved,finalCargoState:box.state,scenario:r,additional_scenario:aux,simulation_seconds:s.simulation_seconds};
   await writeFile(new URL(extended?'../../PortSim/Saved/Tests/IncidentExtensions_Live.json':'../../PortSim/Saved/Tests/IncidentManagement_Live.json',import.meta.url),JSON.stringify(summary,null,2));
   console.log(`PASS ${runs.reduce((n,item)=>n+item.steps.length,0)} live incident steps: actual responses, independent recovery, final yard ownership; other vehicles continued.`);process.exit(0);
  }
 }
 await sleep(150);
}
throw Error('Live incident integration timed out');
