import { cn } from '@/lib/utils';

interface StatusIndicatorProps {
  status: 'online' | 'offline' | 'running' | 'paused' | 'stopped' | 'error' | 'connected' | 'disconnected' | 'connecting';
  size?: 'sm' | 'md' | 'lg';
  showLabel?: boolean;
}

export function StatusIndicator({ status, size = 'md', showLabel = false }: StatusIndicatorProps) {
  const sizeClasses = {
    sm: 'h-2 w-2',
    md: 'h-2.5 w-2.5',
    lg: 'h-3 w-3',
  };

  const statusClasses = {
    online: 'bg-success',
    running: 'bg-success animate-pulse',
    connected: 'bg-success',
    paused: 'bg-warning',
    connecting: 'bg-warning animate-pulse',
    offline: 'bg-muted-foreground',
    stopped: 'bg-muted-foreground',
    disconnected: 'bg-muted-foreground',
    error: 'bg-destructive',
  };

  const labelClasses = {
    online: 'text-success',
    running: 'text-success',
    connected: 'text-success',
    paused: 'text-warning',
    connecting: 'text-warning',
    offline: 'text-muted-foreground',
    stopped: 'text-muted-foreground',
    disconnected: 'text-muted-foreground',
    error: 'text-destructive',
  };

  return (
    <div className="flex items-center gap-2">
      <div
        className={cn(
          'rounded-full',
          sizeClasses[size],
          statusClasses[status]
        )}
      />
      {showLabel && (
        <span className={cn('text-sm capitalize', labelClasses[status])}>
          {status}
        </span>
      )}
    </div>
  );
}
