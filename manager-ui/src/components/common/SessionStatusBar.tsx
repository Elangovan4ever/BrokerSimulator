import { useEffect, useRef } from 'react';
import { Clock, Play, Pause, FastForward, Wifi, WifiOff } from 'lucide-react';
import { useSessionStore } from '@/stores/sessionStore';
import { getStatusWsUrl } from '@/api/config';
import { StatusIndicator } from './StatusIndicator';
import { cn } from '@/lib/utils';

const ET_TIMEZONE = 'America/New_York';

function formatSimTime(isoString: string | undefined): string {
  if (!isoString) return '--:--:--';
  try {
    // Treat timestamp as UTC and convert to ET
    const utcString = isoString.endsWith('Z') ? isoString : isoString + 'Z';
    const date = new Date(utcString);
    return date.toLocaleTimeString('en-US', {
      timeZone: ET_TIMEZONE,
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      hour12: false,
    });
  } catch {
    return '--:--:--';
  }
}

function formatSimDate(isoString: string | undefined): string {
  if (!isoString) return '----------';
  try {
    // Treat timestamp as UTC and convert to ET
    const utcString = isoString.endsWith('Z') ? isoString : isoString + 'Z';
    const date = new Date(utcString);
    return date.toLocaleDateString('en-US', {
      timeZone: ET_TIMEZONE,
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
    });
  } catch {
    return '----------';
  }
}

export function SessionStatusBar() {
  const { sessions, fetchSessions, updateSessionFromWs } = useSessionStore();
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimeoutRef = useRef<NodeJS.Timeout | null>(null);
  const isConnectedRef = useRef(false);
  const isActiveRef = useRef(true);

  // Filter for active sessions (running or paused)
  const activeSessions = sessions.filter(
    s => s.status === 'RUNNING' || s.status === 'PAUSED'
  );

  // WebSocket connection for real-time updates
  useEffect(() => {
    isActiveRef.current = true;

    // Initial fetch to get session list
    fetchSessions();

    const connect = () => {
      const wsUrl = getStatusWsUrl();
      const ws = new WebSocket(wsUrl);

      ws.onopen = () => {
        if (!isActiveRef.current) {
          ws.close();
          return;
        }
        isConnectedRef.current = true;
        // Clear any pending reconnect
        if (reconnectTimeoutRef.current) {
          clearTimeout(reconnectTimeoutRef.current);
          reconnectTimeoutRef.current = null;
        }
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);

          if (data.type === 'session_status') {
            // Update session with real-time data
            updateSessionFromWs({
              session_id: data.session_id,
              status: data.status,
              current_time_ns: data.current_time_ns,
              events_processed: data.events_processed,
              speed_factor: data.speed_factor,
            });
          } else if (data.type === 'session_created' || data.type === 'session_deleted') {
            // Refetch sessions list when sessions are added/removed
            fetchSessions();
          }
        } catch (err) {
          console.warn('Failed to parse WebSocket message:', err);
        }
      };

      ws.onclose = () => {
        if (!isActiveRef.current) {
          return;
        }
        isConnectedRef.current = false;
        wsRef.current = null;

        // Attempt to reconnect after 2 seconds
        reconnectTimeoutRef.current = setTimeout(() => {
          connect();
        }, 2000);
      };

      ws.onerror = () => {
        // Error handling - onclose will trigger reconnect
      };

      wsRef.current = ws;
    };

    connect();

    return () => {
      isActiveRef.current = false;
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
      }
      if (wsRef.current) {
        if (wsRef.current.readyState === WebSocket.OPEN) {
          wsRef.current.close();
        }
      }
    };
  }, [fetchSessions, updateSessionFromWs]);

  if (activeSessions.length === 0) {
    return null;
  }

  return (
    <div className="h-10 border-t bg-card flex items-center px-4 gap-6 text-sm">
      <div className="flex items-center gap-2 text-muted-foreground">
        <Clock className="h-4 w-4" />
        <span className="font-medium">Sessions:</span>
      </div>

      <div className="flex-1 flex items-center gap-4 overflow-x-auto">
        {activeSessions.map(session => (
          <div
            key={session.id}
            className={cn(
              'flex items-center gap-3 px-3 py-1 rounded-md',
              session.status === 'RUNNING' ? 'bg-primary/10' : 'bg-muted'
            )}
          >
            {/* Status indicator */}
            <StatusIndicator
              status={session.status === 'RUNNING' ? 'running' : 'paused'}
            />

            {/* Session ID (truncated) */}
            <span className="font-mono text-xs text-muted-foreground">
              {session.id.slice(0, 8)}
            </span>

            {/* Symbols */}
            <span className="text-xs">
              {session.symbols?.slice(0, 2).join(', ')}
              {(session.symbols?.length || 0) > 2 && '...'}
            </span>

            {/* Current simulation time */}
            <div className="flex items-center gap-1.5 font-mono">
              <span className="text-muted-foreground text-xs">
                {formatSimDate(session.current_time)}
              </span>
              <span className={cn(
                'font-semibold',
                session.status === 'RUNNING' ? 'text-primary' : 'text-muted-foreground'
              )}>
                {formatSimTime(session.current_time)}
              </span>
            </div>

            {/* Speed indicator */}
            <div className="flex items-center gap-1 text-xs text-muted-foreground">
              <FastForward className="h-3 w-3" />
              <span>
                {session.speed_factor === 0 ? 'Max' : `${session.speed_factor}x`}
              </span>
            </div>

            {/* Status icon */}
            {session.status === 'RUNNING' ? (
              <Play className="h-3 w-3 text-green-500 fill-green-500" />
            ) : (
              <Pause className="h-3 w-3 text-yellow-500" />
            )}
          </div>
        ))}
      </div>

      {/* Total active count */}
      <div className="text-xs text-muted-foreground">
        {activeSessions.filter(s => s.status === 'RUNNING').length} running
        {activeSessions.filter(s => s.status === 'PAUSED').length > 0 && (
          <span>, {activeSessions.filter(s => s.status === 'PAUSED').length} paused</span>
        )}
      </div>
    </div>
  );
}
