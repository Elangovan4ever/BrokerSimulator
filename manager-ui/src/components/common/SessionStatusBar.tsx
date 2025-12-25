import { useEffect } from 'react';
import { Clock, Play, Pause, FastForward } from 'lucide-react';
import { useSessionStore } from '@/stores/sessionStore';
import { StatusIndicator } from './StatusIndicator';
import { cn } from '@/lib/utils';

function formatSimTime(isoString: string | undefined): string {
  if (!isoString) return '--:--:--';
  try {
    const date = new Date(isoString);
    return date.toLocaleTimeString('en-US', {
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
    const date = new Date(isoString);
    return date.toLocaleDateString('en-US', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
    });
  } catch {
    return '----------';
  }
}

export function SessionStatusBar() {
  const { sessions, fetchSession, fetchSessions } = useSessionStore();

  // Filter for active sessions (running or paused)
  const activeSessions = sessions.filter(
    s => s.status === 'RUNNING' || s.status === 'PAUSED'
  );

  // Poll running sessions for updates
  useEffect(() => {
    // Initial fetch
    fetchSessions();

    const interval = setInterval(() => {
      const currentSessions = useSessionStore.getState().sessions;
      currentSessions
        .filter(s => s.status === 'RUNNING')
        .forEach(s => fetchSession(s.id));
    }, 1000); // Update every second for smooth timestamp display

    return () => clearInterval(interval);
  }, [fetchSession, fetchSessions]);

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
