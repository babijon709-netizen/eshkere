'use strict';

const http = require('node:http');

function sendJson(response, status, body) {
  response.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store',
  });
  response.end(JSON.stringify(body));
}

function createServer() {
  return http.createServer((request, response) => {
    if (request.method === 'GET' && request.url === '/healthz') {
      return sendJson(response, 200, { ok: true });
    }

    if (request.method !== 'GET') {
      response.setHeader('allow', 'GET');
      return sendJson(response, 405, { error: 'method_not_allowed' });
    }

    return sendJson(response, 404, { error: 'not_found' });
  });
}

if (require.main === module) {
  const port = Number.parseInt(process.env.PORT || '3000', 10);
  const server = createServer();
  server.listen(port, '0.0.0.0', () => {
    console.log(`Control API listening on 0.0.0.0:${port}`);
  });
}

module.exports = { createServer };
