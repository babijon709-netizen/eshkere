'use strict';

const crypto = require('node:crypto');
const http = require('node:http');

const ADMIN_PAGE = `<!doctype html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Eshkere — управление</title>
  <style>
    body { max-width: 560px; margin: 48px auto; padding: 0 18px; font: 16px system-ui, sans-serif; color: #eee; background: #111; }
    h1 { font-size: 1.4rem; }
    input, button { box-sizing: border-box; width: 100%; margin: 8px 0; padding: 12px; font: inherit; }
    button { cursor: pointer; }
    #state { margin: 18px 0; padding: 14px; background: #222; }
    .row { display: flex; gap: 10px; }
  </style>
</head>
<body>
  <h1>Управление доступом к софту</h1>
  <p>Сначала добавьте ADMIN_TOKEN в Environment Variables Render. Токен вводится только в этом браузере и не сохраняется.</p>
  <label for="token">Админ-токен</label>
  <input id="token" type="password" autocomplete="current-password" placeholder="Вставьте ADMIN_TOKEN">
  <button id="check">Проверить состояние</button>
  <div id="state" role="status">Состояние пока не загружено.</div>
  <div class="row">
    <button id="enable">Включить</button>
    <button id="disable">Выключить</button>
  </div>
  <script>
    const token = document.querySelector('#token');
    const state = document.querySelector('#state');
    async function request(path, method = 'GET', body) {
      const response = await fetch(path, {
        method,
        headers: { 'authorization': 'Bearer ' + token.value, ...(body ? {'content-type': 'application/json'} : {}) },
        body: body ? JSON.stringify(body) : undefined,
        cache: 'no-store'
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || 'request_failed');
      return result;
    }
    async function refresh() {
      try {
        const result = await request('/api/admin/status');
        state.textContent = result.enabled ? 'СОФТ ВКЛЮЧЁН' : 'СОФТ ВЫКЛЮЧЕН';
      } catch (error) {
        state.textContent = error.message === 'unauthorized'
          ? 'Токен не принят. Проверьте ADMIN_TOKEN.'
          : 'Ошибка: ' + error.message;
      }
    }
    async function setEnabled(enabled) {
      try {
        await request('/api/admin/enabled', 'POST', { enabled });
        await refresh();
      } catch (error) {
        state.textContent = error.message === 'unauthorized'
          ? 'Токен не принят. Проверьте ADMIN_TOKEN.'
          : 'Ошибка: ' + error.message;
      }
    }
    document.querySelector('#check').addEventListener('click', refresh);
    document.querySelector('#enable').addEventListener('click', () => setEnabled(true));
    document.querySelector('#disable').addEventListener('click', () => setEnabled(false));
  </script>
</body>
</html>`;

function sendJson(response, status, body) {
  response.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store',
    'x-content-type-options': 'nosniff',
  });
  response.end(JSON.stringify(body));
}

function authorized(request, expectedToken) {
  if (!expectedToken) return false;
  const header = request.headers.authorization || '';
  const prefix = 'Bearer ';
  if (!header.startsWith(prefix)) return false;
  const supplied = Buffer.from(header.slice(prefix.length));
  const expected = Buffer.from(expectedToken);
  return supplied.length === expected.length && crypto.timingSafeEqual(supplied, expected);
}

function readJson(request) {
  return new Promise((resolve, reject) => {
    let data = '';
    request.setEncoding('utf8');
    request.on('data', (chunk) => {
      data += chunk;
      if (data.length > 1024) {
        reject(new Error('body_too_large'));
        request.destroy();
      }
    });
    request.on('end', () => {
      try {
        resolve(JSON.parse(data || '{}'));
      } catch {
        reject(new Error('invalid_json'));
      }
    });
    request.on('error', reject);
  });
}

function createServer(options = {}) {
  const adminToken = options.adminToken ?? process.env.ADMIN_TOKEN ?? '';
  const state = { enabled: false };

  const server = http.createServer(async (request, response) => {
    if (request.method === 'GET' && request.url === '/healthz') {
      return sendJson(response, 200, { ok: true });
    }

    if (request.method === 'GET' && request.url === '/admin') {
      response.writeHead(200, {
        'content-type': 'text/html; charset=utf-8',
        'cache-control': 'no-store',
        'content-security-policy': "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'",
        'x-content-type-options': 'nosniff',
      });
      response.end(ADMIN_PAGE);
      return;
    }

    if (request.url === '/api/admin/status' || request.url === '/api/admin/enabled') {
      if (!authorized(request, adminToken)) {
        return sendJson(response, adminToken ? 401 : 503,
                        { error: adminToken ? 'unauthorized' : 'admin_not_configured' });
      }

      if (request.method === 'GET' && request.url === '/api/admin/status') {
        return sendJson(response, 200, { enabled: state.enabled });
      }

      if (request.method === 'POST' && request.url === '/api/admin/enabled') {
        try {
          const body = await readJson(request);
          if (typeof body.enabled !== 'boolean') {
            return sendJson(response, 400, { error: 'enabled_must_be_boolean' });
          }
          state.enabled = body.enabled;
          return sendJson(response, 200, { enabled: state.enabled });
        } catch (error) {
          return sendJson(response, 400, { error: error.message || 'invalid_request' });
        }
      }

      response.setHeader('allow', request.url === '/api/admin/status' ? 'GET' : 'POST');
      return sendJson(response, 405, { error: 'method_not_allowed' });
    }

    if (request.method !== 'GET') {
      response.setHeader('allow', 'GET');
      return sendJson(response, 405, { error: 'method_not_allowed' });
    }

    return sendJson(response, 404, { error: 'not_found' });
  });

  server.controlState = state;
  return server;
}

if (require.main === module) {
  const port = Number.parseInt(process.env.PORT || '3000', 10);
  const server = createServer();
  server.listen(port, '0.0.0.0', () => {
    console.log(`Control API listening on 0.0.0.0:${port}`);
  });
}

module.exports = { createServer };
