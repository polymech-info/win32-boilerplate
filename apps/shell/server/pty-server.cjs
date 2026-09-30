/**
 * PolyMech Shell PTY Server
 *
 * A testing server that bridges xterm.js (frontend) to real shell processes
 * using node-pty and WebSocket.
 *
 * Architecture:
 *   Frontend (xterm.js) ← WebSocket → PTY Server ← node-pty → Real Shell
 *
 * This will be replaced by the C++ backend in production.
 */

const { WebSocketServer } = require("ws");
const pty = require("node-pty");
const http = require("http");
const fs = require("fs");
const path = require("path");

// Configuration
const PORT = process.env.SHELL_SERVER_PORT || 8080;
const HOST = process.env.SHELL_SERVER_HOST || "127.0.0.1";

// Detect shell based on platform
function getDefaultShell() {
  if (process.platform === "win32") {
    // Use CMD as default for stability (PowerShell has ConPTY issues in headless mode)
    const cmdPath = "C:\\Windows\\System32\\cmd.exe";
    const psPath = "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    const pwshPath = "C:\\Program Files\\PowerShell\\7\\pwsh.exe";

    // Return CMD first for stability during testing
    if (fs.existsSync(cmdPath)) return cmdPath;
    if (fs.existsSync(psPath)) return psPath;
    if (fs.existsSync(pwshPath)) return pwshPath;
    return "cmd.exe";
  }

  // Unix-like systems
  return process.env.SHELL || "/bin/bash";
}

// Map shell types to executables (Windows uses full paths)
const shellMap = process.platform === "win32"
  ? {
      powershell: "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
      pwsh: "C:\\Program Files\\PowerShell\\7\\pwsh.exe",
      cmd: "C:\\Windows\\System32\\cmd.exe",
      wsl: "C:\\Windows\\System32\\wsl.exe",
      "git-bash": "C:\\Program Files\\Git\\bin\\bash.exe",
    }
  : {
      powershell: "powershell",
      cmd: "cmd",
      bash: "/bin/bash",
      zsh: "/bin/zsh",
      fish: "/usr/bin/fish",
    };

// Active PTY sessions
const sessions = new Map();

// Create HTTP server (for WebSocket upgrade)
const server = http.createServer((req, res) => {
  res.writeHead(200, { "Content-Type": "text/plain" });
  res.end("PolyMech Shell PTY Server\nConnect via WebSocket for terminal access.\n");
});

// Create WebSocket server
const wss = new WebSocketServer({ server });

console.log(`[Shell Server] Starting on ${HOST}:${PORT}...`);
console.log(`[Shell Server] Platform: ${process.platform}`);
console.log(`[Shell Server] Default shell: ${getDefaultShell()}`);

wss.on("connection", (ws, req) => {
  const clientId = `${req.socket.remoteAddress}:${req.socket.remotePort}`;
  console.log(`[Shell Server] Client connected: ${clientId}`);

  let ptyProcess = null;
  let shellId = null;

  ws.on("message", (data) => {
    try {
      const msg = JSON.parse(data.toString());

      switch (msg.type) {
        case "create": {
          // Create new PTY session
          shellId = msg.shellId || `shell-${Date.now()}`;
          const shellType = msg.shellType || "default";

          // Determine shell executable
          let shellExe = shellMap[shellType] || getDefaultShell();

          // Verify the executable exists, fallback to default if not
          if (!fs.existsSync(shellExe)) {
            console.log(`[Shell Server] Shell not found: ${shellExe}, using default`);
            shellExe = getDefaultShell();
          }

          console.log(
            `[Shell Server] Creating PTY: ${shellId} (${shellType}: ${shellExe})`
          );

          // Windows-specific: use WinPTY instead of ConPTY to avoid AttachConsole issues
          // ConPTY requires a console window which isn't available in headless/server mode
          const isWindows = process.platform === "win32";

          // Spawn PTY process
          try {
            console.log(`[Shell Server] Spawning: ${shellExe}`);
            ptyProcess = pty.spawn(shellExe, [], {
              name: "xterm-color",
              cols: msg.cols || 80,
              rows: msg.rows || 24,
              cwd: process.env.HOME || process.env.USERPROFILE || ".",
              env: process.env,
              // Use WinPTY on Windows for stability (ConPTY has console attachment issues)
              useConpty: isWindows ? false : undefined,
              // Windows-specific: don't inherit console
              windowsHide: isWindows ? true : undefined,
            });
            console.log(`[Shell Server] Spawned PID: ${ptyProcess.pid}`);
          } catch (spawnErr) {
            console.error(`[Shell Server] Failed to spawn: ${spawnErr.message}`);
            ws.send(JSON.stringify({
              type: "error",
              error: `Failed to spawn shell: ${spawnErr.message}`
            }));
            break;
          }

          // Store session
          sessions.set(shellId, {
            pty: ptyProcess,
            ws: ws,
            shellType: shellType,
            createdAt: new Date(),
          });

          // Handle PTY output → WebSocket
          let outputCount = 0;
          ptyProcess.onData((data) => {
            outputCount++;
            if (outputCount <= 5) {
              console.log(`[Shell Server] PTY output #${outputCount} for ${shellId}: ${data.substring(0, 50).replace(/\n/g, '\\n')}...`);
            }
            if (ws.readyState === ws.OPEN) {
              ws.send(
                JSON.stringify({
                  type: "output",
                  shellId: shellId,
                  data: data,
                })
              );
            } else {
              console.log(`[Shell Server] Cannot send output - WebSocket not open (state: ${ws.readyState})`);
            }
          });

          // Handle PTY exit
          ptyProcess.onExit(({ exitCode, signal }) => {
            console.log(
              `[Shell Server] PTY exited: ${shellId} (code: ${exitCode}, signal: ${signal})`)
            if (ws.readyState === ws.OPEN) {
              ws.send(
                JSON.stringify({
                  type: "exit",
                  shellId: shellId,
                  exitCode: exitCode,
                  signal: signal,
                })
              );
            }
            sessions.delete(shellId);
            ptyProcess = null;
          });

          // Acknowledge creation
          ws.send(
            JSON.stringify({
              type: "created",
              shellId: shellId,
              shellType: shellType,
              pid: ptyProcess.pid,
            })
          );
          break;
        }

        case "input":
          // Send input to PTY
          if (ptyProcess && msg.data) {
            ptyProcess.write(msg.data);
          }
          break;

        case "resize":
          // Resize PTY
          if (ptyProcess && msg.cols && msg.rows) {
            ptyProcess.resize(msg.cols, msg.rows);
          }
          break;

        case "kill":
          // Kill PTY process
          if (ptyProcess) {
            console.log(`[Shell Server] Killing PTY: ${shellId}`);
            ptyProcess.kill();
          }
          break;

        case "list":
          // List active sessions
          const activeSessions = Array.from(sessions.entries()).map(
            ([id, session]) => ({
              shellId: id,
              shellType: session.shellType,
              pid: session.pty.pid,
              createdAt: session.createdAt,
            })
          );
          ws.send(
            JSON.stringify({
              type: "list",
              sessions: activeSessions,
            })
          );
          break;

        default:
          console.log(`[Shell Server] Unknown message type: ${msg.type}`);
      }
    } catch (err) {
      console.error("[Shell Server] Error handling message:", err);
      ws.send(
        JSON.stringify({
          type: "error",
          error: err.message,
        })
      );
    }
  });

  ws.on("close", (code, reason) => {
    console.log(`[Shell Server] Client disconnected: ${clientId} (code: ${code}, reason: ${reason || 'none'})`);
    // Clean up PTY if still running
    if (ptyProcess) {
      console.log(`[Shell Server] Cleaning up PTY: ${shellId}`);
      try {
        ptyProcess.kill();
      } catch (e) {
        console.log(`[Shell Server] Error killing PTY: ${e.message}`);
      }
      sessions.delete(shellId);
    }
  });

  ws.on("error", (err) => {
    console.error(`[Shell Server] WebSocket error for ${clientId}:`, err);
  });

  // Send welcome message
  ws.send(
    JSON.stringify({
      type: "connected",
      message: "Connected to PolyMech Shell PTY Server",
      platform: process.platform,
      availableShells: Object.keys(shellMap).filter((sh) => {
        // Check if shell exists (simplified check)
        if (process.platform === "win32") {
          return ["powershell", "cmd", "wsl", "git-bash"].includes(sh);
        }
        return ["bash", "zsh", "fish"].includes(sh);
      }),
    })
  );
});

// Start server
server.listen(PORT, HOST, () => {
  console.log(`[Shell Server] Listening on ws://${HOST}:${PORT}`);
  console.log(`[Shell Server] Press Ctrl+C to stop`);
});

// Graceful shutdown
process.on("SIGINT", () => {
  console.log("\n[Shell Server] Shutting down...");

  // Kill all PTY sessions
  for (const [id, session] of sessions) {
    console.log(`[Shell Server] Killing session: ${id}`);
    session.pty.kill();
  }
  sessions.clear();

  wss.close(() => {
    server.close(() => {
      console.log("[Shell Server] Stopped");
      process.exit(0);
    });
  });
});

// Handle uncaught errors
process.on("uncaughtException", (err) => {
  console.error("[Shell Server] Uncaught exception:", err);
});

process.on("unhandledRejection", (reason, promise) => {
  console.error("[Shell Server] Unhandled rejection at:", promise, "reason:", reason);
});
