import test from 'node:test';
import assert from 'node:assert/strict';
import {renderFrictionDetails} from '../public/friction-view.mjs';
test('weather details distinguish inactive seating, insufficient friction and mechanical locks',()=>{
 const crane={type:'sts',friction:{seating_active:true,seating_grip_ok:false,seating_demand_n:200,seating_capacity_n:100}};
 assert.match(renderFrictionDetails(crane),/안착 마찰 부족/);
 assert.match(renderFrictionDetails(crane),/배수·건조·융해가 진행/);
 assert.match(renderFrictionDetails(crane),/200 \/ 100 N/);
 crane.friction.mechanically_locked=true;
 assert.match(renderFrictionDetails(crane),/트위스트록 기계 결합/);
 crane.friction.seating_active=false;
 assert.doesNotMatch(renderFrictionDetails(crane),/200 \/ 100 N/);
});
test('live water, snow and drying status are displayed with current units',()=>{
 const html=renderFrictionDetails({type:'agv',friction:{water_film_mm:.25,snow_depth_mm:2,water_change_mm_per_hour:-.5,snow_change_mm_per_hour:-20,drying:true,dryness:.5,wetness:.5,snow:.2,ice:0}});
 assert.match(html,/배수·건조 중/);assert.match(html,/0.25 mm \/ 2 mm/);assert.match(html,/50%/);
 assert.match(html,/-0.5 \/ -20 mm\/h/);
});
test('old telemetry is tolerated and unavailable braking is not displayed as zero stopping distance',()=>{
 assert.equal(renderFrictionDetails({type:'agv'}),'');
 const html=renderFrictionDetails({type:'agv',friction:{traction_available:false,stopping_distance_m:0,wetness:1,ice:0,snow:0}});
 assert.match(html,/접지력 부족/);assert.match(html,/100%/);assert.doesNotMatch(html,/NaN|Infinity|undefined/);
});
