import { useCallback, useEffect, useRef, useState } from "react";
import type { Terminal } from "@xterm/xterm";
import type { ShellType } from "./shellStore";

interface PtyMessage {
  type: string;
  shellId?: string;
  data?: string;
  cols?: number;
  rows?: number;
  shellType?: ShellType;
  pid?: number;
  exitCode?: number;
  signal?: number;
  error?: string;
  message?: string;
  platform?: string;
  availableShells?: string[];
  sessions?: Array<{
    shellId: string;
    shellType: string;
    pid: number;
    createdAt: Date;
  }>;
}

interface UsePtyWebSocketOptions {
  shellId: string;
  shellType: ShellType;
  terminal: Terminal | null;
  enabled?: boolean;
  onConnect?: () => void;
  onDisconnect?: () => void;
  onError?: (error: string) => void;
  onExit?: (code: number, signal?: number) => void;
  onOutput?: (data: string) => void;
}

const WS_URL = import.meta.env.VITE_PTY_WS_URL || "ws://localhost:8080";
const RECONNECT_INTERVAL = 5000; // 5 seconds
const MAX_RECONNECT_ATTEMPTS = 10;

export function usePtyWebSocket({
  shellId,
  shellType,
  terminal,
  enabled = true,
  onConnect,
  onDisconnect,
  onError,
  onExit,
  onOutput,
}: UsePtyWebSocketOptions) {
  const wsRef = useRef<WebSocket | null>(null);
  const terminalRef = useRef<Terminal | null>(terminal);
  const readyRef = useRef(false);
  const shouldReconnectRef = useRef(false);
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const reconnectAttemptsRef = useRef(0);
  const callbacksRef = useRef({ onConnect, onDisconnect, onError, onExit, onOutput });
  const [isConnected, setIsConnected] = useState(false);
  const [isReady, setIsReady] = useState(false);
  const [platform, setPlatform] = useState<string>("");
  const [availableShells, setAvailableShells] = useState<string[]>([]);
  const [reconnectAttempt, setReconnectAttempt] = useState(0);

  useEffect(() => {
    terminalRef.current = terminal;
  }, [terminal]);

  useEffect(() => {
    callbacksRef.current = { onConnect, onDisconnect, onError, onExit, onOutput };
  }, [onConnect, onDisconnect, onError, onExit, onOutput]);

  const clearReconnectTimer = useCallback(() => {
    if (reconnectTimerRef.current) {
      clearTimeout(reconnectTimerRef.current);
      reconnectTimerRef.current = null;
    }
  }, []);

  const setReady = useCallback((value: boolean) => {
    readyRef.current = value;
    setIsReady(value);
  }, []);

  const connect = useCallback(() => {
    if (!enabled) return;
    if (wsRef.current && (wsRef.current.readyState === WebSocket.OPEN || wsRef.current.readyState === WebSocket.CONNECTING)) {
      console.log(`[PtyWS] Socket already active for ${shellId}, skipping connect`);
      return;
    }

    clearReconnectTimer();
    shouldReconnectRef.current = true;

    console.log(`[PtyWS] Connecting to ${WS_URL} (attempt ${reconnectAttemptsRef.current + 1}) for ${shellId}...`);
    const ws = new WebSocket(WS_URL);
    wsRef.current = ws;

    ws.onopen = () => {
      console.log("[PtyWS] Connected successfully");
      reconnectAttemptsRef.current = 0;
      setReconnectAttempt(0);
      setIsConnected(true);
      callbacksRef.current.onConnect?.();
    };

    let hasCreatedPty = false;

    ws.onmessage = (event) => {
      try {
        const msg: PtyMessage = JSON.parse(event.data);

        switch (msg.type) {
          case "connected":
            console.log("[PtyWS] Server ready:", msg.message);
            setPlatform(msg.platform || "");
            setAvailableShells(msg.availableShells || []);

            // Create PTY session only once
            const term = terminalRef.current;
            if (term && !hasCreatedPty) {
              hasCreatedPty = true;
              const dims = term.cols && term.rows
                ? { cols: term.cols, rows: term.rows }
                : { cols: 80, rows: 24 };

              console.log(`[PtyWS] Sending create PTY request for ${shellId}`);
              ws.send(
                JSON.stringify({
                  type: "create",
                  shellId,
                  shellType,
                  ...dims,
                })
              );
            } else if (!term) {
              console.log("[PtyWS] Terminal not ready yet, skipping PTY creation");
            }
            break;

          case "created":
            console.log(`[PtyWS] PTY created: ${msg.shellId} (pid: ${msg.pid})`);
            setReady(true);
            break;

          case "output":
            // Write output to terminal
            if (terminalRef.current && msg.data) {
              try {
                terminalRef.current.write(msg.data);
                callbacksRef.current.onOutput?.(msg.data);
              } catch (err) {
                console.error("[PtyWS] Error writing to terminal:", err);
                console.error("[PtyWS] Data that caused error:", msg.data.substring(0, 100));
              }
            }
            break;

          case "exit":
            console.log(`[PtyWS] PTY exited: code=${msg.exitCode}, signal=${msg.signal}`);
            setReady(false);
            callbacksRef.current.onExit?.(msg.exitCode || 0, msg.signal);
            break;

          case "error":
            console.error("[PtyWS] Server error:", msg.error);
            callbacksRef.current.onError?.(msg.error || "Unknown error");
            break;

          case "list":
            console.log("[PtyWS] Active sessions:", msg.sessions);
            break;

          default:
            console.log("[PtyWS] Unknown message:", msg);
        }
      } catch (err) {
        console.error("[PtyWS] Failed to parse message:", err);
      }
    };

    ws.onclose = (event) => {
      console.log(`[PtyWS] Disconnected (code: ${event.code}, reason: '${event.reason || 'none'}', wasClean: ${event.wasClean})`);
      console.log(`[PtyWS] Close code ${event.code} meaning:`, 
        event.code === 1000 ? 'Normal closure' :
        event.code === 1001 ? 'Going away' :
        event.code === 1005 ? 'No status received (browser closed/tab closed)' :
        event.code === 1006 ? 'Abnormal closure' :
        event.code === 1011 ? 'Server error' :
        'Unknown'
      );
      setIsConnected(false);
      setReady(false);
      wsRef.current = null;
      callbacksRef.current.onDisconnect?.();

      if (shouldReconnectRef.current && enabled && reconnectAttemptsRef.current < MAX_RECONNECT_ATTEMPTS) {
        reconnectAttemptsRef.current++;
        setReconnectAttempt(reconnectAttemptsRef.current);
        console.log(`[PtyWS] Will reconnect in ${RECONNECT_INTERVAL}ms (attempt ${reconnectAttemptsRef.current}/${MAX_RECONNECT_ATTEMPTS})`);

        reconnectTimerRef.current = setTimeout(() => {
          if (!shouldReconnectRef.current) {
            console.log("[PtyWS] Reconnect cancelled");
            return;
          }
          console.log("[PtyWS] Attempting to reconnect...");
          connect();
        }, RECONNECT_INTERVAL);
      } else if (reconnectAttemptsRef.current >= MAX_RECONNECT_ATTEMPTS) {
        console.error("[PtyWS] Max reconnection attempts reached. Giving up.");
        callbacksRef.current.onError?.("Failed to connect after maximum retry attempts");
      }
    };

    ws.onerror = (error) => {
      console.error("[PtyWS] WebSocket error:", error);
      // Don't call onError here - let onclose handle reconnection
    };
  }, [clearReconnectTimer, enabled, setReady, shellId, shellType]);

  // Initial connection
  useEffect(() => {
    if (!enabled) return;

    connect();

    return () => {
      shouldReconnectRef.current = false;
      clearReconnectTimer();

      if (wsRef.current) {
        // Prevent reconnection on intentional close
        const ws = wsRef.current;
        ws.onclose = null;

        if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
          // Send kill message before closing
          if (readyRef.current) {
            try {
              ws.send(JSON.stringify({ type: "kill", shellId }));
            } catch (e) {
              // Ignore send errors during cleanup
            }
          }
          ws.close();
        }
        wsRef.current = null;
      }
    };
  }, [clearReconnectTimer, connect, enabled, shellId]);

  // Send input to PTY
  const sendInput = useCallback(
    (data: string) => {
      if (wsRef.current?.readyState === WebSocket.OPEN && isReady) {
        wsRef.current.send(
          JSON.stringify({
            type: "input",
            shellId,
            data,
          })
        );
      } else {
        console.log("[PtyWS] Cannot send input - not connected or not ready");
      }
    },
    [shellId, isReady]
  );

  // Resize PTY
  const resize = useCallback(
    (cols: number, rows: number) => {
      if (wsRef.current?.readyState === WebSocket.OPEN && isReady) {
        wsRef.current.send(
          JSON.stringify({
            type: "resize",
            shellId,
            cols,
            rows,
          })
        );
      }
    },
    [shellId, isReady]
  );

  // Kill PTY
  const kill = useCallback(() => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(
        JSON.stringify({
          type: "kill",
          shellId,
        })
      );
    }
  }, [shellId]);

  // Manual reconnect function
  const reconnect = useCallback(() => {
    console.log("[PtyWS] Manual reconnect triggered");
    reconnectAttemptsRef.current = 0;
    setReconnectAttempt(0);

    // Close existing connection
    if (wsRef.current) {
      wsRef.current.onclose = null;
      wsRef.current.close();
      wsRef.current = null;
    }

    // Clear any pending reconnect
    clearReconnectTimer();

    // Connect immediately
    setTimeout(connect, 100);
  }, [clearReconnectTimer, connect]);

  return {
    isConnected,
    isReady,
    platform,
    availableShells,
    reconnectAttempt,
    sendInput,
    resize,
    kill,
    reconnect,
  };
}
