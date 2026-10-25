'use strict';

const assert = require('node:assert/strict');
const { after, before, test } = require('node:test');
const { createServer } = require('./index');

const server = createServer({ adminToken: 'test-secret-token-1234567890' });
let baseUrl;

before(async () => {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  await new Promise((resolve) => server.close(resolve));
});

test('health endpoint works without admin token', async () => {
  const response = await fetch(`${baseUrl}/healthz`);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { ok: true });
});

test('admin endpoints reject requests without the token', async () => {
  const response = await fetch(`${baseUrl}/api/admin/status`);
  assert.equal(response.status, 401);
  assert.deepEqual(await response.json(), { error: 'unauthorized' });
});

test('admin can read and change the global switch', async () => {
  const headers = { authorization: 'Bearer test-secret-token-1234567890' };
  let response = await fetch(`${baseUrl}/api/admin/status`, { headers });
  assert.deepEqual(await response.json(), { enabled: false });

  response = await fetch(`${baseUrl}/api/admin/enabled`, {
    method: 'POST',
    headers: { ...headers, 'content-type': 'application/json' },
    body: JSON.stringify({ enabled: true }),
  });
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { enabled: true });

  response = await fetch(`${baseUrl}/api/admin/status`, { headers });
  assert.deepEqual(await response.json(), { enabled: true });
});
