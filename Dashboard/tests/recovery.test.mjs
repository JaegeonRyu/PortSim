import test from 'node:test';
import assert from 'node:assert/strict';
import {renderRecoveryDetails,faultDialogTitle} from '../public/recovery-view.mjs';
test('local equipment fault does not claim a terminal-wide stop',()=>{
 assert.match(faultDialogTitle({fault:''},true),/다른 작업은 계속/);
 assert.match(faultDialogTitle({fault:'collision'},true),/전체 운전이 정지/);
 assert.match(renderRecoveryDetails({fault_scope:'equipment',retained_load:true}),/잠금·화물 유지/);
 assert.match(renderRecoveryDetails({fault_scope:'equipment',retained_load:false,recovery_status:'verification_repair_required'}),/안착 검증 미완료/);
 assert.doesNotMatch(renderRecoveryDetails({fault_scope:'terminal'}),/관련 장비·작업만 격리/);
});
test('reassigned container and unavailable alternatives are explicit and escaped',()=>{
 const html=renderRecoveryDetails({original_sts:2,assigned_sts:1,reassignments:1,recovery_hold:'no_compatible_healthy_yard'});
 assert.match(html,/STS 2 → STS 1/);assert.match(html,/적합한 슬롯 없음/);
 assert.match(renderRecoveryDetails({recovery_hold:'<script>'}),/&lt;script&gt;/);
 assert.equal(renderRecoveryDetails({}), '');
});
test('pickup isolation and measured trim are shown without claiming a sensor failure',()=>{
 const html=renderRecoveryDetails({fault_scope:'equipment',pickup_failure:true,pickup_recovery_events:3,pickup_recovery_phase:'isolated',
  spreader_attitude:{enabled:true,pitch_roll_deg:[.12,-.15],torque_nm:[123,456],saturated:true}});
 assert.match(html,/체결 정렬 실패/);assert.doesNotMatch(html,/센서 고장/);
 assert.match(html,/안전 후퇴 후 장비 격리/);assert.match(html,/0.120° \/ -0.150°/);assert.match(html,/123.000 \/ 456.000 N·m/);
 assert.match(html,/한도 도달/);
});
