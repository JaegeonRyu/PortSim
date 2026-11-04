const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const n=(v,unit='',digits=1)=>Number.isFinite(v)?v.toLocaleString('ko-KR',{maximumFractionDigits:digits})+(unit?' '+unit:''):'—';
const on=v=>v===true?'ON':v===false?'OFF':'수신 대기';
const row=(label,value)=>'<div class="env-row"><span>'+label+'</span><strong>'+esc(value)+'</strong></div>';
const dirs=['북','북북동','북동','동북동','동','동남동','남동','남남동','남','남남서','남서','서남서','서','서북서','북서','북북서'];
export const direction=deg=>Number.isFinite(deg)?dirs[Math.round(((deg%360+360)%360)/22.5)%16]+'풍':'—';
const reasons={'Strong wind':'강풍','Low visibility':'가시거리 부족','Heavy rain':'폭우','Heavy snow':'폭설','Icy surface':'노면 결빙','Insufficient tire grip':'타이어 접지력 부족','High waves':'높은 파랑'};
function panel(title,sub,body,tag=''){return '<section class="panel env-panel"><div class="panel-head"><div><h2>'+title+'</h2><p>'+sub+'</p></div>'+tag+'</div><div class="env-body">'+body+'</div></section>';}
function badge(value){return '<span class="env-pill">'+esc(value)+'</span>';}
const weatherPaths={
 clear:'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1.5 1.5m11 11L19 19M5 19l1.5-1.5m11-11L19 5',
 cloudy:'M6 18a4 4 0 1 1 .7-7.9 6 6 0 0 1 11.6 1.4A3.5 3.5 0 1 1 18 18H6',
 rain:'M6 15a4 4 0 1 1 .7-7.9 6 6 0 0 1 11.6 1.4A3.5 3.5 0 0 1 18 15H6M8 18l-1 3m6-3-1 3m6-3-1 3',
 snow:'M6 14a4 4 0 1 1 .7-7.9 6 6 0 0 1 11.6 1.4A3.5 3.5 0 0 1 18 14H6M8 17v5m-2.2-3.7 4.4 2.4m-4.4 0 4.4-2.4M17 17v5m-2.2-3.7 4.4 2.4m-4.4 0 4.4-2.4',
 wind:'M3 8h12a3 3 0 1 0-3-3M2 12h17a3 3 0 1 1-3 3M4 17h5a2 2 0 1 1-2 2',
 fog:'M6 12a4 4 0 1 1 .7-7.9 6 6 0 0 1 11.6 1.4A3.5 3.5 0 0 1 18 12M3 16h18M5 20h14',
 unknown:'M9 8a3 3 0 1 1 5 2.2c-1.2.7-2 1.3-2 2.8M12 17v.1M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20'
};
export function weatherConditions(e){
 if(!e)return [{key:'unknown',label:'수신 대기'}];
 const conditions=[];
 if(e.rain_enabled===true)conditions.push({key:'rain',label:e.rainfall_mm_per_hour>=50?'폭우':'비'});
 if(e.snow_enabled===true)conditions.push({key:'snow',label:e.snowfall_mm_per_hour>=80?'폭설':'눈'});
 if(e.wind_enabled===true&&Number.isFinite(e.wind_speed_mps)&&e.wind_speed_mps>=10)conditions.push({key:'wind',label:'강풍'});
 if(e.fog_enabled===true)conditions.push({key:'fog',label:'안개'});
 if(Number.isFinite(e.cloud_cover)&&e.cloud_cover>=.5)conditions.push({key:'cloudy',label:'흐림'});
 if(!conditions.length){
  const known=['rain_enabled','snow_enabled','fog_enabled','wind_enabled'].every(k=>typeof e[k]==='boolean')&&(e.wind_enabled===false||Number.isFinite(e.wind_speed_mps));
  conditions.push(known?{key:'clear',label:'맑음'}:{key:'unknown',label:'수신 대기'});
 }
 return conditions;
}
export function renderWeatherOverview(state,{live=false}={}){
 const e=state?.environment,conditions=weatherConditions(e),primary=conditions[0];
 const glyph=k=>'<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="'+weatherPaths[k]+'"/></svg>';
 const speed=e?.wind_enabled===false?0:e?.wind_speed_mps;
 return '<section class="weather-overview weather-'+primary.key+'" aria-label="현재 날씨"><div class="weather-primary"><div class="weather-symbol">'+glyph(primary.key)+'</div><div><small>현재 날씨 · '+(live?'실시간':'최근 기록')+'</small><strong>'+primary.label+'</strong><div class="weather-conditions">'+conditions.slice(1).map(c=>'<span>'+glyph(c.key)+c.label+'</span>').join('')+'</div></div></div><div class="weather-facts"><div><small>기온</small><strong>'+n(e?.temperature_celsius,'°C')+'</strong><span>'+(e?.daily_temperature_cycle===true?'일변화':e?.daily_temperature_cycle===false?'고정 기온':'수신 대기')+'</span></div><div><small>풍속</small><strong>'+n(speed,'m/s')+'</strong><span>'+(e?.wind_enabled===true&&Number.isFinite(speed)&&speed>0?direction(e.wind_from_deg):'—')+'</span></div><div><small>강수량</small><strong>'+n(e?.rainfall_mm_per_hour,'mm/h')+'</strong><span>강설 '+n(e?.snowfall_mm_per_hour,'mm/h')+'</span></div><div><small>가시거리</small><strong>'+(e?.fog_enabled===false?'제한 없음':e?.fog_enabled===true&&e.fog_visibility_m>=0?n(e.fog_visibility_m,'m'):'—')+'</strong><span>안개 기준</span></div></div><div class="weather-source">시뮬레이션 날씨 · '+(Number.isFinite(e?.cloud_cover)?'구름량 '+n(e.cloud_cover*100,'%',0):'구름량 미측정 · 맑음: 비·눈·안개·강풍 없음')+'</div></section>';
}
export function renderEnvironment(state,{live=false,age=null}={}){
 const full=!!state?.environment,e=state?.environment||{fog_enabled:state?.fog_enabled,fog_visibility_m:state?.fog_visibility_m,rain_enabled:state?.rain_enabled,rainfall_mm_per_hour:state?.rainfall_mm_per_hour},s=e.surface||state?.surface_conditions||{};
 const status=!state?'엔진 연결 대기':!live?'저장된 환경 기록':e.emergency_stop?'비상 정지':e.manual_paused?'수동 일시정지':e.weather_hold?'기상 회복 대기':state.fault?'운용 오류':!state.running?'실행 대기':full?'작업 진행 중':'환경 데이터 일부 수신';
 const alert=e.weather_hold||e.emergency_stop||state?.fault;
 const freshness=live?'LIVE · 1초 간격 갱신':state?'SNAPSHOT · '+(Number.isFinite(age)?n(age,'초 전',0):'최근 저장 기록'):'NO DATA';
 const effective=e.wind_enabled===false?0:e.wind_speed_mps;
 const bearing=e.wind_enabled===true&&Number.isFinite(e.wind_from_deg)&&Number.isFinite(effective)&&effective>0;
 const angle=bearing?(e.wind_from_deg%360+360)%360:0;
 const compass='<div class="env-compass" role="img" aria-label="'+esc(bearing?direction(angle)+' '+n(angle,'°'):'풍향 수신 대기 또는 바람 꺼짐')+'"><span class="north">N</span><span class="east">E</span><span class="south">S</span><span class="west">W</span><div class="env-compass-ring"></div>'+(bearing?'<div class="env-needle" style="transform:rotate('+angle+'deg)"><i></i></div>':'')+'<div class="env-compass-core">'+(bearing?esc(direction(angle)):'—')+'<small>불어오는 방향</small></div></div>';
 const wind=panel('바람과 돌풍','평균풍 · 유효 풍속 · 돌풍 증가량', '<div class="env-wind">'+compass+'<div><div class="env-number">'+n(effective)+'<small>m/s</small></div><p class="env-muted">현재 유효 풍속 · 돌풍 포함</p>'+row('풍향',bearing?direction(angle)+' / '+n(angle,'°'):'—')+row('기준 설정',n(e.wind_setting_mps,'m/s'))+row('현재 평균풍',e.wind_enabled===false?'적용 안 함':n(e.mean_wind_mps,'m/s'))+row('돌풍 증가량',n(e.gust_increment_mps,'m/s'))+'</div></div>',badge(e.gust_active===true?'돌풍 발생':on(e.wind_enabled)));
 const visibility=e.fog_enabled===false?'안개 제한 없음':e.fog_enabled===true&&e.fog_visibility_m>=0?n(e.fog_visibility_m,'m'):'—';
 const precip=panel('강수와 가시거리','카메라 · LiDAR 감지 거리 관련 조건',row('안개 가시거리',visibility)+row('강우량',n(e.rainfall_mm_per_hour,'mm/h'))+row('강설량',n(e.snowfall_mm_per_hour,'mm/h'))+row('비 / 눈',on(e.rain_enabled)+' / '+on(e.snow_enabled))+'<p class="env-footnote">비·안개 동시 적용: 센서별 강우 감쇠 + 가시거리 제한</p>',badge(on(e.fog_enabled)==='ON'?'안개 ON':'시정 관측'));
 const seaCard=(name,height,period,from)=>'<div class="env-sea-card"><div class="env-wave-art" aria-hidden="true">∿ ∿ ∿</div><h3>'+name+'</h3><strong>'+n(height,'m',2)+'</strong><span>파고</span>'+row('주기',n(period,'s'))+row('입사 방향',Number.isFinite(from)?direction(from).replace(/풍$/,'')+' / '+n(from,'°'):'—')+'</div>';
 const sea=panel('파도와 너울','파고 · 주기 · 입사 방향','<div class="env-seas">'+seaCard('파도',e.wave_height_m,e.wave_period_s,e.wave_from_deg)+seaCard('너울',e.swell_height_m,e.swell_period_s,e.swell_from_deg)+'</div>'+row('합성 유의파고 Hs',n(e.significant_wave_height_m,'m',2))+'<p class="env-footnote">'+(e.sea_enabled===false?'해상 운동 OFF · 저장된 설정값 · 선박 운동 미적용':'선박 1 현재 응답 · 상하 '+n(e.heave_m,'m',3)+' / 롤 '+n(e.roll_deg,'°',2)+' / 피치 '+n(e.pitch_deg,'°',2))+'</p>',badge(on(e.sea_enabled)));
 const percent=v=>Number.isFinite(v)?n(v*100,'%',0):'—';
 const surface=panel('노면과 운용 상태','물막 · 적설 · 결빙 잔류량',row('물막 / 적설',n(s.water_film_mm,'mm',2)+' / '+n(s.snow_depth_mm,'mm'))+['wetness','snow','ice'].map((k,i)=>{const v=Number.isFinite(s[k])?Math.min(100,Math.max(0,s[k]*100)):0;return '<div class="env-surface"><div><span>'+['젖음','눈 덮임','결빙'][i]+'</span><strong>'+percent(s[k])+'</strong></div><div class="env-meter"><span style="width:'+v+'%"></span></div></div>';}).join('')+row('물막 변화',n(s.water_change_mm_per_hour,'mm/h',2))+row('적설 변화',n(s.snow_change_mm_per_hour,'mm/h',2))+'<p class="env-footnote">음수 변화율: 배수·건조·융설</p>');
 const recovery=e.weather_hold?'<div class="env-hold">대기 사유: '+esc(reasons[e.hold_reason]||e.hold_reason||'기상 조건 확인 중')+'<span>회복 조건 유지 '+n(e.recovery_elapsed_s,'s')+' / '+n(e.recovery_required_s,'s')+'</span></div>':'';
 return '<div class="environment-page"><section class="env-status-bar '+(alert?'env-warning':'')+'"><strong>'+esc(status)+'</strong>'+badge(freshness)+recovery+'</section>'+(!full?'<div class="note">'+(state?'상세 환경 데이터 없음 · 최신 엔진 빌드 실행 필요':'환경 데이터 대기 · Unreal 시뮬레이션 실행 필요')+'</div>':'')+'<div class="env-grid">'+wind+precip+sea+surface+'</div><p class="env-footnote">풍향·파향: 유입 방향(FROM) · 북쪽: 시뮬레이션 +X축 · 실제 지도 방위와 별도</p></div>';
}
