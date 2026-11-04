import test from 'node:test';
import assert from 'node:assert/strict';
import {renderEnvironment,direction,weatherConditions,renderWeatherOverview} from '../public/environment-view.mjs';
import {createDashboardServer} from '../server.mjs';
const snapshot=()=>({running:true,environment:{wind_enabled:true,wind_speed_mps:6.2,wind_from_deg:270,mean_wind_mps:3.5,wind_setting_mps:4,gust_active:true,gust_increment_mps:2.7,fog_enabled:true,fog_visibility_m:30,rain_enabled:true,rainfall_mm_per_hour:25,snow_enabled:false,snowfall_mm_per_hour:0,ice_enabled:false,sea_enabled:true,wave_height_m:.25,wave_period_s:5.5,wave_from_deg:135,swell_height_m:.4,swell_period_s:10.5,swell_from_deg:150,significant_wave_height_m:.47,weather_hold:false,surface:{wetness:.5,snow:0,ice:.1,water_film_mm:.25,snow_depth_mm:0}}});
test('environment displays observed wind, visibility, wave and swell with units and FROM bearing',()=>{
 const html=renderEnvironment(snapshot(),{live:true});
 for(const text of ['6.2','30 m','25 mm/h','0.25 m','0.4 m','5.5 s','10.5 s','270 °','돌풍 발생','서풍','50 %','0.25 mm','작업 진행 중'])assert.ok(html.includes(text),text);
 assert.ok(html.includes("rotate(270deg)"));assert.equal(direction(-90),'서풍');assert.equal(direction(360),'북풍');
});
test('disabled effects retain settings without claiming applied wind, unlimited real visibility or a live snapshot',()=>{
 const s=snapshot();s.environment.wind_enabled=false;s.environment.fog_enabled=false;s.environment.sea_enabled=false;
 const html=renderEnvironment(s,{live:false,age:42});
 assert.match(html,/저장된 환경 기록/);assert.match(html,/42 초 전/);assert.match(html,/안개 제한 없음/);assert.match(html,/해상 운동 OFF/);assert.doesNotMatch(html,/env-needle/);
});
test('missing or old snapshots never invent calm weather or zero measurements',()=>{
 for(const s of [null,{actors:[]},{fog_enabled:false,rain_enabled:false,surface_conditions:{}}]){
  const html=renderEnvironment(s);assert.match(html,/수신|대기/);assert.doesNotMatch(html,/강수·안개 없음|NaN|undefined|Infinity|0 mps/);
 }
});
test('weather hold reason, stable recovery and emergency/manual priorities are shown safely',()=>{
 const s=snapshot();s.environment.weather_hold=true;s.environment.hold_reason='Heavy rain';s.environment.recovery_elapsed_s=4;s.environment.recovery_required_s=10;
 assert.match(renderEnvironment(s,{live:true}),/기상 회복 대기/);assert.match(renderEnvironment(s,{live:true}),/폭우/);assert.ok(renderEnvironment(s).includes("4 s / 10 s"));
 s.environment.hold_reason='<script>alert(1)</script>';assert.doesNotMatch(renderEnvironment(s),/<script>/);assert.match(renderEnvironment(s),/&lt;script&gt;/);
 s.environment.manual_paused=true;assert.match(renderEnvironment(s,{live:true}),/수동 일시정지/);
 s.environment.emergency_stop=true;assert.match(renderEnvironment(s,{live:true}),/비상 정지/);
});
test('HTTP exposes environment assets and places navigation between sensors and performance',async t=>{
 const server=createDashboardServer();await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));t.after(()=>server.close());
 const base='http://127.0.0.1:'+server.address().port;
 for(const file of ['/environment-view.mjs','/environment.css'])assert.equal((await fetch(base+file)).status,200);
 const html=await(await fetch(base)).text();assert.ok(html.indexOf('data-view="sensors"')<html.indexOf('data-view="environment"'));assert.ok(html.indexOf('data-view="environment"')<html.indexOf('data-view="performance"'));
});

test('weather summary distinguishes simultaneous hazards, clear conditions, clouds and unavailable telemetry',()=>{
 const e=snapshot().environment;
 assert.deepEqual(weatherConditions(e).map(x=>x.label),['비','안개']);
 e.wind_speed_mps=10;assert.ok(weatherConditions(e).some(x=>x.label==='강풍'));
 e.rain_enabled=e.fog_enabled=false;e.wind_speed_mps=3;
 assert.equal(weatherConditions(e)[0].label,'맑음');
 e.cloud_cover=.8;assert.equal(weatherConditions(e)[0].label,'흐림');
 e.snow_enabled=true;e.snowfall_mm_per_hour=90;assert.equal(weatherConditions(e)[0].label,'폭설');
 assert.equal(weatherConditions(null)[0].label,'수신 대기');
 const html=renderWeatherOverview({environment:{...e,cloud_cover:undefined}},{live:false});
 assert.match(html,/최근 기록/);assert.match(html,/구름량 미측정/);assert.doesNotMatch(html,/운영 장비|야드 적재 완료|실행시간|NaN|undefined/);
});

test('temperature displays Celsius including negative/zero values and missing telemetry',()=>{
 for(const t of [-25,0,18,40]){
  const s=snapshot();s.environment.temperature_celsius=t;s.environment.daily_temperature_cycle=false;
  assert.ok(renderWeatherOverview(s).includes(t+' °C'));
  assert.match(renderWeatherOverview(s),/고정 기온/);
 }
 const s=snapshot();s.environment.temperature_celsius=22;s.environment.daily_temperature_cycle=true;
 assert.match(renderWeatherOverview(s),/일변화/);
 assert.doesNotMatch(renderWeatherOverview(snapshot()),/undefined|NaN|18 °C/);
});
