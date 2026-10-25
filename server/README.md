# Control API — Render setup

This service hosts the control panel/API, not the Android client itself.

## Routes

- `GET /healthz` — public service health check.
- `GET /admin` — small admin page.
- `GET /api/admin/status` — current global switch state (admin token required).
- `POST /api/admin/enabled` with `{"enabled":true|false}` — changes the switch (admin token required).

The admin token must be set as the Render environment variable `ADMIN_TOKEN`. Do not commit the token or paste it into chat. The admin page keeps the token only in the open browser tab and sends it in an authorization header.

## Important for the free Render service

For this first version, the switch is stored in the running process's memory and starts as **off**. A process restart or free-service spin-down clears it back to off. This is intentional fail-safe behavior for the initial test; before connecting the client, we should add persistent storage or another restart-safe configuration method.

The client is not connected yet. Therefore changing this switch currently only proves the admin API works; it does not yet enable/disable the client.

## Local test

```sh
npm test
npm start
```

The service listens on `0.0.0.0:$PORT` (default port 3000).
