const finite = value => Number.isFinite(value) ? value : null;
const fmt = (value, digits=1) => finite(value) === null ? '—' : value.toFixed(digits);

// The streamlines illustrate the measured wind vector; they are not a CFD solution.
export function renderSTSWind(a, live) {
  const physics=a.dynamics;
  const wind=physics?.wind_velocity_mps;
  const wx=finite(wind?.[0]), wy=finite(wind?.[1]);
  const available=wx!==null && wy!==null;
  const speed=available?Math.hypot(wx,wy):null;
  const direction=wx===null||Math.abs(wx)<0.05?0:Math.sign(wx);
  const offset=finite(physics?.offset_m?.[0])??0;
  const sway=finite(physics?.sway_angle_rad?.[0])??0;
  const rope=finite(a.rope_length_m)??12;
  const loadX=Math.max(-25,Math.min(25,(offset+Math.tan(sway)*rope)*4));
  const carrying=Boolean(a.carrying);
  const trolleyX=220;
  const cargoX=trolleyX+loadX;
  const flow=available&&speed>0.05;
  const flowLevels=[54,105,132,160,183,220,244];
  const lines=flow?flowLevels.map((y,i)=>{
    const aroundCargo=carrying&&y>=220;
    const bypass=aroundCargo?(y===220?181:261):y;
    const left=cargoX-50,right=cargoX+50;
    const path=aroundCargo
      ?`M -12 ${y} L ${left-38} ${y} C ${left-17} ${y} ${left-18} ${bypass} ${left} ${bypass} L ${right} ${bypass} C ${right+18} ${bypass} ${right+17} ${y} ${right+38} ${y} L 562 ${y}`
      :`M -12 ${y} L 562 ${y}`;
    return `<path class="sts-flow-line" d="${path}" pathLength="100" style="--flow-duration:${Math.max(2,8-Math.min(speed,20)*.27).toFixed(2)}s;--flow-delay:${(-i*.63).toFixed(2)}s;--flow-distance:${direction>=0?'-100':'100'}"/>`;
  }).join(''):'';
  const arrow=direction>=0?'→':direction<0?'←':'·';
  return `<div class="sts-wind-card"><svg viewBox="0 -45 550 400" role="img" aria-label="STS 크레인 전체 측면도와 트롤리·현수 화물의 바람 영향"><defs><pattern id="sts-wind-grid" width="20" height="20" patternUnits="userSpaceOnUse"><path d="M20 0H0v20" fill="none" stroke="#dcebe9" stroke-width=".6"/></pattern><clipPath id="sts-wind-clip"><rect y="-45" width="550" height="400"/></clipPath></defs><rect y="-45" width="550" height="400" fill="#edf6f5"/><rect y="-45" width="550" height="400" fill="url(#sts-wind-grid)"/><g clip-path="url(#sts-wind-clip)">${lines}</g><path d="M20 268h510" stroke="#c0d1c5"/><path d="M20 270h260v18H20z" fill="#dbe9e6"/><path d="M350 267V83h94v184m-94-94h94" fill="none" stroke="#77a18c" stroke-width="7"/><path d="M350 173l94-90m-94 0 94 90M350 84l47-57 47 57M397 27V15M397 27 30 78M397 27l116 51" fill="none" stroke="#9abca8" stroke-width="2"/><path d="M30 78h483M30 88h483" stroke="#4c8b75" stroke-width="4"/><path d="M30 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M47 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M64 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M81 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M98 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M115 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M132 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M149 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M166 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M183 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M200 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M217 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M234 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M251 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M268 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M285 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M302 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M319 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M336 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M353 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M370 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M387 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M404 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M421 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M438 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M455 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M472 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><path d="M489 78l17 10m0-10-17 10" stroke="#78a793" stroke-width="1.2"/><rect x="384" y="70" width="50" height="17" fill="#b3cfbd"/><path d="M344 267h19m74 0h19" stroke="#577c69" stroke-width="7"/><rect x="453" y="250" width="46" height="14" rx="3" fill="#9ebac2"/><circle cx="463" cy="267" r="4" fill="#6b8b91"/><circle cx="489" cy="267" r="4" fill="#6b8b91"/><rect x="${trolleyX-31}" y="70" width="62" height="22" rx="4" fill="#bdab78" stroke="#867b5e"/><circle cx="${trolleyX-20}" cy="69" r="4" fill="#547e76"/><circle cx="${trolleyX+20}" cy="69" r="4" fill="#547e76"/><path d="M${trolleyX-16} 92L${cargoX-23} 195M${trolleyX+16} 92L${cargoX+23} 195" stroke="#6c9186" stroke-width="2"/><rect x="${cargoX-31}" y="192" width="62" height="7" rx="2" fill="#347d70"/>${carrying?`<rect x="${cargoX-26}" y="202" width="52" height="52" rx="3" fill="#8fb8ad" stroke="#508d7e" stroke-width="2"/>${Array.from({length:5},(_,i)=>`<path d="M${cargoX-18+i*9} 207v42" stroke="#6d9f90"/>`).join('')}`:'<path d="M235 250h78" stroke="#9ab4aa" stroke-dasharray="4 4"/>'}<path d="M22 270H528" stroke="#bfd5ce"/><text x="22" y="25" class="sts-wind-title">STS · SIDE ELEVATION</text><text x="22" y="45" class="sts-wind-small">${available?`${arrow} X ${fmt(Math.abs(wx))} m/s   ·   Y ${fmt(wy)} m/s`:'풍속 관측 대기'}</text><text x="${trolleyX+40}" y="87" class="sts-wind-small">TROLLEY</text><text x="${Math.min(410,cargoX+65)}" y="230" class="sts-wind-small">${carrying?'CONTAINER':'화물 미체결'}</text><text x="22" y="289" class="sts-wind-small">${live?'LIVE':'SAVED SNAPSHOT'} · 크레인 전체 측면도 · 바람 유선</text></svg><div class="sts-wind-readout"><span>합성 풍속 <b>${fmt(speed)} m/s</b></span><span>X 흔들림 <b>${fmt(finite(physics?.sway_deg),2)}°</b></span><span>화물 <b>${carrying?'매달림':'미체결'}</b></span></div><p>바람 유선은 관측값을 설명하는 2D 도식입니다. Y 방향 바람과 3D 난류는 이미지에 투영되지 않습니다.</p></div>`;
}
