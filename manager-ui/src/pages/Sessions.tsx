import { useEffect, useState } from 'react';
import {
  Play,
  Pause,
  Square,
  Trash2,
  Plus,
  RefreshCw,
  Clock,
  FastForward,
  ChevronRight,
} from 'lucide-react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Badge } from '@/components/ui/badge';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from '@/components/ui/dialog';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { ScrollArea } from '@/components/ui/scroll-area';
import { StatusIndicator } from '@/components/common/StatusIndicator';
import { useSessionStore } from '@/stores/sessionStore';
import { formatCurrency, formatTimestamp, formatPercent } from '@/lib/utils';
import { toast } from 'sonner';
import type { Session, SessionConfig } from '@/types';

export function Sessions() {
  const {
    sessions,
    selectedSessionId,
    isLoading,
    error,
    fetchSessions,
    fetchSession,
    createSession,
    deleteSession,
    startSession,
    pauseSession,
    resumeSession,
    stopSession,
    setSpeed,
    selectSession,
    clearError,
  } = useSessionStore();

  const [isCreateOpen, setIsCreateOpen] = useState(false);
  const [newSession, setNewSession] = useState<SessionConfig>({
    symbols: ['AAPL'],
    start_time: '2024-01-15T09:30:00',
    end_time: '2024-01-15T16:00:00',
    initial_capital: 100000,
    speed_factor: 0,
  });

  useEffect(() => {
    fetchSessions();
    const interval = setInterval(() => {
      sessions.filter(s => s.status === 'RUNNING').forEach(s => fetchSession(s.id));
    }, 2000);
    return () => clearInterval(interval);
  }, [fetchSessions]);

  useEffect(() => {
    if (error) {
      toast.error(error);
      clearError();
    }
  }, [error, clearError]);

  const selectedSession = sessions.find(s => s.id === selectedSessionId);

  const handleCreate = async () => {
    try {
      // Format times without 'Z' suffix and milliseconds for C++ server
      const formatTime = (iso: string) => iso.replace('Z', '').split('.')[0];
      const config = {
        ...newSession,
        start_time: formatTime(newSession.start_time),
        end_time: formatTime(newSession.end_time),
      };
      const session = await createSession(config);
      toast.success(`Session ${session.id.slice(0, 8)} created`);
      setIsCreateOpen(false);
      selectSession(session.id);
    } catch {
      toast.error('Failed to create session');
    }
  };

  const handleDelete = async (sessionId: string) => {
    if (confirm('Are you sure you want to delete this session?')) {
      await deleteSession(sessionId);
      toast.success('Session deleted');
    }
  };

  const getStatusVariant = (status: string) => {
    switch (status) {
      case 'RUNNING': return 'success';
      case 'PAUSED': return 'warning';
      case 'ERROR': return 'destructive';
      default: return 'secondary';
    }
  };

  return (
    <div className="p-6 h-full flex flex-col">
      {/* Header */}
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-bold">Sessions</h1>
          <p className="text-muted-foreground">Manage backtest simulation sessions</p>
        </div>
        <div className="flex gap-2">
          <Button variant="outline" onClick={() => fetchSessions()}>
            <RefreshCw className={`h-4 w-4 mr-2 ${isLoading ? 'animate-spin' : ''}`} />
            Refresh
          </Button>
          <Dialog open={isCreateOpen} onOpenChange={setIsCreateOpen}>
            <DialogTrigger asChild>
              <Button>
                <Plus className="h-4 w-4 mr-2" />
                New Session
              </Button>
            </DialogTrigger>
            <DialogContent className="max-w-md">
              <DialogHeader>
                <DialogTitle>Create New Session</DialogTitle>
                <DialogDescription>
                  Configure a new backtest simulation session
                </DialogDescription>
              </DialogHeader>
              <div className="space-y-4 py-4">
                <div className="space-y-2">
                  <Label htmlFor="symbols">Symbols (comma-separated)</Label>
                  <Input
                    id="symbols"
                    value={newSession.symbols.join(', ')}
                    onChange={(e) => setNewSession({
                      ...newSession,
                      symbols: e.target.value.split(',').map(s => s.trim().toUpperCase()),
                    })}
                    placeholder="AAPL, MSFT, GOOGL"
                  />
                </div>
                <div className="grid grid-cols-2 gap-4">
                  <div className="space-y-2">
                    <Label htmlFor="start_time">Start Time</Label>
                    <Input
                      id="start_time"
                      type="datetime-local"
                      value={newSession.start_time.slice(0, 16)}
                      onChange={(e) => setNewSession({
                        ...newSession,
                        start_time: e.target.value + ':00',
                      })}
                    />
                  </div>
                  <div className="space-y-2">
                    <Label htmlFor="end_time">End Time</Label>
                    <Input
                      id="end_time"
                      type="datetime-local"
                      value={newSession.end_time.slice(0, 16)}
                      onChange={(e) => setNewSession({
                        ...newSession,
                        end_time: e.target.value + ':00',
                      })}
                    />
                  </div>
                </div>
                <div className="grid grid-cols-2 gap-4">
                  <div className="space-y-2">
                    <Label htmlFor="capital">Initial Capital</Label>
                    <Input
                      id="capital"
                      type="number"
                      value={newSession.initial_capital}
                      onChange={(e) => setNewSession({
                        ...newSession,
                        initial_capital: parseFloat(e.target.value),
                      })}
                    />
                  </div>
                  <div className="space-y-2">
                    <Label htmlFor="speed">Speed Factor</Label>
                    <Select
                      value={String(newSession.speed_factor || 0)}
                      onValueChange={(v) => setNewSession({
                        ...newSession,
                        speed_factor: parseFloat(v),
                      })}
                    >
                      <SelectTrigger>
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="0">Max Speed</SelectItem>
                        <SelectItem value="1">1x (Real-time)</SelectItem>
                        <SelectItem value="10">10x</SelectItem>
                        <SelectItem value="100">100x</SelectItem>
                        <SelectItem value="1000">1000x</SelectItem>
                      </SelectContent>
                    </Select>
                  </div>
                </div>
              </div>
              <DialogFooter>
                <Button variant="outline" onClick={() => setIsCreateOpen(false)}>
                  Cancel
                </Button>
                <Button onClick={handleCreate}>Create Session</Button>
              </DialogFooter>
            </DialogContent>
          </Dialog>
        </div>
      </div>

      {/* Main Content */}
      <div className="flex-1 flex gap-6 min-h-0">
        {/* Session List */}
        <Card className="w-96 flex flex-col">
          <CardHeader className="pb-2">
            <CardTitle className="text-lg">Sessions ({sessions.length})</CardTitle>
          </CardHeader>
          <CardContent className="flex-1 p-0">
            <ScrollArea className="h-full">
              <div className="p-4 space-y-2">
                {sessions.length === 0 ? (
                  <p className="text-center text-muted-foreground py-8">
                    No sessions yet
                  </p>
                ) : (
                  sessions.map((session) => (
                    <div
                      key={session.id}
                      className={`p-3 rounded-lg border cursor-pointer transition-colors ${
                        selectedSessionId === session.id
                          ? 'border-primary bg-primary/5'
                          : 'hover:bg-muted/50'
                      }`}
                      onClick={() => selectSession(session.id)}
                    >
                      <div className="flex items-center justify-between">
                        <div className="flex items-center gap-2">
                          <StatusIndicator
                            status={
                              session.status === 'RUNNING' ? 'running' :
                              session.status === 'PAUSED' ? 'paused' :
                              session.status === 'ERROR' ? 'error' : 'stopped'
                            }
                          />
                          <span className="font-mono text-sm">
                            {session.id.slice(0, 8)}
                          </span>
                        </div>
                        <ChevronRight className="h-4 w-4 text-muted-foreground" />
                      </div>
                      <div className="mt-2 flex items-center gap-2 text-xs text-muted-foreground">
                        <span>{(session.symbols || []).slice(0, 3).join(', ') || 'No symbols'}</span>
                        {(session.symbols?.length || 0) > 3 && <span>+{session.symbols!.length - 3}</span>}
                      </div>
                    </div>
                  ))
                )}
              </div>
            </ScrollArea>
          </CardContent>
        </Card>

        {/* Session Details */}
        <Card className="flex-1 flex flex-col">
          {selectedSession ? (
            <>
              <CardHeader className="pb-4">
                <div className="flex items-center justify-between">
                  <div>
                    <CardTitle className="font-mono">{selectedSession.id}</CardTitle>
                    <CardDescription>
                      Created {formatTimestamp(selectedSession.created_at)}
                    </CardDescription>
                  </div>
                  <Badge variant={getStatusVariant(selectedSession.status)}>
                    {selectedSession.status}
                  </Badge>
                </div>
              </CardHeader>
              <CardContent className="flex-1 space-y-6">
                {/* Controls */}
                <div className="flex items-center gap-2">
                  {selectedSession.status === 'CREATED' && (
                    <Button onClick={() => startSession(selectedSession.id)}>
                      <Play className="h-4 w-4 mr-2" />
                      Start
                    </Button>
                  )}
                  {selectedSession.status === 'RUNNING' && (
                    <Button onClick={() => pauseSession(selectedSession.id)}>
                      <Pause className="h-4 w-4 mr-2" />
                      Pause
                    </Button>
                  )}
                  {selectedSession.status === 'PAUSED' && (
                    <>
                      <Button onClick={() => resumeSession(selectedSession.id)}>
                        <Play className="h-4 w-4 mr-2" />
                        Resume
                      </Button>
                      <Button variant="outline" onClick={() => stopSession(selectedSession.id)}>
                        <Square className="h-4 w-4 mr-2" />
                        Stop
                      </Button>
                    </>
                  )}
                  {['RUNNING', 'PAUSED'].includes(selectedSession.status) && (
                    <Select
                      value={String(selectedSession.speed_factor || 0)}
                      onValueChange={(v) => setSpeed(selectedSession.id, parseFloat(v))}
                    >
                      <SelectTrigger className="w-32">
                        <FastForward className="h-4 w-4 mr-2" />
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="0">Max Speed</SelectItem>
                        <SelectItem value="1">1x</SelectItem>
                        <SelectItem value="10">10x</SelectItem>
                        <SelectItem value="100">100x</SelectItem>
                      </SelectContent>
                    </Select>
                  )}
                  <div className="flex-1" />
                  <Button
                    variant="destructive"
                    size="icon"
                    onClick={() => handleDelete(selectedSession.id)}
                  >
                    <Trash2 className="h-4 w-4" />
                  </Button>
                </div>

                <Separator />

                {/* Session Info */}
                <div className="grid grid-cols-2 gap-6">
                  <div>
                    <h4 className="font-medium mb-3">Configuration</h4>
                    <dl className="space-y-2 text-sm">
                      <div className="flex justify-between">
                        <dt className="text-muted-foreground">Symbols</dt>
                        <dd>{selectedSession.symbols?.join(', ') || 'No symbols'}</dd>
                      </div>
                      <div className="flex justify-between">
                        <dt className="text-muted-foreground">Start Time</dt>
                        <dd>{formatTimestamp(selectedSession.start_time)}</dd>
                      </div>
                      <div className="flex justify-between">
                        <dt className="text-muted-foreground">End Time</dt>
                        <dd>{formatTimestamp(selectedSession.end_time)}</dd>
                      </div>
                      <div className="flex justify-between">
                        <dt className="text-muted-foreground">Initial Capital</dt>
                        <dd>{formatCurrency(selectedSession.initial_capital)}</dd>
                      </div>
                    </dl>
                  </div>
                  <div>
                    <h4 className="font-medium mb-3">Progress</h4>
                    <dl className="space-y-2 text-sm">
                      <div className="flex justify-between">
                        <dt className="text-muted-foreground">Current Time</dt>
                        <dd>{selectedSession.current_time ? formatTimestamp(selectedSession.current_time) : '-'}</dd>
                      </div>
                      <div className="flex justify-between">
                        <dt className="text-muted-foreground">Events Processed</dt>
                        <dd>{selectedSession.events_processed?.toLocaleString() || 0}</dd>
                      </div>
                      <div className="flex justify-between">
                        <dt className="text-muted-foreground">Speed Factor</dt>
                        <dd>{selectedSession.speed_factor === 0 ? 'Max' : `${selectedSession.speed_factor}x`}</dd>
                      </div>
                    </dl>
                  </div>
                </div>

                {/* Account State */}
                {selectedSession.account && (
                  <>
                    <Separator />
                    <div>
                      <h4 className="font-medium mb-3">Account</h4>
                      <div className="grid grid-cols-4 gap-4">
                        <div className="p-3 rounded-lg bg-muted/50">
                          <p className="text-xs text-muted-foreground">Equity</p>
                          <p className="text-lg font-semibold">{formatCurrency(selectedSession.account.equity)}</p>
                        </div>
                        <div className="p-3 rounded-lg bg-muted/50">
                          <p className="text-xs text-muted-foreground">Cash</p>
                          <p className="text-lg font-semibold">{formatCurrency(selectedSession.account.cash)}</p>
                        </div>
                        <div className="p-3 rounded-lg bg-muted/50">
                          <p className="text-xs text-muted-foreground">Buying Power</p>
                          <p className="text-lg font-semibold">{formatCurrency(selectedSession.account.buying_power)}</p>
                        </div>
                        <div className="p-3 rounded-lg bg-muted/50">
                          <p className="text-xs text-muted-foreground">Unrealized P&L</p>
                          <p className={`text-lg font-semibold ${
                            selectedSession.account.unrealized_pl >= 0 ? 'text-success' : 'text-destructive'
                          }`}>
                            {formatCurrency(selectedSession.account.unrealized_pl)}
                          </p>
                        </div>
                      </div>
                    </div>
                  </>
                )}

                {/* Performance */}
                {selectedSession.performance && (
                  <>
                    <Separator />
                    <div>
                      <h4 className="font-medium mb-3">Performance</h4>
                      <div className="grid grid-cols-4 gap-4">
                        <div className="p-3 rounded-lg bg-muted/50">
                          <p className="text-xs text-muted-foreground">Total Return</p>
                          <p className={`text-lg font-semibold ${
                            selectedSession.performance.total_return >= 0 ? 'text-success' : 'text-destructive'
                          }`}>
                            {formatPercent(selectedSession.performance.total_return)}
                          </p>
                        </div>
                        <div className="p-3 rounded-lg bg-muted/50">
                          <p className="text-xs text-muted-foreground">Max Drawdown</p>
                          <p className="text-lg font-semibold text-destructive">
                            {formatPercent(-selectedSession.performance.max_drawdown)}
                          </p>
                        </div>
                        <div className="p-3 rounded-lg bg-muted/50">
                          <p className="text-xs text-muted-foreground">Sharpe Ratio</p>
                          <p className="text-lg font-semibold">
                            {selectedSession.performance.sharpe_ratio.toFixed(2)}
                          </p>
                        </div>
                        <div className="p-3 rounded-lg bg-muted/50">
                          <p className="text-xs text-muted-foreground">Total Trades</p>
                          <p className="text-lg font-semibold">
                            {selectedSession.performance.total_trades}
                          </p>
                        </div>
                      </div>
                    </div>
                  </>
                )}
              </CardContent>
            </>
          ) : (
            <CardContent className="flex-1 flex items-center justify-center">
              <p className="text-muted-foreground">Select a session to view details</p>
            </CardContent>
          )}
        </Card>
      </div>
    </div>
  );
}
