#!/usr/bin/env node
// Stdio <-> local HTTP bridge for the Seamly2D MCP server (Streamable HTTP, JSON responses, bearer token).
// Reads one JSON-RPC message per line from stdin, POSTs it to Seamly2D and writes the reply to stdout.
'use strict';
const http = require('http');
const readline = require('readline');

const port = Number(process.env.SEAMLY2D_MCP_PORT) || 47800;
const token = (process.env.SEAMLY2D_MCP_TOKEN || '').trim();
const log = (m) => process.stderr.write(`[seamly2d-bridge] ${m}\n`);

function post(body) {
  return new Promise((resolve, reject) => {
    const req = http.request({
      host: '127.0.0.1', port, path: '/mcp', method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Accept': 'application/json, text/event-stream',
        'Authorization': `Bearer ${token}`,
        'Content-Length': Buffer.byteLength(body),
      },
    }, (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => resolve({ status: res.statusCode, text: Buffer.concat(chunks).toString('utf8') }));
    });
    req.on('error', reject);
    req.end(body);
  });
}

function errorReply(id, message) {
  return JSON.stringify({ jsonrpc: '2.0', id, error: { code: -32000, message } });
}

const rl = readline.createInterface({ input: process.stdin });
let pending = 0, closing = false;
rl.on('line', async (line) => {
  if (!line.trim()) return;
  let id = null, isRequest = false;
  try { const m = JSON.parse(line); id = m.id ?? null; isRequest = m.id !== undefined && m.method !== undefined; } catch (_) {}
  pending++;
  try {
    const { status, text } = await post(line);
    if (status === 401) throw new Error('Seamly2D rejected the token. Copy the current token from the MCP panel into the extension settings.');
    if (status >= 400) throw new Error(`Seamly2D answered HTTP ${status}`);
    if (text.trim()) process.stdout.write(text.trim() + '\n');
  } catch (e) {
    const hint = e.code === 'ECONNREFUSED' ? `Seamly2D is not running or its Claude connection is off (port ${port}).` : e.message;
    log(hint);
    if (isRequest) process.stdout.write(errorReply(id, hint) + '\n');
  } finally {
    pending--;
    if (closing && pending === 0) process.exit(0);
  }
});
rl.on('close', () => { closing = true; if (pending === 0) process.exit(0); });
