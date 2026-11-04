const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const n=(v,d=2)=>Number.isFinite(v)?v.toFixed(d):'—';
const vec=v=>Array.isArray(v)?v.map(x=>n(x)).join(' / '):'—';
const row=(label,value)=>`<div class="kv-row"><span>${label}</span><b>${esc(value)}</b></div>`;
export function renderRSPhysics(a){
 const p=a.rs_physics;
 if(!p)return '<div class="note">R/S 물리 데이터가 포함된 엔진으로 다시 실행하세요.</div>';
 return '<div class="detail-subhead">R/S 타이어 · 전도 · 유압</div>'+
 row('계산 간격',`${n(p.step_seconds,6)} s`)+row('물리 상태',p.free_body?'Chaos 자유 강체':'타이어·하중 이동 모델')+
 row('속도',`${n(a.speed_mps)} m/s`)+row('총질량',`${n(p.mass_kg/1000)} t`)+
 row('롤 / 피치',`${n(p.roll_deg)}° / ${n(p.pitch_deg)}°`)+
 row('하중 이동비 / 바퀴 들림',`${n(p.load_transfer_ratio,3)} / ${p.wheel_lift?'발생':'없음'}`)+
 row('최대 타이어 슬립비',n(p.peak_slip,3))+row('합성 무게중심',`${vec(p.combined_cog_m)} m`)+
 row('유압 X/Y/Z',`${vec(p.hydraulic_pressure_mpa)} MPa`)+row('펌프 유량',`${n(p.hydraulic_flow_lpm,1)} L/min`)+
 row('릴리프 압력',`${n(p.relief_pressure_mpa)} MPa`)+row('유압 제한',p.hydraulic_limited?'압력·유량 또는 반경 제한':'정상')+
 '<div class="detail-subhead">타이어 접점</div>'+(p.wheels||[]).map((w,i)=>row(`접점 ${i+1}`,`${n(w.normal_n/1000)} kN · ${n(w.torque_nm)} Nm · slip ${n(w.slip_ratio,3)}`)).join('')+
 '<div class="note">6개 등가 타이어 접점과 3축 등가 유압 액추에이터를 사용하는 가정 모델입니다. 제조사 하중표·실측 유압 회로를 재현한 모델은 아닙니다. 자유 강체 전환 후에는 초기화가 필요합니다.</div>';
}
