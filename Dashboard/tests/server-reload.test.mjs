import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import {createDashboardServer} from '../server.mjs';

test('health reports the source hashes used to detect a stale dashboard server',async t=>{
  const server=createDashboardServer();
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  t.after(async()=>{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));});
  const health=await (await fetch(`http://127.0.0.1:${server.address().port}/api/health`)).json();
  for(const file of ['server.mjs','commands.mjs','public/app.mjs','public/index.html']){
    const content=await readFile(new URL(`../${file}`,import.meta.url));
    assert.equal(health.source_hashes[file],createHash('sha256').update(content).digest('hex'));
  }
});
