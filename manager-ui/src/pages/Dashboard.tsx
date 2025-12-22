import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import {
  Activity,
  Server,
  Play,
  Pause,
  Square,
  TrendingUp,
  Clock,
  Zap,
  RefreshCw,
} from 'lucide-react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Badge } from '@/components/ui/badge';
import { StatusIndicator } from '@/components/common/StatusIndicator';
import { useSessionStore } from '@/stores/sessionStore';
import { checkAllServicesStatus } from '@/api/status';
import { formatCurrency, formatTimestamp } from '@/lib/utils';
import type { SimulatorStatus } from '@/types';

export function Dashboard() {
  const { sessions, fetchSessions, isLoading } = useSessionStore();
  const [serviceStatus, setServiceStatus] = useState<SimulatorStatus[]>([]);
  const [isRefreshing, setIsRefreshing] = useState(false);

  useEffect(() => {
    fetchSessions();
    refreshStatus();
  }, [fetchSessions]);

  const refreshStatus = async () => {
    setIsRefreshing(true);
    try {
      const status = await checkAllServicesStatus();
      setServiceStatus(status);
    } catch (error) {
      console.error('Failed to check status:', error);
    } finally {
      setIsRefreshing(false);
    }
  };

  const runningSessions = sessions.filter(s => s.status === 'RUNNING').length;
  const pausedSessions = sessions.filter(s => s.status === 'PAUSED').length;
  const onlineServices = serviceStatus.filter(s => s.status === 'online').length;

  return (
    <div className="p-6 space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold">Dashboard</h1>
          <p className="text-muted-foreground">
            BrokerSimulator Management Overview
          </p>
        </div>
        <Button onClick={refreshStatus} disabled={isRefreshing}>
          <RefreshCw className={`h-4 w-4 mr-2 ${isRefreshing ? 'animate-spin' : ''}`} />
          Refresh
        </Button>
      </div>

      {/* Stats Cards */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        <Card>
          <CardHeader className="flex flex-row items-center justify-between pb-2">
            <CardTitle className="text-sm font-medium">Services</CardTitle>
            <Server className="h-4 w-4 text-muted-foreground" />
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-bold">{onlineServices}/{serviceStatus.length}</div>
            <p className="text-xs text-muted-foreground">
              {onlineServices === serviceStatus.length ? 'All online' : 'Some offline'}
            </p>
          </CardContent>
        </Card>

        <Card>
          <CardHeader className="flex flex-row items-center justify-between pb-2">
            <CardTitle className="text-sm font-medium">Active Sessions</CardTitle>
            <Play className="h-4 w-4 text-muted-foreground" />
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-bold">{runningSessions}</div>
            <p className="text-xs text-muted-foreground">
              {pausedSessions} paused
            </p>
          </CardContent>
        </Card>

        <Card>
          <CardHeader className="flex flex-row items-center justify-between pb-2">
            <CardTitle className="text-sm font-medium">Total Sessions</CardTitle>
            <Activity className="h-4 w-4 text-muted-foreground" />
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-bold">{sessions.length}</div>
            <p className="text-xs text-muted-foreground">
              All time
            </p>
          </CardContent>
        </Card>

        <Card>
          <CardHeader className="flex flex-row items-center justify-between pb-2">
            <CardTitle className="text-sm font-medium">Events Processed</CardTitle>
            <Zap className="h-4 w-4 text-muted-foreground" />
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-bold">
              {sessions.reduce((acc, s) => acc + (s.events_processed || 0), 0).toLocaleString()}
            </div>
            <p className="text-xs text-muted-foreground">
              Across all sessions
            </p>
          </CardContent>
        </Card>
      </div>

      {/* Services Status */}
      <Card>
        <CardHeader>
          <CardTitle>Simulator Services</CardTitle>
          <CardDescription>Connection status to elanlinux services</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
            {serviceStatus.map((service) => (
              <div
                key={service.service}
                className="flex items-center justify-between p-4 rounded-lg border"
              >
                <div>
                  <p className="font-medium capitalize">{service.service}</p>
                  <p className="text-xs text-muted-foreground">Port {service.port}</p>
                </div>
                <div className="text-right">
                  <StatusIndicator status={service.status} showLabel />
                  {service.latency && (
                    <p className="text-xs text-muted-foreground mt-1">{service.latency}ms</p>
                  )}
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      {/* Recent Sessions */}
      <Card>
        <CardHeader className="flex flex-row items-center justify-between">
          <div>
            <CardTitle>Recent Sessions</CardTitle>
            <CardDescription>Latest backtest sessions</CardDescription>
          </div>
          <Link to="/sessions">
            <Button variant="outline" size="sm">View All</Button>
          </Link>
        </CardHeader>
        <CardContent>
          {isLoading ? (
            <div className="text-center py-8 text-muted-foreground">Loading...</div>
          ) : sessions.length === 0 ? (
            <div className="text-center py-8 text-muted-foreground">
              <p>No sessions yet</p>
              <Link to="/sessions">
                <Button className="mt-4">Create Session</Button>
              </Link>
            </div>
          ) : (
            <div className="space-y-4">
              {sessions.slice(0, 5).map((session) => (
                <div
                  key={session.id}
                  className="flex items-center justify-between p-4 rounded-lg border"
                >
                  <div className="flex items-center gap-4">
                    <StatusIndicator
                      status={
                        session.status === 'RUNNING' ? 'running' :
                        session.status === 'PAUSED' ? 'paused' :
                        session.status === 'COMPLETED' ? 'stopped' :
                        session.status === 'ERROR' ? 'error' : 'stopped'
                      }
                      size="lg"
                    />
                    <div>
                      <p className="font-medium font-mono text-sm">
                        {session.id.slice(0, 8)}...
                      </p>
                      <div className="flex items-center gap-2 text-xs text-muted-foreground">
                        <span>{session.symbols.join(', ')}</span>
                        <span>•</span>
                        <span>{formatTimestamp(session.created_at)}</span>
                      </div>
                    </div>
                  </div>
                  <div className="flex items-center gap-4">
                    <div className="text-right">
                      {session.account && (
                        <p className="font-medium">
                          {formatCurrency(session.account.equity)}
                        </p>
                      )}
                      <p className="text-xs text-muted-foreground">
                        {session.events_processed?.toLocaleString()} events
                      </p>
                    </div>
                    <Badge
                      variant={
                        session.status === 'RUNNING' ? 'success' :
                        session.status === 'PAUSED' ? 'warning' :
                        session.status === 'ERROR' ? 'destructive' : 'secondary'
                      }
                    >
                      {session.status}
                    </Badge>
                  </div>
                </div>
              ))}
            </div>
          )}
        </CardContent>
      </Card>

      {/* Quick Actions */}
      <Card>
        <CardHeader>
          <CardTitle>Quick Actions</CardTitle>
          <CardDescription>Common tasks and shortcuts</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
            <Link to="/sessions">
              <Button variant="outline" className="w-full h-20 flex-col gap-2">
                <Play className="h-6 w-6" />
                New Session
              </Button>
            </Link>
            <Link to="/explorer">
              <Button variant="outline" className="w-full h-20 flex-col gap-2">
                <Activity className="h-6 w-6" />
                API Explorer
              </Button>
            </Link>
            <Link to="/websocket">
              <Button variant="outline" className="w-full h-20 flex-col gap-2">
                <TrendingUp className="h-6 w-6" />
                WebSocket Monitor
              </Button>
            </Link>
            <Link to="/settings">
              <Button variant="outline" className="w-full h-20 flex-col gap-2">
                <Clock className="h-6 w-6" />
                Settings
              </Button>
            </Link>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
