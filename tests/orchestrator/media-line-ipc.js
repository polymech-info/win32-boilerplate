/**
 * Line-delimited JSON over TCP or Unix stream (media-img `ipc` mode).
 * One request per connection (server closes after one response).
 */
import net from 'node:net';

/**
 * @param {import('node:net').Socket} socket
 * @param {Record<string, unknown>} payload
 * @param {number} [timeoutMs]
 */
export function requestLineJson(socket, payload, timeoutMs = 10_000) {
  return new Promise((resolve, reject) => {
    let buf = '';
    const timer = setTimeout(() => {
      cleanup();
      reject(new Error('IPC line read timeout'));
    }, timeoutMs);

    function cleanup() {
      clearTimeout(timer);
      socket.removeListener('data', onData);
      socket.removeListener('error', onErr);
    }

    function onData(chunk) {
      buf += chunk.toString('utf8');
      const lineEnd = buf.indexOf('\n');
      if (lineEnd >= 0) {
        cleanup();
        const line = buf.slice(0, lineEnd);
        try {
          resolve(JSON.parse(line));
        } catch (e) {
          reject(e);
        }
      }
    }

    function onErr(e) {
      cleanup();
      reject(e);
    }

    socket.on('data', onData);
    socket.once('error', onErr);
    socket.write(`${JSON.stringify(payload)}\n`);
  });
}

/**
 * @param {string} host
 * @param {number} port
 */
export function connectTcp(host, port) {
  return new Promise((resolve, reject) => {
    const s = net.connect({ host, port }, () => resolve(s));
    s.once('error', reject);
  });
}

/**
 * @param {string} path
 */
export function connectUnix(path) {
  return new Promise((resolve, reject) => {
    const s = net.connect(path, () => resolve(s));
    s.once('error', reject);
  });
}
