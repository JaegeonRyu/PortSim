// Run against an exported engine snapshot after the integrated site test.
import {readFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
const state=JSON.parse(await readFile(new URL('../../PortSim/Saved/Dashboard/state.json',import.meta.url),'utf8'));
const containers=state.actors.filter(a=>a.type==='container');
assert.equal(containers.length,600);
const byId=new Map(containers.map(c=>[c.id,c]));
let placements=0,short=0;
for(const vessel of state.vessels){
 const p=vessel.ship_loading_preview;
 assert(p.feasible&&p.applied_to_arrival,'dashboard must show the applied arrival, not an unrelated preview');
 assert.equal(p.state_basis,'initial_arrival_not_live_hydrostatics');
 assert.equal(p.placements.length,vessel.initial_count);
 assert(p.corrected_gm_m>=p.minimum_gm_m&&p.corrected_gm_m<=p.maximum_gm_m);
 for(const slot of p.placements){
  // Equipment IDs and displayed cargo labels use different prefixes in the schema.
  const c=containers.find(c=>c.name===slot.cargo||c.container_id===slot.cargo)||byId.get(slot.cargo);
  assert(c,`missing physical container ${slot.cargo}`);
  assert.equal(c.ship_bay,slot.bay);assert.equal(c.ship_row,slot.row);assert.equal(c.ship_tier,slot.tier);
  assert.equal(c.ship_subslot,slot.subslot);assert.equal(c.ship_below_container,slot.below_container);
  assert.equal(c.ship_initial_position_m.length,3);
  assert(c.ship_initial_position_m.every(Number.isFinite));
  const [long,trans,z]=slot.ship_axes_position_m;
  const expected=[vessel.position_m[0]+trans,vessel.position_m[1]+long,z];
  expected.forEach((v,i)=>assert(Math.abs(v-c.ship_initial_position_m[i])<.0001,'telemetry must match the applied physical coordinate mapping'));
  if(c.container_length_ft===20){assert([1,2].includes(slot.subslot));short++;}
  else assert.equal(slot.subslot,0);
  placements++;
 }
}
assert.equal(placements,600);assert.equal(short,168);
console.log(`ARRIVAL TELEMETRY PASS: ${placements} real container slots; ${short} 20ft subslots`);
